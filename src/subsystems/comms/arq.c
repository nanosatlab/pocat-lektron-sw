#include "arq.h"
#include "saw.h"
#include "frame.h"
#include "timing.h"
#include "cad.h"
#include "lora_cfg.h"
#include "comms.h"
#include "health.h"
#include "radiolib_wrapper.h"
#include "notifications.h"
#include "task_management.h"
#include "types.h"
#include "flash.h"
#include "obdh_requests.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ---- CRC-32/ISO-HDLC (poly 0x04C11DB7 reflected, init 0xFFFFFFFF) ---- */

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint32_t crc32_finish(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

/* ---- Session state ---- */

typedef enum {
    ARQ_IDLE = 0,
    ARQ_DL_ACTIVE,   /* OBC is sender (downlink) */
    ARQ_UL_ARMED,    /* waiting for DATA_BEGIN from GS */
    ARQ_UL_ACTIVE,   /* OBC is receiver (uplink) */
} arq_state_t;

typedef struct {
    arq_state_t     state;
    uint8_t         session_id;
    transfer_type_t transfer_type;   /* also used as ul_expected_type when ARMED */
    uint8_t         window_size;
    uint8_t         block_size;
    uint16_t        total_blocks;
    uint32_t        total_bytes;

    /* DL params from the triggering TC */
    uint8_t         dl_params[8];
    uint8_t         dl_params_len;

    /* UL reassembly */
    uint8_t         ul_buf[ARQ_UL_BUF_MAX];
    uint32_t        ul_bytes;
    uint16_t        ul_rx_base;        /* lowest block not yet received */
    uint8_t         ul_bitmap[8];      /* received blocks above ul_rx_base */
    uint16_t        ul_blocks_received;
} arq_session_t;

static arq_session_t g_sess;

/* ---- Flash address map for DL transfer types ---- */

/* TRANSFER_OBC_LOG has no real log-to-flash capture subsystem yet (it reuses
 * the telemetry area as a stand-in source). To exercise the ARQ engine and
 * measure throughput at a realistic size (test-campaign.md §3.2) without
 * reading into unrelated flash regions, OBC_LOG sessions cycle through a
 * small, legitimately-OBC_LOG-owned window of flash, repeating it as many
 * times as needed to reach ARQ_OBC_LOG_TOTAL_BYTES — see dl_block_addr(). */
#define ARQ_OBC_LOG_TOTAL_BYTES  8192u
#define ARQ_OBC_LOG_SRC_WINDOW    256u   /* TELEMETRY_ADDR .. COUNT_PACKET_ADDR */

static uint32_t dl_base_addr(transfer_type_t type)
{
    switch (type) {
    case TRANSFER_HK_HISTORY:    return TELEMETRY_ADDR;
    case TRANSFER_PAYLOAD_DATA:  return PHOTO_ADDR;
    default:                     return TELEMETRY_ADDR; /* OBC_LOG: reuse telemetry area */
    }
}

/* Flash address to read for one DL block. OBC_LOG wraps within
 * ARQ_OBC_LOG_SRC_WINDOW (repeating content) since its source bytes are a
 * placeholder, not a real per-block log buffer; other transfer types read
 * linearly as before. */
static uint32_t dl_block_addr(transfer_type_t type, uint32_t base_addr,
                              uint16_t block_idx, uint8_t block_size)
{
    uint32_t off = (uint32_t)block_idx * block_size;
    if (type == TRANSFER_OBC_LOG) {
        off %= ARQ_OBC_LOG_SRC_WINDOW;
    }
    return base_addr + off;
}

/* Return total bytes for a DL session based on type and params. */
static uint32_t dl_total_bytes(transfer_type_t type, const uint8_t *p, uint8_t plen)
{
    switch (type) {
    case TRANSFER_PAYLOAD_DATA: {
        /* p[0]=MEASURE_NUMBER, p[1:2]=FIRST_BLOCK, p[3:4]=BLOCK_COUNT */
        uint16_t block_count = (plen >= 5u) ?
            (uint16_t)(((uint16_t)p[3] << 8) | p[4]) : 0u;
        if (block_count == 0u) { block_count = 20u; }
        return (uint32_t)block_count * ARQ_BLOCK_SIZE;
    }
    case TRANSFER_HK_HISTORY:
        return 2048u; /* 2 KB of historic HK (≈128 × 16-B records) */
    case TRANSFER_OBC_LOG:
        return ARQ_OBC_LOG_TOTAL_BYTES;
    default:
        return 1024u;
    }
}

/* ---- UL flash write destinations ---- */

static void ul_write_flash(transfer_type_t type, const uint8_t *data, uint32_t len)
{
    TaskHandle_t adcs = tm_get_task_handle(TM_TASK_ADCS);

    switch (type) {
    case TRANSFER_TLE_UPLOAD:
        obdh_write_request(TLE_ADDR, data, len);
        if (adcs != NULL) {
            xTaskNotify(adcs, N_ADCS_NEW_TLE, eSetBits);
        }
        printf("ARQ: TLE written %lu B\r\n", (unsigned long)len);
        break;
    case TRANSFER_ADCS_CALIBRATION:
        obdh_write_request(CALIBRATION_ADDR, data, len);
        if (adcs != NULL) {
            xTaskNotify(adcs, N_ADCS_NEW_CALIBRATION, eSetBits);
        }
        printf("ARQ: ADCS cal written %lu B\r\n", (unsigned long)len);
        break;
    case TRANSFER_FLIGHT_PARAMS:
        if (len > 8u) { len = 8u; }
        obdh_write_request(RFI_CONFIG_ADDR, data, len);
        printf("ARQ: flight params written %lu B\r\n", (unsigned long)len);
        break;
    default:
        printf("ARQ: UL unknown type 0x%02x\r\n", (unsigned)type);
        break;
    }
}

/* ---- Low-level TX helpers (called from transceiver_task context) ---- */

#define TX_DONE_TIMEOUT_MS  5000u

static void arq_tx_raw(const uint8_t *buf, uint8_t len)
{
    RadioLib_Standby();
    xTaskNotifyStateClear(NULL);
    RadioLib_StartTransmit((uint8_t *)buf, len);
    xTaskNotifyWait(0, N_TRANSCEIVER_RADIO_IRQ_BIT, NULL,
                    pdMS_TO_TICKS(TX_DONE_TIMEOUT_MS));
    RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
}

/* CAD + transmit + wait TX_DONE for one DATA block. */
static void arq_send_data_block(transfer_type_t type, uint8_t session_id,
                                 uint16_t block_idx, uint8_t block_size,
                                 uint8_t actual_size, uint32_t base_addr,
                                 bool is_retx)
{
    uint8_t block_data[ARQ_BLOCK_SIZE];
    obdh_read_request(dl_block_addr(type, base_addr, block_idx, block_size),
                      block_data, actual_size);

    /* DATA payload: [SESSION_ID][BLOCK_INDEX_HI][BLOCK_INDEX_LO][data...] */
    uint8_t payload[3u + ARQ_BLOCK_SIZE];
    payload[0] = session_id;
    payload[1] = (uint8_t)(block_idx >> 8);
    payload[2] = (uint8_t)(block_idx);
    memcpy(&payload[3], block_data, actual_size);

    uint8_t flags = is_retx ? AIR_FLAG_IS_RETX : 0u;
    uint8_t frame_buf[AIR_FRAME_MAX];
    uint8_t frame_len = air_encode(frame_buf, AIR_DATA, flags,
                                   comms_next_seq(), payload,
                                   (uint8_t)(3u + actual_size));
    if (frame_len == 0u) { return; }

    if (cad_precheck() == CAD_RESULT_FREE) {
        arq_tx_raw(frame_buf, frame_len);
    }
    /* else: CAD busy — skip block; GS will re-request it via DATA_ACK bitmap */
}

/* Wait for DATA_ACK from GS (called from transceiver_task, like saw_send).
 * Forwards non-ARQ frames to rx_q. Returns true when matching DATA_ACK arrives. */
static bool arq_wait_data_ack(QueueHandle_t rx_q, uint32_t timeout_ms,
                               uint8_t session_id, AirFrame_t *frame_out)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    for (;;) {
        TickType_t now  = xTaskGetTickCount();
        TickType_t rem  = (deadline > now) ? (deadline - now) : 0u;
        if (rem == 0u) { return false; }

        uint32_t irq_notif = 0u;
        TickType_t wait = (rem > pdMS_TO_TICKS(2000u)) ? pdMS_TO_TICKS(2000u) : rem;
        BaseType_t got = xTaskNotifyWait(0u, N_TRANSCEIVER_RADIO_IRQ_BIT,
                                          &irq_notif, wait);
        health_kick(HEALTH_BIT_TRANSCEIVER);
        if (got != pdTRUE || !(irq_notif & N_TRANSCEIVER_RADIO_IRQ_BIT)) {
            continue;
        }

        uint32_t irq = RadioLib_GetIrqFlags();
        RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
        if (!(irq & RADIOLIB_SX126X_IRQ_RX_DONE)) { continue; }

        uint8_t raw[AIR_FRAME_MAX];
        uint16_t raw_len = 0u;
        int16_t rssi = 0; int8_t snr = 0;
        int16_t st = RadioLib_ReadRxData(raw, AIR_FRAME_MAX, &raw_len, &rssi, &snr);
        if (st != RADIOLIB_ERR_NONE || raw_len == 0u) {
            RadioLib_StartReceive(lora_cfg_active_preamble());
            continue;
        }

        AirFrame_t f;
        if (air_decode(raw, (uint8_t)raw_len, &f) != 0) {
            printf("ARQ DL: wait_ack decode fail len=%u\r\n", (unsigned)raw_len);
            RadioLib_StartReceive(lora_cfg_active_preamble());
            continue;
        }
        lora_cfg_notify_rx();

        printf("ARQ DL: wait_ack rx type=0x%02x len=%u sid=%u\r\n",
               (unsigned)f.type, (unsigned)f.len,
               (f.len >= 1u) ? (unsigned)f.payload[0] : 0u);

        if (f.type == AIR_DATA_ACK && f.len >= 4u && f.payload[0] == session_id) {
            uint16_t base = (uint16_t)(((uint16_t)f.payload[1] << 8) | f.payload[2]);
            printf("ARQ DL: DATA_ACK matched base=%u bmap_len=%u\r\n",
                   (unsigned)base, (unsigned)f.payload[3]);
            memcpy(frame_out, &f, sizeof(AirFrame_t));
            return true;
        }

        /* Not our DATA_ACK — forward to comms for regular processing */
        if (rx_q != NULL) {
            RxAirFrame_t pkt;
            pkt.air  = f;
            pkt.rssi = rssi;
            pkt.snr  = snr;
            xQueueSend(rx_q, &pkt, 0);
        }
        RadioLib_StartReceive(lora_cfg_active_preamble());
    }
}

