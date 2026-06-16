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
#include "flash.h"
#include "notifications.h"

//Variables que vaig fer servir per a la simulació, no verificats
#define OBDH_QUEUE_LEN 10
#define OBDH_ITEM_SIZE sizeof(obdh_request)

static void setup_obc(ObcState_t currentState);
static void process_obc(ObcState_t *currentState);

void obc_task(void *pv_parameters) {

    ObcState_t currentState = (ObcState_t)(uint32_t)pv_parameters;
    setup_obc(currentState);

    for (;;) {
       process_obc(&currentState);
       EventBits_t faults = health_check();
       if (faults != 0)
       {
           tm_handle_health_faults(faults);
       }
    }

}

/**
 * @brief Initialize OBC task state.
 *
 * Creates the OBDH request queue, initializes the health monitoring module,
 * registers the independent watchdog handle, configures the health check
 * period, and creates each subsystem tasks in resumed or paused state according 
 * to the current satellite operational mode.
 *
 * @param currentState Satellite state restored at boot.
 */
static void setup_obc(ObcState_t currentState) {

    // 1. Create queues
    obdh_queue_handle = xQueueCreate(OBDH_QUEUE_LEN, OBDH_ITEM_SIZE);

    if (obdh_queue_handle == NULL) {
        printf("ERROR: Could not create OBDH Queue\n");
        while(1);
    }

    health_init();
    health_register_iwdg(&hiwdg);
    health_config(pdMS_TO_TICKS(5000));

    // 2. Create subsystem tasks
    if (!state_machine_boot(currentState)) {
        printf("Error creating subsystem tasks\r\n");
    }
}


/**
 * @brief Execute one OBC task processing cycle.
 *
 * Reads pending OBC task notifications, processes them, and passes them to 
 * the state machine so it can evaluate possible state transitions.
 *
 * @param currentState Pointer to the current OBC state.
 */
static void process_obc(ObcState_t *currentState) {

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

    if (notificationValue & N_OBC_EPS_FAULT_DETECTED) {
        // Handle fault in Power Manager, eps has disabled charging and is saving the configuration to flash in parallel
    }

    if (notificationValue & N_OBC_EPS_ECLIPSE_START) {
        // Notification to let OBC know we are entering eclipse, eps is running and updating telemetry in parallel
    }

    if (notificationValue & N_OBC_EPS_ECLIPSE_END) {
        // Notification to let OBC know we have sunlight again
    }

    check_next_state(currentState, notificationValue);
}

