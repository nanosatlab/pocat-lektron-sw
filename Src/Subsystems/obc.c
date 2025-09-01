// OBC Task serves as the central scheduler, coordinating the operation of all other tasks. 
// It is responsible for managing transitions between different operational modes, task scheduling, 
// power control, and essential satellite checkups.

/* ---- Includes ---- */
#include "obc.h"
#include "FreeRTOS.h"
#include "main.h"
#include "eps.h"

/* ---- Macros and constants ---- */
#define COMMS_STACK_SIZE 3000
#define COMMS_PRIORITY 1

/* ---- Type definitions ---- */
typedef enum {
    NOMINAL,
} ObcState_t;

/* ---- Module-level variables ---- */
static TaskHandle_t eps_task_handle;
static TaskHandle_t comms_task_handle;
// ...

/* ---- Private function prototypes ---- */
static void setup_obc(void);
static void process_obc(void);
static void create_queues(void);
static void create_tasks(void);
static void suspend_and_resume_tasks_depending_on_state(ObcState_t *currentState);

// a considerar/eliminar:
static ObcState_t currentState;

/* ---- Public function definitions ---- */

void obc_task(void *pv_parameters) {

    setup_obc();

    for (;;) {
        process_obc(&currentState);
    }

}

/* ---- Private function definitions ---- */

static void setup_obc(void) {

    printf("Setting up OBC...\n");
    // 1. Create queues
    create_queues();

    // 2. Create tasks
    create_tasks();

}

static void create_tasks(void) {

    xTaskCreate(payload_task, "PAYLOAD", PAYLOAD_STACK_SIZE, NULL, PAYLOAD_PRIORITY, &payload_task_handle);
    xTaskCreate(eps_task, "EPS", EPS_STACK_SIZE, NULL, EPS_PRIORITY, &eps_task_handle);
    xTaskCreate(comms_task, "COMMS", COMMS_STACK_SIZE, NULL, COMMS_PRIORITY, &comms_task_handle);
    xTaskCreate(obdh_task, "OBDH", OBDH_STACK_SIZE, NULL, OBDH_PRIORITY, &obdh_task_handle);
    // ..
    
}

static static void process_obc(ObcState_t *currentState) {

    printf("Processing OBC...\n");

    suspend_and_resume_tasks_depending_on_state(currentState);

    // Finish implementing... Need to know how the rest of the tasks work...

    check_notifications();

    change_state_if_needed();

}

static void suspend_and_resume_tasks_depending_on_state(ObcState_t *currentState) {

    switch (*currentState) {

        case NOMINAL:
            // Suspend or resume tasks as needed for the NOMINAL state
            // vTaskSuspend(task_handle) ....
            // vTaskResume(task_handle) ....
            break;

        default:
            printf("Unknown state\n");
            break;

    }

}

static void obc_does_nominal(void) {
    // 
}

// implemented until here at the moment:

ObcState_t Nominal(void) {
    // to do:
    //  frequency tratment for state
    for(;;)
	{
        uint32_t notificationValue = waitForNotification();

        if ( notificationValue & OBC_PHOTO_CAPTURE ) handlePayloadCapture();
        // ... handle other events
	}
}

uint32_t waitForNotification(void) {
    uint32_t notificationValue;
    xTaskNotifyWait( 0,          // don’t clear on entry
                    0xFFFFFFFF, // clear all bits on exit
                    &notificationValue,
                    portMAX_DELAY );
    return notificationValue;
}

void handlePayloadCapture(void) {
    xTaskNotify(xPayloadTaskHandle,
            PAYLOAD_PHOTO_CAPTURE,
            eSetBits);
    // ...
}

// REVISAR!!
    // The obc task / manager task is the only one that is in charge of changing satellite modes
    // 1. suspends or resumes the other tasks
    // 2. checks the manager queue and looks for notifications/events for changing 
    //    the mode of the satellite or reloading the default configuration
    // 3. evaluates the variable CURRENT SATELLITE STATUS and checks the flags that
    //    are set to 1, it analyses them and decides whether or not needs to perform a transit of mode
    // Can't 2 not be merged into three? Or the other way around?