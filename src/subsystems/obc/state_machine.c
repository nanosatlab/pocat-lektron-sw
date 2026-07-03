/**
 * @file state_machine.c
 * @brief OBC operational state machine.
 *
 * Owns the state-to-frequency mapping and orchestrates safe clock/peripheral
 * transitions when the satellite changes operational state.
 */

#include "state_machine.h"
#include "notifications.h"
#include "FreeRTOS.h"
#include "task.h"
#include "flash.h"
#include "obdh_requests.h"
#include "task_management.h"
#include "clock.h"
#include "periph.h"

#include <stdio.h>

static uint32_t tasks_for_state(obc_state_t state);
static void change_state(obc_state_t *currentState, obc_state_t newState);

/* ---- Public API ---- */

bool state_machine_boot(obc_state_t state)
{
    uint32_t running = tasks_for_state(state);
    uint32_t paused  = TM_TASK_ALL & ~running;
    BaseType_t ok = tm_create_tasks(running, paused);

    if (ok != pdPASS) {
        printf("Error creating subsystem tasks\r\n");
        return false;
    }

    return true;
}

void check_next_state(obc_state_t *currentState, uint32_t notificationValue)
{
    // ADD EPS LOGIC
    if (notificationValue & N_OBC_EXIT_STATE_TO_OBC_STATE_NM) {
        change_state(currentState, OBC_STATE_NM);
    }
    else if (notificationValue & N_OBC_EXIT_STATE_TO_OBC_STATE_CM) {
        change_state(currentState, OBC_STATE_CM);
    }
    else if (notificationValue & N_OBC_EXIT_STATE_TO_OBC_STATE_SSM) {
        change_state(currentState, OBC_STATE_SSM);
    }
    else if (notificationValue & N_OBC_EXIT_STATE_TO_OBC_STATE_SM) {
        change_state(currentState, OBC_STATE_SM);
    }
}

/* ---- Private ---- */

ClockFreq_t freq_for_state(obc_state_t state)
{
    switch (state) {
        case OBC_STATE_SSM:  return CLK_FREQ_8MHZ;
        case OBC_STATE_SM: return CLK_FREQ_2MHZ;
        default:       return CLK_FREQ_80MHZ;
    }
}

/**
 * @brief Return the subsystem task mask that should run in a given OBC state.
 * @param state OBC operational state.
 * @return Bitmask of TM_TASK_* values for tasks that should be active.
 */
static uint32_t tasks_for_state(obc_state_t state)
{
    switch (state) {
        case OBC_STATE_NM:
            return TM_TASK_ALL;
        case OBC_STATE_CM:
            // COMMS beacon only, ADCS detumbling only
            return TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_ADCS | TM_TASK_OBDH
                 | TM_TASK_TRANSCEIVER | TM_TASK_BEACON;
        case OBC_STATE_SSM:
            // COMMS beacon only, ADCS idle
            return TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_OBDH
                 | TM_TASK_TRANSCEIVER | TM_TASK_BEACON;
        case OBC_STATE_SM:
            // COMMS RX only, ADCS idle
            return TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_OBDH
                 | TM_TASK_TRANSCEIVER;
        default:
            return TM_TASK_ALL;
    }
}

/**
 * @brief Transition from the current OBC state to a new state.
 *
 * Persists the previous state, pauses tasks from the old state, switches clock
 * and peripheral configuration if required, resumes tasks for the new state,
 * and persists the new current state.
 *
 * @param currentState Pointer to the current OBC state.
 * @param newState State to transition into.
 */
static void change_state(obc_state_t *currentState, obc_state_t newState)
{
    obdh_write_request(PREVIOUS_STATE_ADDR, (uint8_t*)currentState, sizeof(obc_state_t));

    uint32_t oldTasks = tasks_for_state(*currentState);
    uint32_t newTasks = tasks_for_state(newState);

    tm_pause_tasks(oldTasks);

    ClockFreq_t newFreq = freq_for_state(newState);
    if (newFreq != clock_get_current()) {
        vTaskSuspendAll(); //Revise
        clock_switch_to_freq(newFreq);
        periph_reconfigure_for_freq(newFreq);
        xTaskResumeAll(); //Revise
    }

    tm_resume_tasks(newTasks);

    *currentState = newState;
    obdh_write_request(CURRENT_STATE_ADDR, (uint8_t*)currentState, sizeof(obc_state_t));
}
