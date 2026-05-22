#include "FreeRTOS.h"
#include "task.h"
#include "tc_handler.h"
#include "beacon.h"
#include "comms.h"
#include "frame.h"
#include "lora_cfg.h"
#include "tm_builder.h"
#include "types.h"
#include "notifications.h"
#include "task_management.h"
#include "main.h"
#include "time.h"
#include "flash.h"
#include <string.h>
#include <stdint.h>

static inline void notify(TaskHandle_t handle, uint32_t bits)
{
    if (handle != NULL) {
        xTaskNotify(handle, bits, eSetBits);
    }
}

static void enqueue_nack(uint8_t ref_type, uint8_t ref_seq, uint8_t reason)
{
    /* §9: TC_LORA_CONFIG can be rejected after the eager ACK has already
     * been sent by the transceiver. Emit an explicit NACK on the TX queue;
     * the GS lora_cfg_service interprets a post-ACK NACK as rejection. */
    uint8_t buf[AIR_FRAME_MAX];
    uint8_t len = air_encode_nack(buf, comms_next_seq(), ref_type, ref_seq, reason);

    TxQueueEntry_t entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.frame, buf, len);
    entry.frame_len = len;
    entry.needs_ack = 0;

    QueueHandle_t tx_q = comms_get_tx_queue();
    if (tx_q != NULL && xQueueSend(tx_q, &entry, 0) == pdTRUE) {
        TaskHandle_t trx = tm_get_task_handle(TM_TASK_TRANSCEIVER);
        if (trx != NULL) {
            xTaskNotify(trx, N_TRANSCEIVER_TX_READY_BIT, eSetBits);
        }
    }
}

static void enqueue_tm(uint8_t tm_id, const uint8_t *tm_body, uint8_t body_len,
                       uint8_t ref_tc_id, uint8_t ref_seq)
{
    /* TM header: BODY_VER, TM_ID, EPOCH(4), REF_TC_ID, REF_SEQ = 8 bytes */
    uint8_t payload[AIR_PAYLOAD_MAX];
    uint8_t idx = 0;

    uint32_t epoch = time_get_unix();

    payload[idx++] = AIR_BODY_VER;
    payload[idx++] = tm_id;
    payload[idx++] = (uint8_t)(epoch >> 24);
    payload[idx++] = (uint8_t)(epoch >> 16);
    payload[idx++] = (uint8_t)(epoch >> 8);
    payload[idx++] = (uint8_t)(epoch);
    payload[idx++] = ref_tc_id;
    payload[idx++] = ref_seq;

    if (body_len > 0 && (uint8_t)(idx + body_len) <= AIR_PAYLOAD_MAX) {
        memcpy(&payload[idx], tm_body, body_len);
        idx = (uint8_t)(idx + body_len);
    }

    uint8_t air_buf[AIR_FRAME_MAX];
    uint8_t frame_len = air_encode(air_buf, AIR_TM,
                                   AIR_FLAG_REQUIRES_ACK,
                                   comms_next_seq(),
                                   payload, idx);

    TxQueueEntry_t entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.frame, air_buf, frame_len);
    entry.frame_len = frame_len;
    entry.needs_ack = 1;

    QueueHandle_t tx_q = comms_get_tx_queue();
    if (tx_q != NULL && xQueueSend(tx_q, &entry, 0) == pdTRUE) {
        TaskHandle_t trx = tm_get_task_handle(TM_TASK_TRANSCEIVER);
        if (trx != NULL) {
            xTaskNotify(trx, N_TRANSCEIVER_TX_READY_BIT, eSetBits);
        }
    }
}

