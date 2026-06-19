/**
 * @file state_machine.h
 * @brief OBC operational state machine.
 */

#ifndef INC_STATE_MACHINE_H_
#define INC_STATE_MACHINE_H_

#include <stdint.h>
#include <stdbool.h>
#include "clock.h"

/* ---------- OBC state enum (§6.6) ---------- */
typedef enum {
    OBC_STATE_BOOT      = 0x00,
    OBC_STATE_NM        = 0x01,
    OBC_STATE_CM        = 0x02,
    OBC_STATE_SSM       = 0x03,
    OBC_STATE_SM        = 0x04,
    OBC_STATE_SAFE_HOLD = 0x05,
} obc_state_t;

/**
 * @brief Return the clock frequency associated with a given state.
 * @param state Operational state.
 * @return Corresponding ClockFreq_t value.
 */
ClockFreq_t freq_for_state(obc_state_t state);

/**
 * @brief Create subsystem tasks with the correct running/paused configuration for the given state.
 * @param state Boot state read from flash.
 * @return true on success, false if task creation failed.
 */
bool state_machine_boot(obc_state_t state); // Todo: change name to something more descriptive or maybe simply create tasks in obc task? Revise.

/**
 * @brief Evaluate pending notifications and determine the next OBC state. If a state transition is needed, perform necessary actions (e.g., pausing/resuming tasks, reconfiguring subsystems) and update currentState.
 * @param currentState Pointer to the current operational state.
 * @param notificationValue Notification bits from xTaskNotifyWait.
 */
void check_next_state(obc_state_t *currentState, uint32_t notificationValue);

#endif /* INC_STATE_MACHINE_H_ */
