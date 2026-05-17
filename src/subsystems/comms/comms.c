#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "comms.h"
#include "tc_handler.h"
#include "health.h"
#include "notifications.h"
#include "beacon.h"
#include "task_management.h"
#include <stdbool.h>
#include <string.h>

#define COMMS_HEALTH_KICK_MS   2000u

static QueueHandle_t rx_queue = NULL;
static QueueHandle_t tx_queue = NULL;

static uint32_t deferred_notifications = 0;

static uint8_t s_seq = 0;

uint8_t comms_next_seq(void)
{
    return s_seq++;
}

void comms_task(void *pv_parameters)
{
    (void)pv_parameters;

    rx_queue = xQueueCreate(COMMS_QUEUE_LEN, sizeof(RxAirFrame_t));
    tx_queue = xQueueCreate(COMMS_QUEUE_LEN, sizeof(TxQueueEntry_t));

    if (rx_queue == NULL || tx_queue == NULL) {
        return;
    }

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
            tc_process(&pkt.air);
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
