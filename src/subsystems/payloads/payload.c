/**
 * @file payload.c
 * @brief Implementation of the Payload task.
 * 
 */

#include <stdbool.h>
#include "FreeRTOS.h"
#include "main.h"
#include "health.h"
#include "notifications.h"
#include "task_management.h"
#include "log.h"

/** @brief Notification bits deferred while the Payload task is paused. */
static uint32_t deferred_notifications;

static void setup_payload(void);
static void process_payload(void);

void payload_task(void *pv_parameters) {
    (void)pv_parameters;
    setup_payload();

    for (;;) {
        process_payload();
        health_kick(HEALTH_BIT_PAYLOAD);
    }
    
}

/**
 * @brief Initialize Payload task state.
 */
static void setup_payload(void) {

    // Apply default configuration
    // ...
    deferred_notifications = 0;

}

/**
 * @brief Execute one payload task processing cycle. 
 *
 * Handles the task pause/resume protocol and processes pending payload 
 * task notifications.
 */
static void process_payload(void) {

    uint32_t notificationValue = wait_for_notification(pdMS_TO_TICKS(1000));

    if (tm_check_pause(notificationValue, &deferred_notifications))
        return;

    notificationValue |= deferred_notifications;
    deferred_notifications = 0;
    // if (notificationValue & PAYLOAD_PHOTO_CAPTURE) {
    //     capture_photo();
    // }
    // if ... (not else if!!)

}