/* Inline stop-and-wait for DATA_BEGIN / DATA_END (REQUIRES_ACK=1).
 * Mirrors saw_send but without the separate saw.c dependency on rx_q routing. */
static saw_result_t arq_saw_frame(const uint8_t *frame, uint8_t frame_len,
                                  uint8_t seq, uint8_t type,
                                  QueueHandle_t rx_q)
{
    uint8_t tmp[AIR_FRAME_MAX];
    memcpy(tmp, frame, frame_len);
    uint32_t timeout_ms = ack_timeout_ms(frame_len);

    for (uint8_t attempt = 0u; attempt <= SAW_MAX_RETX; attempt++) {
        health_kick(HEALTH_BIT_TRANSCEIVER);

        if (attempt > 0u) {
            tmp[2] |= AIR_FLAG_IS_RETX;
        }

        if (cad_precheck() == CAD_RESULT_GIVEUP) {
            return SAW_CAD_FAIL;
        }

        arq_tx_raw(tmp, frame_len);
        RadioLib_StartReceive(lora_cfg_active_preamble());

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
        for (;;) {
            TickType_t now = xTaskGetTickCount();
            TickType_t rem = (deadline > now) ? (deadline - now) : 0u;
            if (rem == 0u) { break; }

            uint32_t notif = 0u;
            if (xTaskNotifyWait(0u, N_TRANSCEIVER_RADIO_IRQ_BIT,
                                &notif, rem) != pdTRUE) {
                break;
            }
            if (!(notif & N_TRANSCEIVER_RADIO_IRQ_BIT)) { continue; }

            uint32_t irq = RadioLib_GetIrqFlags();
            RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            if (!(irq & RADIOLIB_SX126X_IRQ_RX_DONE)) { continue; }

            uint8_t raw[AIR_FRAME_MAX];
            uint16_t raw_len = 0u;
            int16_t rssi = 0; int8_t snr = 0;
            int16_t st = RadioLib_ReadRxData(raw, AIR_FRAME_MAX, &raw_len, &rssi, &snr);
            if (st != RADIOLIB_ERR_NONE || raw_len == 0u) {
                RadioLib_StartReceive(lora_cfg_active_preamble());
                continue;
            }

            AirFrame_t f;
            if (air_decode(raw, (uint8_t)raw_len, &f) != 0) {
                RadioLib_StartReceive(lora_cfg_active_preamble());
                continue;
            }
            lora_cfg_notify_rx();

            if (f.type == AIR_ACK && f.len >= 2u &&
                f.payload[0] == type && f.payload[1] == seq) {
                return SAW_OK;
            }
            if (f.type == AIR_NACK && f.len >= 2u &&
                f.payload[0] == type && f.payload[1] == seq) {
                return SAW_NACKED;
            }

            if (rx_q != NULL) {
                RxAirFrame_t pkt;
                pkt.air  = f;
                pkt.rssi = rssi;
                pkt.snr  = snr;
                xQueueSend(rx_q, &pkt, 0);
            }
            RadioLib_StartReceive(lora_cfg_active_preamble());
        }
    }
    return SAW_TIMEOUT;
}

