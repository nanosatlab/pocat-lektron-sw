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
#include "task_management.h"
#include "clock.h"
#include "periph.h"

#include <stdio.h>

static uint32_t tasks_for_state(ObcState_t state);
static void change_state(ObcState_t *currentState, ObcState_t newState);

bool state_machine_boot(ObcState_t state)
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

void check_next_state(ObcState_t *currentState, uint32_t notificationValue)
{
    // ADD EPS LOGIC
    if (notificationValue & N_OBC_EXIT_STATE_TO_NOMINAL) {
        change_state(currentState, NOMINAL);
    }
    else if (notificationValue & N_OBC_EXIT_STATE_TO_CONTINGENCY) {
        change_state(currentState, CONTINGENCY);
    }
    else if (notificationValue & N_OBC_EXIT_STATE_TO_SUNSAFE) {
        change_state(currentState, SUNSAFE);
    }
    else if (notificationValue & N_OBC_EXIT_STATE_TO_SURVIVAL) {
        change_state(currentState, SURVIVAL);
    }
}

ClockFreq_t freq_for_state(ObcState_t state)
{
    switch (state) {
        case SUNSAFE:  return CLK_FREQ_8MHZ;
        case SURVIVAL: return CLK_FREQ_2MHZ;
        default:       return CLK_FREQ_80MHZ;
    }
}

/**
 * @brief Return the subsystem task mask that should run in a given OBC state.
 * @param state OBC operational state.
 * @return Bitmask of TM_TASK_* values for tasks that should be active.
 */
static uint32_t tasks_for_state(ObcState_t state)
{
    switch (state) {
        case NOMINAL:
            return TM_TASK_ALL;
        case CONTINGENCY:
            // COMMS beacon only, ADCS detumbling only
            return TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_ADCS | TM_TASK_OBDH
                 | TM_TASK_TRANSCEIVER | TM_TASK_BEACON;
        case SUNSAFE:
            // COMMS beacon only, ADCS idle
            return TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_OBDH
                 | TM_TASK_TRANSCEIVER | TM_TASK_BEACON;
        case SURVIVAL:
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
static void change_state(ObcState_t *currentState, ObcState_t newState)
{
    OBDH_Write_Request(PREVIOUS_STATE_ADDR, (uint8_t*)currentState, sizeof(ObcState_t));

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
    OBDH_Write_Request(CURRENT_STATE_ADDR, (uint8_t*)currentState, sizeof(ObcState_t));
}
