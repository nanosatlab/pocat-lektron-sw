/**
 * @file comms.c
 * @brief Implementation of the communications task: protocol logic and packet processing.
 *
 * This task handles application-level operations:
 * - Receiving packets from transceiver_task via rx_queue
 * - Processing TCs with tc_process()
 * - Generating periodic beacons
 *
 * Link-layer concerns (ACK generation, ARQ retries) are handled
 * by transceiver_task.  This task does NOT touch RadioLib directly.
 */

/* ---- Includes ---- */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "comms.h"
#include "tc_handler.h"
#include "arq.h"
#include "health.h"
#include "notifications.h"
#include "beacon.h"
#include "task_management.h"
#include "types.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define COMMS_HEALTH_KICK_MS   2000u

static QueueHandle_t rx_queue = NULL;
static QueueHandle_t tx_queue = NULL;

static uint32_t deferred_notifications = 0;

static uint8_t s_seq = 0;

uint8_t comms_next_seq(void)
{
    return s_seq++;
}

void comms_init(void)
{
    /* Create the shared RX/TX queues here, during OBC setup, BEFORE any
     * subsystem task is created. transceiver_task caches these handles once at
     * startup, and comms and transceiver run at the same priority (4), so
     * either may start first. Creating the queues up front, like the OBDH
     * queue in setup_obc(), makes the handles valid regardless of task start
     * order. */
    rx_queue = xQueueCreate(COMMS_QUEUE_LEN, sizeof(RxAirFrame_t));
    tx_queue = xQueueCreate(COMMS_QUEUE_LEN, sizeof(TxQueueEntry_t));

    if (rx_queue == NULL || tx_queue == NULL) {
        printf("ERROR: Could not create comms queues\n");
        while (1);
    }
}

void comms_task(void *pv_parameters)
{
    (void)pv_parameters;

    /* rx_queue/tx_queue are created in comms_init() during OBC setup, before
     * any subsystem task runs, so they are guaranteed valid here. */
    deferred_notifications = 0;

    for (;;) {
        uint32_t notif = 0;
        xTaskNotifyWait(0, 0xFFFFFFFFu, &notif, 0);

        if (tm_check_pause(notif, &deferred_notifications)) {
            continue;
        }

        notif |= deferred_notifications;
        deferred_notifications = 0;

        if (notif & N_COMMS_NEW_CONFIG) {
            /* TODO: reload LoRa config from OBDH */
        }
        if (notif & N_COMMS_NEW_PARAMS) {
            /* TODO: reload comms params from OBDH */
        }
        if (notif & N_COMMS_STOP_RF) {
            /* TODO: gate the TX path */
        }
        if (notif & N_COMMS_RESUME_RF) {
            /* TODO: un-gate the TX path */
        }

        RxAirFrame_t pkt;
        if (xQueueReceive(rx_queue, &pkt, pdMS_TO_TICKS(COMMS_HEALTH_KICK_MS)) == pdTRUE) {
            /* ARQ frames (UL sessions) are handled by the ARQ engine; all
             * others dispatch through the TC handler. */
            air_type_t atype = (air_type_t)pkt.air.type;
            if (atype == AIR_DATA_BEGIN || atype == AIR_DATA || atype == AIR_DATA_END) {
                arq_rx(&pkt.air);
            } else {
                tc_process(&pkt.air);
            }
        }

        health_kick(HEALTH_BIT_COMMS);
    }
}

QueueHandle_t comms_get_rx_queue(void)
{
    return rx_queue;
}

QueueHandle_t comms_get_tx_queue(void)
{
    return tx_queue;
}
