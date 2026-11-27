/**
 * @file obdh.c
 * @author Medir Segura medir.segura@estudiantat.upc.edu
 * @brief Implementation of the OBDH task.
 *
 */

#include "obdh.h"
#include <stdbool.h>
#include <stdio.h>
#include "health.h"
#include <stdint.h>
#include <string.h>
#include "main.h"
#include "queue.h"
#include "flash.h"
#include "param_store.h"
#include "ht_store.h"
#include "notifications.h"
#include "task_management.h"

_Static_assert(HT_BEACON_SIZE <= OBDH_DATA_MAX, "a beacon must fit a queue entry");
_Static_assert(HT_BLOCK_MAX <= OBDH_DATA_MAX, "an HT block must fit an answer");

QueueHandle_t obdh_queue_handle;
QueueHandle_t obdh_answer_queues[OBDH_CLIENTS];
static uint32_t deferred_notifications;

/* The request being served and its answer: static, to keep 0.5 KB off the task's stack */
static obdh_request request;
static obdh_answer answer;

static void setup_obdh(void);
static void process_obdh(void);
static HAL_StatusTypeDef serve(const obdh_request *r, obdh_answer *a);


bool obdh_queues_create(void) {
    obdh_queue_handle = xQueueCreate(OBDH_QUEUE_LEN, sizeof(obdh_request));
    if (obdh_queue_handle == NULL)
        return false;

    for (uint32_t i = 0; i < OBDH_CLIENTS; i++) {
        obdh_answer_queues[i] = xQueueCreate(1, sizeof(obdh_answer));
        if (obdh_answer_queues[i] == NULL)
            return false;
    }
    return true;
}

void obdh_task(void *pv_parameters) {
    (void)pv_parameters;
    setup_obdh();

    for (;;) {
        /* process_obdh() blocks in xQueueReceive (up to 1 s), or while paused until the resume: that paces this loop */
        process_obdh();
        health_kick(HEALTH_BIT_OBDH);
    }

}

/**
 * @brief Initialize OBDH task state, every time the task starts (spec §5.2.1 rule 5).
 *
 * The health monitor may have deleted the previous OBDH task in the middle of
 * a flash operation, or between writing flash and updating RAM: the flash
 * interface is put back in a known state and the stores are loaded again from
 * flash before any request is served. The request it was serving gets no
 * answer: its caller's block time gives HAL_TIMEOUT, "unknown".
 */
static void setup_obdh(void) {
    deferred_notifications = 0;

    flash_interface_reset();
    param_store_load();
    ht_store_load();

    printf("Setting up OBDH...\n");
}

/**
 * @brief Execute one OBDH task processing cycle.
 *
 * Handles pause/resume notifications, receives one pending request or post
 * from the OBDH queue, performs it, and answers a request on the client's
 * answer queue; a post gets no answer. The answer never waits: it overwrites
 * the one-entry queue, and the client drops an answer whose number is not its
 * current request's.
 *
 * While paused, OBDH sleeps until its next notification instead of polling:
 * polling would keep it running at its priority, and no lower-priority task
 * could run, not even to acknowledge its own pause (spec §5.2.1 rule 7). A
 * notification other than the resume is kept for later, and OBDH sleeps again.
 */
static void process_obdh(void) {

    // Non-blocking poll: OBDH paces on its request queue (xQueueReceive) below.
    uint32_t notifications = wait_for_notification(0);

    // Paused: sleep until the resume, out of health monitoring meanwhile.
    while (tm_check_pause(notifications, &deferred_notifications))
        notifications = wait_for_notification(portMAX_DELAY);

    notifications |= deferred_notifications;
    deferred_notifications = 0;

    if (xQueueReceive(obdh_queue_handle, &request, pdMS_TO_TICKS(1000)) != pdPASS)
        return;

    answer.number = request.header.number;
    answer.len = 0u;
    answer.seq = 0u;
    answer.status = serve(&request, &answer);

    if ((request.header.op != HT_ADD_BEACON) && (request.header.client < OBDH_CLIENTS))
    {
        (void)xQueueOverwrite(obdh_answer_queues[request.header.client], &answer);
    }

}

/**
 * @brief Performs one request or post.
 * @param r The request; its data, for the operations that write.
 * @param a Receives what the operations that read return.
 * @return Status of the operation.
 */
static HAL_StatusTypeDef serve(const obdh_request *r, obdh_answer *a) {

    const obdh_request_header *h = &r->header;
    HAL_StatusTypeDef status = HAL_ERROR;

    switch (h->op)
    {
    case FLASH_PARAM_WRITE:
        status = param_store_write(h->param_id, r->data, h->len);
        break;

    case HT_ADD_BEACON:
        status = ht_store_add(r->data);
        break;

    case HT_READ_BLOCK:
        status = ht_store_read(h->seq, a->data, &a->len, &a->seq);
        break;

    default:
        break;
    }
    return status;
}
