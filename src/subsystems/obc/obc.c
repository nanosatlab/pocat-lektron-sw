/**
 * @file obc.c
 * @author guillermo.o.tuama@estudiantat.upc.edu
 * @brief Implementation of the OBC task.
 * @details 
 * OBC Task serves as the central scheduler, coordinating the operation of all other tasks. 
 * It is responsible for managing transitions between different operational modes, task scheduling, 
 * power control, and essential satellite checkups.
 * @date 2026-01-20
 * 
 */

#include <stdint.h>
#include "obc.h"
#include "task_management.h"
#include "state_machine.h"
#include "FreeRTOS.h" 
#include "task.h" 
#include "main.h"
#include "eps.h"
#include "comms.h"
#include "obdh.h"
#include "payload.h"
#include "health.h"
#include "log.h"
#include "notifications.h"

/* ---- Private function prototypes ---- */

static void setup_obc(void);
static void process_obc(void);
static uint32_t process_obc_notifications(void);

/* ---- Public function definitions ---- */

void obc_task(void *pv_parameters) {

    (void)pv_parameters;
    setup_obc();

    for (;;) {
       process_obc();
       EventBits_t faults = health_check();
       if (faults != 0)
       {
           tm_handle_health_faults(faults);
       }
    }

}

/* ---- Private function definitions ---- */

/**
 * @brief Initialize OBC task state.
 *
 * Creates the OBDH request and answer queues, initializes the health monitoring module,
 * registers the independent watchdog handle, configures the health check
 * period, and starts the state machine in Init, which creates each
 * subsystem task resumed or paused as Init's first step needs.
 *
 * Nothing is written to flash at boot: the uptime comes from the tick
 * counter (uptime.h).
 */
static void setup_obc(void) {

    // 1. Create queues
    if (!obdh_queues_create()) {
        printf("ERROR: Could not create OBDH Queue\n");
        while(1);
    }

    health_init();
    health_register_iwdg(&hiwdg);
    health_config(pdMS_TO_TICKS(HEALTH_PERIOD_MS));

    // 2. Create subsystem tasks
    if (!state_machine_boot()) {
        printf("Error creating subsystem tasks\r\n");
    }
}


/**
 * @brief Execute one OBC task processing cycle.
 *
 * Reads pending OBC task notifications, processes them, and passes them to
 * the state machine so it can evaluate possible state transitions.
 */
static void process_obc(void) {

    // Process notifications:
    uint32_t notificationValue = wait_for_notification(pdMS_TO_TICKS(2000));

    if (notificationValue == N_OBC_EXIT_STATE_GROUP_MASK) {
        // Notification to change state, but we will check the exact state in the state machine function
    }
    if (notificationValue & N_OBC_UPDATE_TIME) {
        // printf("Updating system time\r\n");
        // Handle time update, e.g., read new time from OBDH or TC and set RTC
    }
    if (notificationValue & N_OBC_HARD_REBOOT) {
        // Handle hard reboot, e.g., trigger a watchdog reset or perform necessary cleanup before rebooting
    }
    if (notificationValue & N_OBC_SOFT_REBOOT) {
        // Handle soft reboot, e.g., reset tasks and reinitialize subsystems without clearing flash
    }

    check_next_state(notificationValue);
}

