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
#include "health.h"
#include "notifications.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "interleaving.h"
#include "beacon.h"
#include "notifications.h"
#include "task_management.h"


/* ---- Macros and constants ---- */

#define COMMS_QUEUE_LEN        8
#define COMMS_HEALTH_KICK_MS   2000  /* must be < health_config period (5000 ms) */

/* ---- Module-level variables ---- */

static QueueHandle_t rx_queue = NULL;
static QueueHandle_t tx_queue = NULL;

static uint32_t deferred_notifications;

/* ---- Public function definitions ---- */


void comms_task(void *pv_parameters)
{
    (void)pv_parameters;

    /* Create FreeRTOS queues */
    rx_queue = xQueueCreate(COMMS_QUEUE_LEN, sizeof(RxPacket_t));
    tx_queue = xQueueCreate(COMMS_QUEUE_LEN, sizeof(TxQueueEntry_t));

    if (rx_queue == NULL || tx_queue == NULL) {
        printf("COMMS: Queue creation failed\r\n");
        return;
    }
    // Apply the default configuration
    deferred_notifications = 0;

    /* Main loop */
    for (;;) {
        /* Check for config/control notifications (non-blocking) */
        uint32_t notif = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notif, 0);

        if (tm_check_pause(notif, &deferred_notifications))
            continue;

        notif |= deferred_notifications;
        deferred_notifications = 0;
        if (notif & N_COMMS_NEW_CONFIG) {
            /* TODO: reload config from OBDH */
        }
        if (notif & N_COMMS_NEW_PARAMS) {
            /* TODO: reload params from OBDH */
        }
        if (notif & N_COMMS_STOP_RF) {
            /* TODO: stop RF operations */
        }
        if (notif & N_COMMS_RESUME_RF) {
            /* TODO: resume RF operations */
        }

        /* Wait for RX packet (beacon is handled independently by beacon_task).
         * Timeout is bounded so health_kick fires even when the link is idle. */
        RxPacket_t pkt;
        if (xQueueReceive(rx_queue, &pkt, pdMS_TO_TICKS(COMMS_HEALTH_KICK_MS)) == pdTRUE) {
            tc_process(pkt.data);
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
