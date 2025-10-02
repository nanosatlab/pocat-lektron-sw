// The ADCS software processes sensor data, executes algorithms to determine the satellite's 
// position, and issues commands to control and stabilize its orientation. This ensures that 
// the satellite can reliably carry out mission objectives such as precise data collection, 
// alignment with targets, and consistent communication.

/* ---- Includes ---- */
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "adcs.h"

/* ---- Macros and constants ---- */
#define ADCS_DETUMBLING_MODE (1 << 0)
#define ADCS_NADIR_POINTING_MODE (1 << 1)

/* ---- Type definitions ---- */
// ..

/* ---- Module-level variables ---- */
// ..

/* ---- Private function prototypes ---- */
static void setup_adcs(void);
static void process_adcs(void);
static uint32_t wait_for_notification(void);
static void detumble(void);
static void point_to_nadir(void);


/* ---- Public function definitions ---- */

void adcs_task(void *pv_parameters) {

}


/* ---- Private function definitions ---- */

static void setup_adcs(void) {
    // Apply the default configuration
}

static void process_adcs(void) {
    uint32_t notificationValue = wait_for_notification();

    if (notificationValue & ADCS_DETUMBLING_MODE) {
        detumble();
    }
    if (notificationValue & ADCS_NADIR_POINTING_MODE) {
        point_to_nadir();
    }
}


static void detumble(void) {

    // Don't exit function until finished 

}

static void point_to_nadir(void) {

    // Don't exit function until finished 

}

static uint32_t wait_for_notification(void) {

    uint32_t notificationValue;
    xTaskNotifyWait( 0,          // don’t clear on entry
                    0xFFFFFFFF,  // clear all bits on exit
                    &notificationValue,
                    portMAX_DELAY );
    return notificationValue;

}