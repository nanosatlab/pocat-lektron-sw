#include "FreeRTOS.h"
#include "task.h"
#include "transceiver.h"
#include "comms.h"
#include "frame.h"
#include "dedup.h"
#include "cad.h"
#include "saw.h"
#include "lora_cfg.h"
#include "radiolib_wrapper.h"
#include "health.h"
#include "notifications.h"
#include "task_management.h"
#include "timing.h"
#include "types.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#define RF_FREQUENCY        868000000ul
#define TX_OUTPUT_POWER     18
#define LORA_BW_CODE        0
#define LORA_IQ_INV         0
#define TX_DONE_TIMEOUT_MS  5000u

static DedupRing_t s_dedup;

static void transmit_and_wait(uint8_t *buf, uint8_t len)
{
    RadioLib_Standby();
    /* Discard any stale notification (e.g. CAD_DONE from cad_precheck) before
     * starting TX — must be before StartTransmit to avoid racing with TX_DONE. */
    xTaskNotifyStateClear(NULL);
    RadioLib_StartTransmit(buf, len);
    xTaskNotifyWait(0, N_TRANSCEIVER_RADIO_IRQ_BIT, NULL,
                    pdMS_TO_TICKS(TX_DONE_TIMEOUT_MS));
    RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
}

void transceiver_task(void *pv_parameters)
{
    (void)pv_parameters;

    QueueHandle_t rx_q = comms_get_rx_queue();
    QueueHandle_t tx_q = comms_get_tx_queue();

    dedup_init(&s_dedup);
    lora_cfg_init();

    if (RadioLib_Init() != 0) {
        printf("TRX: radio init failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    RadioLib_SetChannel(RF_FREQUENCY);

    RadioLib_SetTxConfig(
        LORA_SF_DEFAULT,
        LORA_CR_DEFAULT,
        TX_OUTPUT_POWER,
        LORA_BW_CODE,
        LORA_IQ_INV,
        LORA_CRC_DEFAULT,
        LORA_PRE_DEFAULT);

    RadioLib_SetRxConfig(
        LORA_SF_DEFAULT,
        LORA_CR_DEFAULT,
        LORA_BW_CODE,
        LORA_IQ_INV,
        LORA_CRC_DEFAULT,
        LORA_PRE_DEFAULT);

    RadioLib_SetIrqTask(xTaskGetCurrentTaskHandle());
    RadioLib_StartReceive(LORA_PRE_DEFAULT);

    for (;;) {
        uint32_t notif = 0;
        xTaskNotifyWait(0,
                        N_TRANSCEIVER_RADIO_IRQ_BIT | N_TRANSCEIVER_TX_READY_BIT |
                        N_TASK_PAUSE | N_TASK_RESUME,
                        &notif, pdMS_TO_TICKS(1000));

        if (tm_check_pause(notif, NULL)) {
            continue;
        }

        if (notif & N_TRANSCEIVER_RADIO_IRQ_BIT) {
            uint32_t irq = RadioLib_GetIrqFlags();
            RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);

            if (irq & RADIOLIB_SX126X_IRQ_RX_DONE) {
                uint8_t raw_buf[AIR_FRAME_MAX];
                uint16_t raw_len = 0;
                int16_t rssi = 0;
                int8_t  snr  = 0;

                int16_t st = RadioLib_ReadRxData(raw_buf, AIR_FRAME_MAX,
                                                 &raw_len, &rssi, &snr);
                printf("TRX: RX done, st=%d, len=%d, rssi=%d, snr=%d\r\n",
                       (int)st, (int)raw_len, (int)rssi, (int)snr);
                if (st == RADIOLIB_ERR_NONE && raw_len >= AIR_FRAME_HDR) {
                    AirFrame_t frame;
                    if (air_decode(raw_buf, (uint8_t)raw_len, &frame) == 0) {
                        /* §9.1: a valid RX at new params confirms the link
                         * and disarms any pending revert. */
                        lora_cfg_notify_rx();

                        if (frame.flags & AIR_FLAG_REQUIRES_ACK) {
                            const uint8_t *cached_ack = NULL;
                            uint8_t cached_len = 0;

                            if (dedup_check(&s_dedup, frame.type, frame.seq,
                                            &cached_ack, &cached_len)) {
                                /* Retransmit the cached ACK/NACK */
                                uint8_t ack_buf[AIR_FRAME_MAX];
                                memcpy(ack_buf, cached_ack, cached_len);
                                transmit_and_wait(ack_buf, cached_len);
                                RadioLib_StartReceive(lora_cfg_active_preamble());
                                goto next_event;
                            }

                            /* Build and send fresh ACK (NACK for unknown type handled by comms) */
                            uint8_t ack_buf[8];
                            uint8_t ack_len = air_encode_ack(ack_buf,
                                                             comms_next_seq(),
                                                             frame.type,
                                                             frame.seq);
                            dedup_add(&s_dedup, frame.type, frame.seq,
                                      ack_buf, ack_len);
                            transmit_and_wait(ack_buf, ack_len);
                        }

                        /* Forward all non-ACK/NACK frames to comms */
                        if (frame.type != AIR_ACK && frame.type != AIR_NACK) {
                            RxAirFrame_t pkt;
                            pkt.air  = frame;
                            pkt.rssi = rssi;
                            pkt.snr  = snr;
                            xQueueSend(rx_q, &pkt, 0);
                        }
                    }
                }
            }
        }

next_event:
        if (notif & N_TRANSCEIVER_TX_READY_BIT) {
            TxQueueEntry_t entry;
            while (xQueueReceive(tx_q, &entry, 0) == pdTRUE) {
                if (!entry.needs_ack) {
                    if (cad_precheck() == CAD_RESULT_FREE) {
                        transmit_and_wait(entry.frame, entry.frame_len);
                    } else {
                        printf("TRX: CAD giveup (fire-and-forget)\r\n");
                    }
                } else {
                    saw_result_t r = saw_send(entry.frame, entry.frame_len,
                                              entry.frame[3], entry.frame[1],
                                              rx_q);
                    if (r != SAW_OK) {
                        printf("TRX: SAW result %d\r\n", (int)r);
                    }
                }
            }
        }

        /* §9.1: between RX/TX cycles, apply any pending LoRa config commit
         * or auto-revert. lora_cfg_tick() owns the SPI swap so we just
         * re-arm RX with the (potentially new) preamble. */
        (void)lora_cfg_tick();

        RadioLib_StartReceive(lora_cfg_active_preamble());
        health_kick(HEALTH_BIT_TRANSCEIVER);
    }
}
