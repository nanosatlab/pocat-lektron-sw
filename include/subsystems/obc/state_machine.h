/**
 * @file state_machine.h
 * @brief OBC operational state machine.
 * @details
 * Every boot starts in Init (SPF §1.5, §1.6.1), at 2 MHz, which goes through
 * three steps:
 * - standby: 45 minutes without radio emissions or operations; only EPS and
 *   OBDH run;
 * - deployment of the COMMS antenna; ADCS runs too;
 * - first contact: beacons and telecommands; everything but the payload runs.
 *
 * Two parameters are the activation history: PARAM_STANDBY_DONE, stored
 * when the standby ends, skips the standby, and PARAM_DEPLOY_COMMS, set once
 * the ground confirms the deployment, skips the deployment. Only the ground's
 * TC_PING at the first-contact step ends Init, for NOMINAL; a lower-power
 * state requested during Init follows at once.
 *
 * The state machine owns the current state, and only the OBC task changes it
 * (obc_is_caller()): it starts it with state_machine_boot() and passes it
 * every notification it receives through check_next_state(). Any task reads
 * the state with state_machine_current_state(). Nothing is stored at a state
 * change.
 * @note Reworked in 2026-11: every boot starts in Init; the state is no longer stored.
 */

#ifndef INC_STATE_MACHINE_H_
#define INC_STATE_MACHINE_H_

#include <stdint.h>
#include <stdbool.h>
#include "clock.h"

/* ---------- OBC state enum (§6.6) ---------- */
typedef enum {
    OBC_STATE_INIT        = 0x00,
    OBC_STATE_NOMINAL     = 0x01,
    OBC_STATE_CONTINGENCY = 0x02,
    OBC_STATE_SUNSAFE     = 0x03,
    OBC_STATE_SURVIVAL    = 0x04,
    OBC_STATE_SAFE_HOLD   = 0x05,
} obc_state_t;

/**
 * @brief Return the clock frequency associated with a given state.
 * @param state Operational state.
 * @return Corresponding ClockFreq_t value.
 */
ClockFreq_t freq_for_state(obc_state_t state);

/**
 * @brief Starts Init at its first step the activation history does not skip, and creates the subsystem tasks, running or paused as that step needs. OBC task only.
 * @details The clock is already the one of Init: main() sets it before the scheduler starts.
 * @return true on success; false if the caller is not the OBC task (nothing done) or task creation failed.
 */
bool state_machine_boot(void); // Todo: change name to something more descriptive or maybe simply create tasks in obc task? Revise.

/**
 * @brief Runs one OBC cycle of the state machine: Init's steps, or a change to the state the notifications request. OBC task only.
 * @details
 * - Any other caller changes nothing.
 * - In Init: moves to the next step when its time comes. N_OBC_PING_RECEIVED
 *   ends Init, for NOMINAL, at the first-contact step only; a PING before it
 *   is dropped. A request for a lower-power state (CONTINGENCY, SUNSAFE or
 *   SURVIVAL) is kept, and carried out right after Init ends. Any other
 *   state request is dropped.
 * - After Init, a request for the current state changes nothing.
 * - A change is refused when OBDH does not acknowledge the pause: the state
 *   stays, the failure is counted, and the state machine keeps the request.
 *   The next call, at the OBC's next cycle, takes it as notified again.
 * @param notificationValue Notification bits from xTaskNotifyWait.
 */
void check_next_state(uint32_t notificationValue);

/**
 * @brief Returns the current state. Any task, any time (plain function).
 */
obc_state_t state_machine_current_state(void);

/**
 * @brief Returns how many state changes were refused since boot because OBDH did not acknowledge the pause.
 */
uint32_t state_machine_change_failures(void);

#endif /* INC_STATE_MACHINE_H_ */