/* Enqueue DATA_ACK to the TX queue so the transceiver sends it (UL sessions). */
static void ul_send_data_ack(void)
{
    /* DATA_ACK payload: [SESSION_ID][BASE_BLOCK_HI][BASE_BLOCK_LO][BITMAP_LEN][BITMAP 8B] */
    uint8_t payload[12];
    payload[0] = g_sess.session_id;
    payload[1] = (uint8_t)(g_sess.ul_rx_base >> 8);
    payload[2] = (uint8_t)(g_sess.ul_rx_base);
    payload[3] = 8u;
    memcpy(&payload[4], g_sess.ul_bitmap, 8u);

    uint8_t frame_buf[AIR_FRAME_MAX];
    uint8_t frame_len = air_encode(frame_buf, AIR_DATA_ACK, 0x00u,
                                   comms_next_seq(), payload, 12u);

    TxQueueEntry_t entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.frame, frame_buf, frame_len);
    entry.frame_len = frame_len;
    entry.needs_ack = 0;

    QueueHandle_t tx_q = comms_get_tx_queue();
    if (tx_q != NULL && xQueueSend(tx_q, &entry, 0) == pdTRUE) {
        TaskHandle_t trx = tm_get_task_handle(TM_TASK_TRANSCEIVER);
        if (trx != NULL) {
            xTaskNotify(trx, N_TRANSCEIVER_TX_READY_BIT, eSetBits);
        }
    }
}