void tc_process(const AirFrame_t *frame)
{
    if (frame->len < 2) {
        return;
    }

    uint8_t tc_id = frame->payload[1];

    g_last_tc_id = tc_id;
    g_last_tc_rc = 0x00u;

    const uint8_t *p = frame->payload;

    switch ((tc_id_t)tc_id) {

    case TC_PING:
        break;

    case TC_TRANSIT_TO_NM:
        notify(main_get_obc_handle(), N_OBC_EXIT_STATE_TO_OBC_STATE_NM);
        break;

    case TC_TRANSIT_TO_CM:
        notify(main_get_obc_handle(), N_OBC_EXIT_STATE_TO_OBC_STATE_CM);
        break;

    case TC_TRANSIT_TO_SSM:
        notify(main_get_obc_handle(), N_OBC_EXIT_STATE_TO_OBC_STATE_SSM);
        break;

    case TC_TRANSIT_TO_SM:
        notify(main_get_obc_handle(), N_OBC_EXIT_STATE_TO_OBC_STATE_SM);
        break;

    case TC_LORA_CONFIG: {
        /* §10.1: 23-byte TC body = BODY_VER + TC_ID + 21 B §9.2 config struct
         * (CFG_ID..REVERT_AFTER_S; the §9.2 BODY_VER is omitted). */
        if (frame->len < 23u) {
            g_last_tc_rc = NACK_BAD_PARAMS;
            enqueue_nack(frame->type, frame->seq, NACK_BAD_PARAMS);
            break;
        }
        LoraConfig_t cfg;
        lora_cfg_result_t pr = lora_cfg_parse(&p[2], &cfg);
        if (pr != LORA_CFG_OK) {
            g_last_tc_rc = NACK_CONFIG_REJECTED;
            enqueue_nack(frame->type, frame->seq, NACK_CONFIG_REJECTED);
            break;
        }
        lora_cfg_result_t rr = lora_cfg_request(&cfg);
        if (rr == LORA_CFG_BUSY) {
            g_last_tc_rc = NACK_BUSY;
            enqueue_nack(frame->type, frame->seq, NACK_BUSY);
        } else if (rr == LORA_CFG_REJECTED) {
            g_last_tc_rc = NACK_CONFIG_REJECTED;
            enqueue_nack(frame->type, frame->seq, NACK_CONFIG_REJECTED);
        }
        printf("TC: received LoRa config TC, result=%d\r\n", (int)rr);
        break;
    }

    case TC_COMMS_PARAMS:
        /* TODO Phase 4: persist params to flash */
        notify(tm_get_task_handle(TM_TASK_COMMS), N_COMMS_NEW_PARAMS);
        break;

    case TC_UPLOAD_UNIX_TIME:
        if (frame->len >= 6u) {
            time_set_unix(((uint32_t)p[2] << 24) |
                          ((uint32_t)p[3] << 16) |
                          ((uint32_t)p[4] << 8)  |
                          (uint32_t)p[5]);
            notify(main_get_obc_handle(), N_OBC_UPDATE_TIME);
        }
        break;

    case TC_UPLOAD_EPS_TH:
        OBDH_Write_Request(EPS_THRESHOLDS_ADDR, &p[2], 3);
        notify(tm_get_task_handle(TM_TASK_EPS), N_EPS_NEW_THRESHOLDS);
        break;

    case TC_UPLOAD_PL_CONFIG:
        OBDH_Write_Request(RFI_CONFIG_ADDR, &p[2], 8);
        break;

    case TC_REQUEST_BEACON_NOW:
        notify(tm_get_task_handle(TM_TASK_BEACON), N_COMMS_TRANSMIT_BEACON);
        break;

    case TC_REQUEST_DOWNLINK_CONFIG: {
        uint8_t body[16];
        tm_build_downlink_config(body);
        enqueue_tm(TM_DOWNLINK_CONFIG, body, 16u, tc_id, frame->seq);
        break;
    }

    case TC_REQUEST_HK_LIVE: {
        uint8_t body[24];
        tm_build_hk_live(body);
        enqueue_tm(TM_HK_LIVE, body, 24u, tc_id, frame->seq);
        break;
    }

    case TC_REQUEST_HK_HISTORY:
        /* TODO: ARQ downlink of historic telemetry */
        break;

    case TC_REQUEST_PAYLOAD_DATA:
        /* TODO: ARQ downlink of payload data */
        break;

    case TC_REQUEST_OBC_LOG:
        /* TODO: ARQ downlink of OBC log */
        break;

    case TC_UPLOAD_TLE_BEGIN:
        /* TODO: ARQ upload session for TLE */
        break;

    case TC_UPLOAD_ADCS_CAL_BEGIN:
        /* TODO: ARQ upload session for ADCS calibration */
        break;

    case TC_EPS_HEATER_ENABLE:
        notify(tm_get_task_handle(TM_TASK_EPS), N_EPS_ENABLE_AUTO_HEAT);
        break;

    case TC_EPS_HEATER_DISABLE:
        notify(tm_get_task_handle(TM_TASK_EPS), N_EPS_DISABLE_AUTO_HEAT);
        break;

    case TC_POL_PAYLOAD_SHUT:
    case TC_POL_PAYLOAD_ENABLE:
    case TC_POL_ADCS_SHUT:
    case TC_POL_ADCS_ENABLE:
    case TC_POL_BURNCOMMS_SHUT:
    case TC_POL_BURNCOMMS_ENABLE:
    case TC_POL_HEATER_SHUT:
    case TC_POL_HEATER_ENABLE:
        /* TODO: PoL rail control */
        break;

    case TC_CLEAR_PL_DATA:
        notify(tm_get_task_handle(TM_TASK_OBDH), N_OBDH_CLEAR_PAYLOAD);
        break;

    case TC_CLEAR_FLASH:
        notify(tm_get_task_handle(TM_TASK_OBDH), N_OBDH_CLEAR_FLASH);
        break;

    case TC_CLEAR_HT:
        notify(tm_get_task_handle(TM_TASK_OBDH), N_OBDH_CLEAR_HT);
        break;

    case TC_COMMS_STOP_TX:
        notify(tm_get_task_handle(TM_TASK_COMMS), N_COMMS_STOP_RF);
        break;

    case TC_COMMS_RESUME_TX:
        notify(tm_get_task_handle(TM_TASK_COMMS), N_COMMS_RESUME_RF);
        break;

    case TC_SET_BEACON_PERIOD:
        if (frame->len >= 4u) {
            uint16_t period_s = (uint16_t)(((uint16_t)p[2] << 8) | p[3]);
            beacon_set_period((uint32_t)period_s * 1000u);
        }
        break;

    case TC_PAYLOAD_SCHEDULE:
        /* TODO: OBDH write for schedule parameters */
        notify(tm_get_task_handle(TM_TASK_PAYLOAD), N_PAYLOAD_ACTIVATE);
        break;

    case TC_PAYLOAD_DEACTIVATE:
        notify(tm_get_task_handle(TM_TASK_PAYLOAD), N_PAYLOAD_DEACTIVATE);
        break;

    case TC_OBC_HARD_REBOOT:
        notify(main_get_obc_handle(), N_OBC_HARD_REBOOT);
        break;

    case TC_OBC_SOFT_REBOOT:
        notify(main_get_obc_handle(), N_OBC_SOFT_REBOOT);
        break;

    case TC_OBC_PERIPH_REBOOT:
        notify(main_get_obc_handle(), N_OBC_PERIPHERALS_REBOOT);
        break;

    case TC_OBC_DEBUG_MODE:
        /* TODO: enter debug mode */
        break;

    default:
        break;
    }
}
