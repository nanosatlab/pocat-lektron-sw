#include "saw.h"
#include "timing.h"
#include "frame.h"
#include "cad.h"
#include "radiolib_wrapper.h"
#include "notifications.h"
#include "comms.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdint.h>

#define TX_DONE_TIMEOUT_MS  5000u

saw_result_t saw_send(const uint8_t *frame, uint8_t frame_len,
                      uint8_t own_seq, uint8_t own_type,
                      QueueHandle_t rx_q)
{
    (void)own_type;

    if (cad_precheck() == CAD_RESULT_GIVEUP) {
        return SAW_CAD_FAIL;
    }

    uint8_t tmp_frame[AIR_FRAME_MAX];
    memcpy(tmp_frame, frame, frame_len);

    uint32_t timeout_ms = ack_timeout_ms(frame_len);

    for (uint8_t attempt = 0; attempt <= SAW_MAX_RETX; attempt++) {
        /* Mark retransmissions */
        if (attempt > 0) {
            tmp_frame[2] |= AIR_FLAG_IS_RETX;
        }

        RadioLib_Standby();
        /* For attempt 0: discard stale CAD_DONE notification from cad_precheck.
         * Must be before StartTransmit to avoid racing with TX_DONE. */
        xTaskNotifyStateClear(NULL);
        RadioLib_StartTransmit(tmp_frame, frame_len);
        xTaskNotifyWait(0, N_TRANSCEIVER_RADIO_IRQ_BIT, NULL,
                        pdMS_TO_TICKS(TX_DONE_TIMEOUT_MS));
        RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);

        RadioLib_StartReceive(LORA_PRE_DEFAULT);

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

        for (;;) {
            TickType_t now = xTaskGetTickCount();
            TickType_t remaining = (deadline > now) ? (deadline - now) : 0u;
            if (remaining == 0u) {
                break;
            }

            uint32_t irq_notif = 0;
            BaseType_t got = xTaskNotifyWait(0, N_TRANSCEIVER_RADIO_IRQ_BIT,
                                             &irq_notif, remaining);
            if (got != pdTRUE) {
                break;
            }

            if (!(irq_notif & N_TRANSCEIVER_RADIO_IRQ_BIT)) {
                continue;
            }

            uint32_t irq = RadioLib_GetIrqFlags();
            RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);

            if (!(irq & RADIOLIB_SX126X_IRQ_RX_DONE)) {
                continue;
            }

            uint8_t raw_buf[AIR_FRAME_MAX];
            uint16_t raw_len = 0;
            int16_t rssi = 0;
            int8_t  snr  = 0;
            int16_t st = RadioLib_ReadRxData(raw_buf, AIR_FRAME_MAX, &raw_len, &rssi, &snr);
            if (st != RADIOLIB_ERR_NONE || raw_len == 0) {
                RadioLib_StartReceive(LORA_PRE_DEFAULT);
                continue;
            }

            AirFrame_t decoded;
            if (air_decode(raw_buf, (uint8_t)raw_len, &decoded) != 0) {
                RadioLib_StartReceive(LORA_PRE_DEFAULT);
                continue;
            }

            if (decoded.type == AIR_ACK && decoded.len >= 2 &&
                decoded.payload[1] == own_seq) {
                return SAW_OK;
            }

            if (decoded.type == AIR_NACK && decoded.len >= 2 &&
                decoded.payload[1] == own_seq) {
                return SAW_NACKED;
            }

            /* Non-matching frame — forward to comms and keep waiting */
            if (rx_q != NULL) {
                RxAirFrame_t pkt;
                pkt.air  = decoded;
                pkt.rssi = rssi;
                pkt.snr  = snr;
                xQueueSend(rx_q, &pkt, 0);
            }
            RadioLib_StartReceive(LORA_PRE_DEFAULT);
        }
        /* Timeout on this attempt — loop for retransmit if attempts remain */
    }

    return SAW_TIMEOUT;
}
