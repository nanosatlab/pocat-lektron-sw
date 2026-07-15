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
#include "notifications.h"
#include "task_management.h"

QueueHandle_t obdh_queue_handle;
static uint32_t deferred_notifications;

static void setup_obdh(void);
static void process_obdh(void);


void obdh_task(void *pv_parameters) {
    (void)pv_parameters;
    setup_obdh();

    for (;;) {
        /* process_obdh() blocks in xQueueReceive (up to 1 s), which paces this loop */
        process_obdh();
        health_kick(HEALTH_BIT_OBDH);
    }

}

/**
 * @brief Initialize OBDH task state.
 */
static void setup_obdh(void) {
    deferred_notifications = 0;

    printf("Setting up OBDH...\n");
}

/**
 * @brief Execute one OBDH task processing cycle.
 *
 * Handles pause/resume notifications, receives one pending flash request from
 * the OBDH queue, performs the requested read or write operation, stores the
 * operation status in the request result pointer, and notifies the requesting
 * task when the operation is complete.
 */
static void process_obdh(void) {

    uint32_t notifications = 0;
    // Non-blocking poll: OBDH paces on its request queue (xQueueReceive) below.
    notifications = wait_for_notification(0);

    if (tm_check_pause(notifications, &deferred_notifications))
        return;

    notifications |= deferred_notifications;
    deferred_notifications = 0;
    
    obdh_request request;
    if (xQueueReceive(obdh_queue_handle, &request, pdMS_TO_TICKS(1000)) != pdPASS)
        return;

    HAL_StatusTypeDef status = HAL_ERROR;

    if (request.buf.src != NULL)
    {
        switch (request.op)
        {
        case FLASH_READ:
            flash_read(request.addr, request.buf.dst, request.len);
            status = HAL_OK;
            break;

        case FLASH_WRITE:
            status = flash_write(request.addr, request.buf.src, request.len);
            break;

        case FLASH_PROGRAM:
            status = flash_program(request.addr, request.buf.src, request.len);
            break;

        case FLASH_ERASE_PROGRAM:
            status = flash_erase_page(request.addr);
            if (status == HAL_OK)
            {
                status = flash_program(request.addr, request.buf.src, request.len);
            }
            break;

        default:
            break;
        }
    }

    if (request.client != NULL)
    {
        xTaskNotifyIndexed(request.client, OBDH_NOTIFY_IDX,
                           (uint32_t)status, eSetValueWithOverwrite);
    }

}
