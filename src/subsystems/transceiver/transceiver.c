/**
 * @file transceiver.c
 * @brief Interrupt-driven RF transceiver task with link-layer ACK and ARQ.
 *
 * Responsibilities:
 * - RX: receives packets via DIO1 interrupt, deinterleaves, auto-ACKs,
 *   and forwards data packets to comms_task via rx_queue.
 * - TX: drains tx_queue, interleaves and transmits.  For packets that
 *   require acknowledgement (needs_ack), performs inline ARQ with
 *   configurable retries and timeout.
 * - ACK matching: incoming ACK packets are consumed here and never
 *   forwarded to comms_task.
 */

/* ---- Includes ---- */

#include "FreeRTOS.h"
#include "task.h"
#include "transceiver.h"
#include "comms.h"
#include "radiolib_wrapper.h"
#include "health.h"
#include "interleaving.h"
#include "notifications.h"
#include "task_management.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ---- Macros ---- */

#define RF_FREQUENCY           868000000
#define TX_OUTPUT_POWER        18
#define LORA_BANDWIDTH         0
#define LORA_IQ_INVERSION      0
#define LORA_PREAMBLE_LENGTH   8
#define TX_DONE_TIMEOUT_MS     5000
#define TX_POST_TX_GUARD_MS    0

/* ---- Module-level variables ---- */

/* ---- Private helpers ---- */

/**
 * @brief Transmit a raw buffer and block until TX_DONE or timeout.
 */
static void transmit_and_wait(uint8_t *buf, uint8_t len)
{
    RadioLib_Standby();
    RadioLib_StartTransmit(buf, len);
    xTaskNotifyWait(0, N_TRANSCEIVER_RADIO_IRQ_BIT, NULL,
                    pdMS_TO_TICKS(TX_DONE_TIMEOUT_MS));
    RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
}

/**
 * @brief Build a link-layer ACK for an incoming packet and transmit it.
 *
 * The ACK mirrors the mission/PQ IDs and the TC ID of the received packet.
 */
static void send_link_ack(uint8_t tc_id)
{
    uint8_t ack_buf[COMMS_PKT_SIZE] = {0};
    ack_buf[0] = COMMS_MISSION_ID;
    ack_buf[1] = COMMS_PQ_ID;
    ack_buf[2] = tc_id;
    ack_buf[5] = COMMS_ACK_TYPE;

    Interleave(ack_buf, COMMS_PKT_SIZE);
    transmit_and_wait(ack_buf, COMMS_PKT_SIZE);
}

/**
 * @brief Wait for an ACK after transmitting a data packet (inline ARQ).
 *
 * Enters RX and waits up to ACK_TIMEOUT_MS for a packet whose
 * data[5] == COMMS_ACK_TYPE.  Non-ACK packets received during the
 * wait are forwarded to comms_task via rx_queue.
 *
 * @return 1 if ACK received, 0 if timeout.
 */
static int wait_for_ack(QueueHandle_t rx_q)
{
    RadioLib_StartReceive(LORA_PREAMBLE_LENGTH);

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ACK_TIMEOUT_MS);

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t remaining = (deadline > now) ? (deadline - now) : 0;
        if (remaining == 0) {
            return 0;
        }

        uint32_t irq_notif = 0;
        BaseType_t got = xTaskNotifyWait(0, N_TRANSCEIVER_RADIO_IRQ_BIT,
                                         &irq_notif, remaining);
        if (got != pdTRUE) {
            return 0; /* timeout */
        }

        if (!(irq_notif & N_TRANSCEIVER_RADIO_IRQ_BIT)) {
            continue;
        }

        uint32_t irq = RadioLib_GetIrqFlags();
        RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);

        if (!(irq & RADIOLIB_SX126X_IRQ_RX_DONE)) {
            continue;
        }

        RxPacket_t pkt = {0};
        int16_t st = RadioLib_ReadRxData(pkt.data, COMMS_PKT_SIZE,
                                         &pkt.length, &pkt.rssi, &pkt.snr);
        if (st != RADIOLIB_ERR_NONE || pkt.length == 0) {
            RadioLib_StartReceive(LORA_PREAMBLE_LENGTH);
            continue;
        }

        Deinterleave(pkt.data, pkt.length);

        if (pkt.data[5] == COMMS_ACK_TYPE) {
            return 1; /* ACK received */
        }

        /* Non-ACK packet during ARQ wait — forward to comms */
        xQueueSend(rx_q, &pkt, 0);
        RadioLib_StartReceive(LORA_PREAMBLE_LENGTH);
    }
}

