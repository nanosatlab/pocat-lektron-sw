/**
 * @file adcs.c 
 * @brief Implementation of the ADCS task.
 * @date 2026-01-20
 * 
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "adcs.h"
#include "health.h"
#include "notifications.h"
#include "task_management.h"

#define ADCS_DETUMBLING_MODE (1 << 0)
#define ADCS_NADIR_POINTING_MODE (1 << 1)

/** @brief Notification bits deferred while the ADCS task is paused. */
static uint32_t deferred_notifications;

static void setup_adcs(void);
static void process_adcs(void);
static void detumble(void);
static void point_to_nadir(void);

void adcs_task(void *pv_parameters) {
    (void)pv_parameters;
    setup_adcs();
    for (;;) {
        process_adcs();
        health_kick(HEALTH_BIT_ADCS);
    }
}

/**
 * @brief Initialize ADCS task state.
 */
static void setup_adcs(void) {
    // Apply the default configuration
    deferred_notifications = 0;
}

/**
 * @brief Execute one ADCS task processing cycle. 
 *
 * Handles the task pause/resume protocol and processes pending ADCS 
 * task notifications.
 */
static void process_adcs(void) {

    uint32_t notificationValue = wait_for_notification(pdMS_TO_TICKS(1000));

    if (tm_check_pause(notificationValue, &deferred_notifications))
        return;

    notificationValue |= deferred_notifications;
    deferred_notifications = 0;

    if (notificationValue & ADCS_DETUMBLING_MODE) {
        detumble();
    }

    if (notificationValue & ADCS_NADIR_POINTING_MODE) {
        point_to_nadir();
    }

}

/**
 * @brief Execute detumbling mode.
 * @todo Implement detumbling control logic.
 */
static void detumble(void) {

    // Don't exit function until finished 
    return;

}

/**
 * @brief Execute nadir-pointing mode.
 * @todo Implement nadir-pointing control logic.
 */
static void point_to_nadir(void) {

    // Don't exit function until finished 

}