/* ---- Public API ---- */

bool arq_active(void)
{
    return g_sess.state != ARQ_IDLE;
}

int arq_begin_dl(uint8_t session_id, transfer_type_t type,
                 const uint8_t *params, uint8_t params_len)
{
    printf("ARQ: request begin DL session_id=%u type=0x%02x params_len=%u\r\n",
           (unsigned)session_id, (unsigned)type, (unsigned)params_len);
    if (g_sess.state != ARQ_IDLE) { return -1; }

    memset(&g_sess, 0, sizeof(g_sess));
    g_sess.state         = ARQ_DL_ACTIVE;
    g_sess.session_id    = session_id;
    g_sess.transfer_type = type;
    g_sess.window_size   = ARQ_WINDOW_SIZE;
    g_sess.block_size    = ARQ_BLOCK_SIZE;

    if (params != NULL && params_len > 0u && params_len <= sizeof(g_sess.dl_params)) {
        memcpy(g_sess.dl_params, params, params_len);
        g_sess.dl_params_len = params_len;
    }

    g_sess.total_bytes  = dl_total_bytes(type, params, params_len);
    g_sess.total_blocks = (uint16_t)((g_sess.total_bytes + ARQ_BLOCK_SIZE - 1u)
                                     / ARQ_BLOCK_SIZE);

    printf("ARQ: begin DL session_id=%u type=%u total_blocks=%u\r\n",
           (unsigned)session_id, (unsigned)type, (unsigned)g_sess.total_blocks);

    TaskHandle_t trx = tm_get_task_handle(TM_TASK_TRANSCEIVER);
    if (trx != NULL) {
        xTaskNotify(trx, N_TRANSCEIVER_ARQ_DL_BIT, eSetBits);
    }
    return 0;
}