/* ---- Public function definitions ---- */

void transceiver_task(void *pv_parameters)
{
    (void)pv_parameters;

    QueueHandle_t rx_q = comms_get_rx_queue();
    QueueHandle_t tx_q = comms_get_tx_queue();

    /* Initialise and configure the radio hardware */
    if (RadioLib_Init() != 0) {
        printf("TRX: Radio init failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    RadioLib_SetChannel(RF_FREQUENCY);

    RadioLib_SetTxConfig(
        8,                    /* SF */
        1,                    /* CR 4/5 */
        TX_OUTPUT_POWER,
        LORA_BANDWIDTH,
        LORA_IQ_INVERSION,
        1,                    /* CRC on */
        LORA_PREAMBLE_LENGTH);

    RadioLib_SetRxConfig(
        8,
        1,
        LORA_BANDWIDTH,
        LORA_IQ_INVERSION,
        1,
        LORA_PREAMBLE_LENGTH);

    /* Register this task for DIO1 notifications */
    RadioLib_SetIrqTask(xTaskGetCurrentTaskHandle());

    /* Enter RX mode immediately */
    RadioLib_StartReceive(LORA_PREAMBLE_LENGTH);

    for (;;) {
        uint32_t notif = 0;
        xTaskNotifyWait(0,
                        N_TRANSCEIVER_RADIO_IRQ_BIT | N_TRANSCEIVER_TX_READY_BIT |
                        N_TASK_PAUSE | N_TASK_RESUME,
                        &notif, portMAX_DELAY);

        if (tm_check_pause(notif, NULL))
            continue;

        /* ---- Handle radio IRQ (RX_DONE) ---- */
        if (notif & N_TRANSCEIVER_RADIO_IRQ_BIT) {
            uint32_t irq = RadioLib_GetIrqFlags();
            RadioLib_ClearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);

            if (irq & RADIOLIB_SX126X_IRQ_RX_DONE) {
                RxPacket_t pkt = {0};
                int16_t st = RadioLib_ReadRxData(pkt.data, COMMS_PKT_SIZE,
                                                 &pkt.length, &pkt.rssi, &pkt.snr);
                if (st == RADIOLIB_ERR_NONE && pkt.length > 0) {
                    Deinterleave(pkt.data, pkt.length);

                    if (pkt.data[5] == COMMS_ACK_TYPE) {
                        /* Incoming ACK — consumed here, not forwarded */
                    } else {
                        /* Data packet: auto-ACK then forward to comms */
                        send_link_ack(pkt.data[2]);
                        xQueueSend(rx_q, &pkt, 0);
                    }
                }
            }
        }

        /* ---- Handle TX ready (packet available in tx_queue) ---- */
        if (notif & N_TRANSCEIVER_TX_READY_BIT) {
            TxQueueEntry_t entry;
            while (xQueueReceive(tx_q, &entry, 0) == pdTRUE) {
                uint8_t tx_buf[COMMS_PKT_SIZE];
                memcpy(tx_buf, entry.data, entry.length);
                Interleave(tx_buf, entry.length);

                if (!entry.needs_ack) {
                    /* Fire-and-forget (beacons, etc.) */
                    transmit_and_wait(tx_buf, entry.length);
                } else {
                    /* ARQ: send and wait for ACK, retry on timeout */
                    uint8_t retries = 0;
                    int acked = 0;

                    while (!acked && retries <= ARQ_MAX_RETRIES) {
                        transmit_and_wait(tx_buf, entry.length);
                        acked = wait_for_ack(rx_q);

                        if (!acked) {
                            retries++;
                            if (retries <= ARQ_MAX_RETRIES) {
                                printf("TRX: retry %u/%u\r\n", retries, ARQ_MAX_RETRIES);
                            }
                        }
                    }

                    if (!acked) {
                        printf("TRX: ARQ gave up after %u retries\r\n", ARQ_MAX_RETRIES);
                    }
                }

                vTaskDelay(pdMS_TO_TICKS(TX_POST_TX_GUARD_MS));
            }
        }

        /* Back to RX after handling everything */
        RadioLib_StartReceive(LORA_PREAMBLE_LENGTH);
        health_kick(HEALTH_BIT_TRANSCEIVER);
    }
}
