/**
 * @file obc.c
 * @author guillermo.o.tuama@estudiantat.upc.edu
 * @brief OBC Task serves as the central scheduler, coordinating the operation of all other tasks. 
    It is responsible for managing transitions between different operational modes, task scheduling, 
    power control, and essential satellite checkups.
 * @version 0.1
 * @date 2026-01-20
 * 
 * @copyright Copyright (c) 2026
 * 
 */

/* ---- Includes ---- */
#include <stdint.h> //mirar
#include "obc.h"
#include "task_management.h"
#include "state_machine.h"
#include "FreeRTOS.h" // mirar
#include "task.h" // mirar
#include "main.h"
#include "eps.h"
#include "comms.h"
#include "obdh.h"
#include "payload.h"
#include "health.h"
#include "log.h"
#include "flash.h"
#include "notifications.h"
#include "time.h"

/* ---- Macros and constants ---- */
//Variables que vaig fer servir per a la simulació, no verificats
#define OBDH_QUEUE_LEN 10
#define OBDH_ITEM_SIZE sizeof(obdh_request)


/* ---- Private function prototypes ---- */

static void setup_obc(obc_state_t currentState);
static void process_obc(obc_state_t *currentState);
static uint32_t process_obc_notifications(void);

/* ---- Public function definitions ---- */

void obc_task(void *pv_parameters) {

    obc_state_t currentState = (obc_state_t)(uint32_t)pv_parameters;
    setup_obc(currentState);

    for (;;) {
       process_obc(&currentState);
       EventBits_t faults = health_check();
       if (faults != 0)
       {
           tm_handle_health_faults(faults);
       }
       vTaskDelay(pdMS_TO_TICKS(2000)); // Delay to prevent busy looping, adjust as needed  
    }

}

/* ---- Private function defisnitions ---- */

static void setup_obc(obc_state_t currentState) {

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

    // 3. Persist the boot time so the beacon can report uptime as (now - boot).
    uint32_t boot_time = time_get_unix();
    OBDH_Write_Request(BOOT_TIME_ADDR, (const uint8_t *)&boot_time, sizeof(boot_time));
}


static void process_obc(obc_state_t *currentState) {

    // Process notifications:
    uint32_t notificationValue = process_obc_notifications();

    check_next_state(currentState, notificationValue);

    vTaskDelay(pdMS_TO_TICKS(100)); // Delay to prevent busy looping, adjust as needed

}

static uint32_t process_obc_notifications(void) {

    uint32_t notificationValue = 0;
    xTaskNotifyWait( 0,          // don't clear on entry
                    0xFFFFFFFF, // clear all bits on exit
                    &notificationValue,
                    0);         // don't block, just check if there's a notification);

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

    // ... handle other notifications as needed
    return notificationValue;
}

// REVISAR!!
    // The obc task / manager task is the only one that is in charge of changing satellite modes
    // 1. suspends or resumes the other tasks
    // 2. checks the manager queue and looks for notifications/events for changing 
    //    the mode of the satellite or reloading the default configuration
    // 3. evaluates the variable CURRENT SATELLITE STATUS and checks the flags that
    //    are set to 1, it analyses them and decides whether or not needs to perform a transit of mode
    // Can't 2 not be merged into three? Or the other way around?