void arq_arm_ul(transfer_type_t type)
{
    if (g_sess.state != ARQ_IDLE) {
        printf("ARQ: arm UL failed — session already active\r\n");
        return;
    }
    memset(&g_sess, 0, sizeof(g_sess));
    g_sess.state         = ARQ_UL_ARMED;
    g_sess.transfer_type = type;   /* stored as expected type; overwritten on DATA_BEGIN */
    printf("ARQ: UL armed for type=0x%02x\r\n", (unsigned)type);
}

/* ---- arq_rx: called from comms_task for UL sessions ---- */

int arq_rx(const AirFrame_t *frame)
{
    switch (frame->type) {

    /* ---- DATA_BEGIN: GS opens a UL session ---- */
    case AIR_DATA_BEGIN: {
        if (g_sess.state != ARQ_UL_ARMED) {
            printf("ARQ: DATA_BEGIN but not armed\r\n");
            return 0;
        }
        /* Payload: [BODY_VER][SESSION_ID][TRANSFER_TYPE]
         *          [TOTAL_BLOCKS 2B][BLOCK_SIZE][WINDOW_SIZE]
         *          [TOTAL_BYTES 4B][META...] */
        if (frame->len < 11u) {
            printf("ARQ: DATA_BEGIN too short\r\n");
            return 0;
        }
        const uint8_t *p = frame->payload;

        g_sess.state        = ARQ_UL_ACTIVE;
        g_sess.session_id   = p[1];
        /* Validate transfer_type matches what we armed for */
        if ((transfer_type_t)p[2] != g_sess.transfer_type) {
            printf("ARQ: DATA_BEGIN type mismatch expected=0x%02x got=0x%02x\r\n",
                   (unsigned)g_sess.transfer_type, (unsigned)p[2]);
        }
        g_sess.total_blocks       = (uint16_t)(((uint16_t)p[3] << 8) | p[4]);
        g_sess.block_size         = p[5];
        g_sess.window_size        = p[6];
        g_sess.total_bytes        = ((uint32_t)p[7]  << 24) | ((uint32_t)p[8]  << 16)
                                  | ((uint32_t)p[9]  <<  8) |  (uint32_t)p[10];
        g_sess.ul_rx_base         = 0u;
        g_sess.ul_bytes           = 0u;
        g_sess.ul_blocks_received = 0u;
        memset(g_sess.ul_bitmap, 0, sizeof(g_sess.ul_bitmap));

        printf("ARQ: UL session active sid=%u total_blocks=%u total_bytes=%lu\r\n",
               (unsigned)g_sess.session_id, (unsigned)g_sess.total_blocks,
               (unsigned long)g_sess.total_bytes);

        ul_send_data_ack();
        break;
    }

    /* ---- DATA: one block of the UL session ---- */
    case AIR_DATA: {
        if (g_sess.state != ARQ_UL_ACTIVE) { return 0; }
        if (frame->len < 3u) { return 0; }

        const uint8_t *p = frame->payload;
        if (p[0] != g_sess.session_id) { return 0; }

        uint16_t block_idx    = (uint16_t)(((uint16_t)p[1] << 8) | p[2]);
        const uint8_t *bdata  = &p[3];
        uint8_t blen          = (uint8_t)(frame->len - 3u);

        if (block_idx >= g_sess.total_blocks) { return 0; }

        /* Determine received-bit status for this block */
        if (block_idx >= g_sess.ul_rx_base) {
            uint16_t offset = (uint16_t)(block_idx - g_sess.ul_rx_base);
            bool already_received = false;

            if (offset == 0u) {
                already_received = false; /* always "missing" from GS perspective */
            } else if (offset <= 64u) {
                uint8_t byte_idx = (uint8_t)((offset - 1u) / 8u);
                uint8_t bit_idx  = (uint8_t)((offset - 1u) % 8u);
                already_received = (g_sess.ul_bitmap[byte_idx] >> bit_idx) & 1u;
            }

            if (!already_received) {
                /* Write into reassembly buffer */
                uint32_t byte_off = (uint32_t)block_idx * g_sess.block_size;
                if (byte_off + blen <= ARQ_UL_BUF_MAX) {
                    memcpy(&g_sess.ul_buf[byte_off], bdata, blen);
                }

                if (offset == 0u) {
                    /* This IS rx_base — advance and compact bitmap */
                    g_sess.ul_rx_base++;
                    while (g_sess.ul_rx_base < g_sess.total_blocks) {
                        if (g_sess.ul_bitmap[0] & 0x01u) {
                            g_sess.ul_rx_base++;
                            for (int i = 0; i < 7; i++) {
                                g_sess.ul_bitmap[i] = (uint8_t)(
                                    (g_sess.ul_bitmap[i] >> 1)
                                    | (g_sess.ul_bitmap[i + 1] << 7));
                            }
                            g_sess.ul_bitmap[7] >>= 1;
                        } else {
                            break;
                        }
                    }
                } else if (offset <= 64u) {
                    uint8_t byte_idx = (uint8_t)((offset - 1u) / 8u);
                    uint8_t bit_idx  = (uint8_t)((offset - 1u) % 8u);
                    g_sess.ul_bitmap[byte_idx] |= (uint8_t)(1u << bit_idx);
                }

                g_sess.ul_blocks_received++;
            }
        }
        /* blocks below ul_rx_base are already committed — drop silently */

        /* Send DATA_ACK after each window's worth of blocks */
        if (g_sess.ul_blocks_received > 0u &&
            (g_sess.ul_blocks_received % g_sess.window_size) == 0u) {
            ul_send_data_ack();
        }
        break;
    }

    /* ---- DATA_END: GS closes the UL session ---- */
    case AIR_DATA_END: {
        if (g_sess.state != ARQ_UL_ACTIVE) { return 0; }
        if (frame->len < 6u) { return 0; }

        const uint8_t *p = frame->payload;
        if (p[0] != g_sess.session_id) { return 0; }

        uint8_t status = p[1];
        if (status == DATA_END_ABORTED_BY_SENDER) {
            printf("ARQ: UL aborted by GS\r\n");
            g_sess.state = ARQ_IDLE;
            return 0;
        }

        uint32_t rcv_crc = ((uint32_t)p[2] << 24) | ((uint32_t)p[3] << 16)
                          | ((uint32_t)p[4] <<  8) |  (uint32_t)p[5];

        /* Send final DATA_ACK acknowledging all received */
        ul_send_data_ack();

        /* Verify CRC32 over assembled buffer */
        uint32_t computed_len = g_sess.total_bytes;
        if (computed_len > ARQ_UL_BUF_MAX) { computed_len = ARQ_UL_BUF_MAX; }
        uint32_t crc = crc32_finish(crc32_update(0xFFFFFFFFu, g_sess.ul_buf, computed_len));

        if (crc != rcv_crc) {
            printf("ARQ: UL CRC32 mismatch computed=0x%08lx received=0x%08lx\r\n",
                   (unsigned long)crc, (unsigned long)rcv_crc);
            g_sess.state = ARQ_IDLE;
            return 0;
        }

        printf("ARQ: UL complete, CRC32 ok, writing %lu B\r\n",
               (unsigned long)computed_len);

        ul_write_flash(g_sess.transfer_type, g_sess.ul_buf, computed_len);

        g_sess.state = ARQ_IDLE;
        return 1;
    }

    default:
        break;
    }

    return 0;
}

/* ---- arq_run_dl: driven from transceiver_task context ---- */

void arq_run_dl(QueueHandle_t rx_q)
{
    if (g_sess.state != ARQ_DL_ACTIVE) { return; }

    const uint8_t  sid      = g_sess.session_id;
    const uint8_t  bsize    = g_sess.block_size;
    const uint8_t  wsz      = g_sess.window_size;
    const uint16_t total    = g_sess.total_blocks;
    const uint32_t tbytes   = g_sess.total_bytes;
    const transfer_type_t ttype = g_sess.transfer_type;
    const uint32_t base_addr = dl_base_addr(ttype);

    /* WINDOW_TIMEOUT_MS = W × ToA(DATA_max) + ToA(DATA_ACK_max=12B) + 500 */
    uint32_t win_timeout = (uint32_t)wsz * lora_toa_ms(3u + bsize)
                           + lora_toa_ms(12u) + 500u;

    /* ---- Step 1: DATA_BEGIN ---- */
    uint8_t db_payload[11];
    db_payload[0]  = AIR_BODY_VER;
    db_payload[1]  = sid;
    db_payload[2]  = (uint8_t)ttype;
    db_payload[3]  = (uint8_t)(total >> 8);
    db_payload[4]  = (uint8_t)(total);
    db_payload[5]  = bsize;
    db_payload[6]  = wsz;
    db_payload[7]  = (uint8_t)(tbytes >> 24);
    db_payload[8]  = (uint8_t)(tbytes >> 16);
    db_payload[9]  = (uint8_t)(tbytes >>  8);
    db_payload[10] = (uint8_t)(tbytes);

    uint8_t db_seq = comms_next_seq();
    uint8_t db_frame[AIR_FRAME_MAX];
    uint8_t db_len = air_encode(db_frame, AIR_DATA_BEGIN, AIR_FLAG_REQUIRES_ACK,
                                db_seq, db_payload, 11u);

    printf("ARQ DL: sending DATA_BEGIN\r\n");
    saw_result_t sr = arq_saw_frame(db_frame, db_len, db_seq, AIR_DATA_BEGIN, rx_q);
    if (sr != SAW_OK) {
        printf("ARQ DL: DATA_BEGIN failed (%d) — aborting\r\n", (int)sr);
        g_sess.state = ARQ_IDLE;
        return;
    }

    /* ---- Step 2: Window loop ---- */
    uint16_t tx_base = 0u;
    uint16_t tx_next = 0u;
    uint8_t  retx_attempts = 0u;

    RadioLib_StartReceive(lora_cfg_active_preamble());

    while (tx_base < total) {
        /* Fill the send window with unsent blocks */
        uint16_t win_end = (uint16_t)(tx_base + wsz);
        if (win_end > total) { win_end = total; }

        printf("ARQ DL: window tx_base=%u tx_next=%u win_end=%u total=%u\r\n",
               (unsigned)tx_base, (unsigned)tx_next,
               (unsigned)win_end, (unsigned)total);

        while (tx_next < win_end) {
            uint8_t actual = bsize;
            if (tx_next == total - 1u) {
                uint8_t rem = (uint8_t)(tbytes % bsize);
                if (rem != 0u) { actual = rem; }
            }
            arq_send_data_block(ttype, sid, tx_next, bsize, actual, base_addr,
                                retx_attempts > 0u);
            tx_next++;
            health_kick(HEALTH_BIT_TRANSCEIVER);
        }

        /* Arm receiver — radio is in standby after the last TX block */
        RadioLib_StartReceive(lora_cfg_active_preamble());

        /* Wait for DATA_ACK from GS */
        AirFrame_t ack_frame;
        bool got = arq_wait_data_ack(rx_q, win_timeout, sid, &ack_frame);

        if (!got) {
            retx_attempts++;
            printf("ARQ DL: window timeout (attempt %u/%u)\r\n",
                   (unsigned)retx_attempts, (unsigned)ARQ_MAX_RETX);
            if (retx_attempts > ARQ_MAX_RETX) {
                printf("ARQ DL: too many timeouts — aborting\r\n");
                uint8_t de_p[6] = { sid, DATA_END_ABORTED_BY_SENDER, 0, 0, 0, 0 };
                uint8_t de_seq = comms_next_seq();
                uint8_t de_f[AIR_FRAME_MAX];
                uint8_t de_l = air_encode(de_f, AIR_DATA_END, AIR_FLAG_REQUIRES_ACK,
                                          de_seq, de_p, 6u);
                arq_saw_frame(de_f, de_l, de_seq, AIR_DATA_END, rx_q);
                g_sess.state = ARQ_IDLE;
                return;
            }
            /* Retransmit entire outstanding window */
            tx_next = tx_base;
            RadioLib_StartReceive(lora_cfg_active_preamble());
            continue;
        }

        /* ---- Process DATA_ACK ---- */
        retx_attempts = 0u;
        const uint8_t *ap   = ack_frame.payload;
        uint16_t new_base   = (uint16_t)(((uint16_t)ap[1] << 8) | ap[2]);
        uint8_t  bmap_len   = ap[3];
        const uint8_t *bmap = &ap[4];

        printf("ARQ DL: DATA_ACK new_base=%u bmap_len=%u\r\n",
               (unsigned)new_base, (unsigned)bmap_len);

        tx_base = new_base;
        if (tx_base >= total) {
            printf("ARQ DL: all blocks acked tx_base=%u >= total=%u\r\n",
                   (unsigned)tx_base, (unsigned)total);
            break;
        }

        /* Retransmit tx_base (always missing) */
        printf("ARQ DL: retx missing tx_base=%u\r\n", (unsigned)tx_base);
        {
            uint8_t actual = bsize;
            if (tx_base == total - 1u) {
                uint8_t rem = (uint8_t)(tbytes % bsize);
                if (rem != 0u) { actual = rem; }
            }
            arq_send_data_block(ttype, sid, tx_base, bsize, actual, base_addr, true);
            health_kick(HEALTH_BIT_TRANSCEIVER);
        }

        /* Retransmit bitmap-identified gaps above tx_base */
        uint16_t covered = (uint16_t)(tx_base + 1u + (uint16_t)bmap_len * 8u);
        if (covered > total) { covered = total; }

        for (uint16_t k = 0u; k < (uint16_t)bmap_len * 8u; k++) {
            uint16_t blk = (uint16_t)(tx_base + 1u + k);
            if (blk >= total) { break; }
            bool received = (bmap[k / 8u] >> (k % 8u)) & 1u;
            if (!received) {
                printf("ARQ DL: retx gap blk=%u\r\n", (unsigned)blk);
                uint8_t actual = bsize;
                if (blk == total - 1u) {
                    uint8_t rem = (uint8_t)(tbytes % bsize);
                    if (rem != 0u) { actual = rem; }
                }
                arq_send_data_block(ttype, sid, blk, bsize, actual, base_addr, true);
                health_kick(HEALTH_BIT_TRANSCEIVER);
            }
        }

        /* Advance tx_next past the range we just covered */
        if (tx_next < covered) { tx_next = covered; }
        if (tx_next > total)   { tx_next = total; }

        RadioLib_StartReceive(lora_cfg_active_preamble());
        (void)lora_cfg_tick();
    }

    /* ---- Step 3: DATA_END with CRC32 ---- */
    printf("ARQ DL: all blocks sent, computing CRC32 total=%u\r\n", (unsigned)total);
    uint32_t crc = 0xFFFFFFFFu;
    for (uint16_t blk = 0u; blk < total; blk++) {
        uint8_t actual = bsize;
        if (blk == total - 1u) {
            uint8_t rem = (uint8_t)(tbytes % bsize);
            if (rem != 0u) { actual = rem; }
        }
        uint8_t blk_buf[ARQ_BLOCK_SIZE];
        obdh_read_request(dl_block_addr(ttype, base_addr, blk, bsize), blk_buf, actual);
        crc = crc32_update(crc, blk_buf, actual);
        health_kick(HEALTH_BIT_TRANSCEIVER);
    }
    crc = crc32_finish(crc);

    uint8_t de_payload[6];
    de_payload[0] = sid;
    de_payload[1] = DATA_END_COMPLETE_OK;
    de_payload[2] = (uint8_t)(crc >> 24);
    de_payload[3] = (uint8_t)(crc >> 16);
    de_payload[4] = (uint8_t)(crc >>  8);
    de_payload[5] = (uint8_t)(crc);

    uint8_t de_seq = comms_next_seq();
    uint8_t de_frame[AIR_FRAME_MAX];
    uint8_t de_len = air_encode(de_frame, AIR_DATA_END, AIR_FLAG_REQUIRES_ACK,
                                de_seq, de_payload, 6u);

    printf("ARQ DL: sending DATA_END crc=0x%08lx\r\n", (unsigned long)crc);
    arq_saw_frame(de_frame, de_len, de_seq, AIR_DATA_END, rx_q);

    g_sess.state = ARQ_IDLE;
    printf("ARQ DL: session complete\r\n");
}
