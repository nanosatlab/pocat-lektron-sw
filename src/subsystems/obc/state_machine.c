/**
 * @file state_machine.c
 * @brief OBC operational state machine.
 *
 * Owns the current state, Init's steps and the state-to-frequency mapping,
 * and orchestrates safe clock/peripheral transitions when the satellite
 * changes operational state.
 */

#include "state_machine.h"
#include "obc.h"
#include "notifications.h"
#include "FreeRTOS.h"
#include "task.h"
#include "param_store.h"
#include "obdh_requests.h"
#include "task_management.h"
#include "clock.h"
#include "periph.h"
#include "uptime.h"

#include <stdio.h>

#define INIT_STANDBY_S  (45u * 60u)   /**< Init's standby after power-on, in seconds: no radio emissions, operations or deployments (SPF §1.5). */

/** @brief Requests for a lower-power state: during Init they wait for its end. */
#define LOWER_POWER_REQUESTS (N_OBC_EXIT_STATE_TO_OBC_STATE_CONTINGENCY | \
                              N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL)

/** @brief Init's steps, in order. */
typedef enum {
    INIT_STEP_STANDBY,         /**< No radio emissions or operations until INIT_STANDBY_S after power-on. */
    INIT_STEP_DEPLOY_COMMS,    /**< Deploys the COMMS antenna. */
    INIT_STEP_FIRST_CONTACT,   /**< Beacons, and waits for the ground's TC_PING. */
} init_step_t;

static void run_init(uint32_t requests);
static void change_to_requested_state(uint32_t requests);
static init_step_t first_init_step_from(init_step_t step);
static bool activation_done(param_id_t flag);
static void enter_init_step(init_step_t step);
static void store_standby_done(void);
static void deploy_comms_antenna(void);
static uint32_t tasks_for_init_step(init_step_t step);
static uint32_t tasks_for_state(obc_state_t state);
static bool change_state(obc_state_t newState);

static obc_state_t current_state = OBC_STATE_INIT;   /**< Written by the OBC task only; one memory access, so any task reads it whole. */
static init_step_t init_step = INIT_STEP_STANDBY;     /**< Init's current step; unused after Init. */
static uint32_t pending_request;   /**< Requests kept for the next call: a refused change or end of Init, and the lower-power requests made during Init; 0 if none. */
static uint32_t change_failures;   /**< State changes refused because OBDH did not acknowledge the pause. */

/* ---- Public API ---- */

ClockFreq_t freq_for_state(obc_state_t state)
{
    switch (state) {
        case OBC_STATE_INIT:     return CLK_FREQ_2MHZ;
        case OBC_STATE_SURVIVAL: return CLK_FREQ_2MHZ;
        default:                 return CLK_FREQ_80MHZ;
    }
}

bool state_machine_boot(void)
{
    if (!obc_is_caller()) {
        return false;   /* another task: no task created, nothing changed */
    }

    current_state = OBC_STATE_INIT;
    init_step = first_init_step_from(INIT_STEP_STANDBY);
    printf("Init: step %d\r\n", (int)init_step);

    uint32_t running = tasks_for_state(current_state);
    uint32_t paused  = TM_TASK_ALL & ~running;
    BaseType_t ok = tm_create_tasks(running, paused);

    if (ok != pdPASS) {
        printf("Error creating subsystem tasks\r\n");
        return false;
    }

    return true;
}

/**
 * @todo add eps logic
 */
void check_next_state(uint32_t notificationValue)
{
    if (!obc_is_caller()) {
        return;   /* another task: nothing changed */
    }

    /* A request kept from an earlier call counts as notified again */
    uint32_t requests = notificationValue | pending_request;
    pending_request = 0u;

    if (current_state == OBC_STATE_INIT) {
        run_init(requests);
    } else {
        change_to_requested_state(requests);
    }
}

obc_state_t state_machine_current_state(void)
{
    return current_state;
}

uint32_t state_machine_change_failures(void)
{
    return change_failures;
}

/* ---- Private ---- */

/**
 * @brief One OBC cycle in Init: moves to the next step when its time comes, and ends Init on the ground's PING at the first-contact step.
 * @param requests Notification bits, with the requests kept from earlier calls.
 */
static void run_init(uint32_t requests)
{
    uint32_t lowerPower = requests & LOWER_POWER_REQUESTS;

    switch (init_step) {
        case INIT_STEP_STANDBY:
            if (uptime_get_s() >= INIT_STANDBY_S) {
                store_standby_done();
                enter_init_step(first_init_step_from(INIT_STEP_DEPLOY_COMMS));
            }
            break;

        case INIT_STEP_DEPLOY_COMMS:
            deploy_comms_antenna();
            enter_init_step(INIT_STEP_FIRST_CONTACT);
            break;

        case INIT_STEP_FIRST_CONTACT:
            /* TODO: after 72 h without a telecommand, reboot and deploy again (SPF §1.6.3), once the
               deployment owner has decided what a reset does before the ground confirms the deployment */
            if ((requests & N_OBC_PING_RECEIVED) != 0u) {
                if (!change_state(OBC_STATE_NOMINAL)) {
                    pending_request = N_OBC_PING_RECEIVED | lowerPower;   /* refused: tried again at the OBC's next cycle */
                    return;
                }
                /* Low battery: from NOMINAL at once to the lower-power state requested during Init (SPF §1.6.1) */
                change_to_requested_state(lowerPower);
                return;
            }
            break;
    }

    pending_request = lowerPower;   /* kept until Init ends */
}

/**
 * @brief Changes to the state @p requests ask for: the first of NOMINAL, CONTINGENCY and SURVIVAL requested.
 * @details A request for the current state changes nothing. A refused change
 *          keeps its request for the next call.
 * @param requests Notification bits, with the requests kept from earlier calls.
 */
static void change_to_requested_state(uint32_t requests)
{
    uint32_t request = 0u;
    obc_state_t newState = current_state;

    if (requests & N_OBC_EXIT_STATE_TO_OBC_STATE_NOMINAL) {
        request = N_OBC_EXIT_STATE_TO_OBC_STATE_NOMINAL;
        newState = OBC_STATE_NOMINAL;
    }
    else if (requests & N_OBC_EXIT_STATE_TO_OBC_STATE_CONTINGENCY) {
        request = N_OBC_EXIT_STATE_TO_OBC_STATE_CONTINGENCY;
        newState = OBC_STATE_CONTINGENCY;
    }
    else if (requests & N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL) {
        request = N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL;
        newState = OBC_STATE_SURVIVAL;
    }

    /* Nothing requested, or the current state: nothing changes */
    if (newState == current_state) {
        return;
    }

    if (!change_state(newState)) {
        pending_request = request;   /* refused: tried again at the OBC's next cycle */
    }
}

/**
 * @brief Returns the first step from @p step on that the activation history does not skip.
 * @details PARAM_STANDBY_DONE skips the standby and PARAM_DEPLOY_COMMS the
 *          deployment. The first-contact step is never skipped.
 */
static init_step_t first_init_step_from(init_step_t step)
{
    if ((step == INIT_STEP_STANDBY) && activation_done(PARAM_STANDBY_DONE)) {
        step = INIT_STEP_DEPLOY_COMMS;
    }
    if ((step == INIT_STEP_DEPLOY_COMMS) && activation_done(PARAM_DEPLOY_COMMS)) {
        step = INIT_STEP_FIRST_CONTACT;
    }
    return step;
}

/**
 * @brief Whether an activation flag marks its step done: its value is 1. Any other value repeats the step.
 */
static bool activation_done(param_id_t flag)
{
    uint8_t value = 0u;
    return (param_get(flag, &value, sizeof(value)) == HAL_OK) && (value == 1u);
}

/**
 * @brief Moves Init to a later step and resumes the tasks it adds. The clock and the other tasks stay.
 */
static void enter_init_step(init_step_t step)
{
    uint32_t added = tasks_for_init_step(step) & ~tasks_for_init_step(init_step);

    init_step = step;
    printf("Init: step %d\r\n", (int)step);
    (void)tm_resume_tasks(added);
}

/**
 * @brief Stores PARAM_STANDBY_DONE. If the store does not take it, the next boot waits the standby again.
 */
static void store_standby_done(void)
{
    const uint8_t done = 1u;
    HAL_StatusTypeDef status = param_set_request(PARAM_STANDBY_DONE, &done, sizeof(done), OBDH_REQUEST_WAIT);
    if (status != HAL_OK) {
        printf("Standby done not stored: status %d\r\n", (int)status);
    }
}

/**
 * @brief Deploys the COMMS antenna.
 * @todo Burn the thermal knife (SPF §3.9.2). The pin, the burn time, the
 *       battery check and any repeat are for the deployment owner to define.
 *       Until the ground confirms the deployment (PARAM_DEPLOY_COMMS), every
 *       reset runs this step again.
 */
static void deploy_comms_antenna(void)
{
}

/**
 * @brief Return the subsystem task mask that should run at a given Init step.
 * @param step Init step.
 * @return Bitmask of TM_TASK_* values for tasks that should be active.
 */
static uint32_t tasks_for_init_step(init_step_t step)
{
    switch (step) {
        case INIT_STEP_STANDBY:
            // No radio emissions, no operations
            return TM_TASK_EPS | TM_TASK_OBDH;
        case INIT_STEP_DEPLOY_COMMS:
            // ADCS detumbling; still no radio emissions
            return TM_TASK_EPS | TM_TASK_OBDH | TM_TASK_ADCS;
        default:
            // First contact: beacons and telecommands, no payload
            return TM_TASK_EPS | TM_TASK_OBDH | TM_TASK_ADCS
                 | TM_TASK_COMMS | TM_TASK_TRANSCEIVER | TM_TASK_BEACON;
    }
}

/**
 * @brief Return the subsystem task mask that should run in a given OBC state.
 * @details OBDH runs in every state and at every Init step.
 * @param state OBC operational state.
 * @return Bitmask of TM_TASK_* values for tasks that should be active.
 */
static uint32_t tasks_for_state(obc_state_t state)
{
    switch (state) {
        case OBC_STATE_INIT:
            return tasks_for_init_step(init_step);
        case OBC_STATE_NOMINAL:
            return TM_TASK_ALL;
        case OBC_STATE_CONTINGENCY:
            // COMMS beacon only, ADCS detumbling only
            return TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_ADCS | TM_TASK_OBDH
                 | TM_TASK_TRANSCEIVER | TM_TASK_BEACON;
        case OBC_STATE_SURVIVAL:
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
 * Pauses tasks from the old state. No flash operation may run during the
 * clock switch, so without OBDH's acknowledgement of the pause nothing
 * changes: the tasks are resumed, the failure is counted and the state
 * stays. Otherwise it switches clock and peripheral configuration if
 * required, makes the new state current and resumes its tasks.
 *
 * @param newState State to transition into; not the current one.
 * @return true if the state changed; false if OBDH did not acknowledge the pause.
 */
static bool change_state(obc_state_t newState)
{
    uint32_t oldTasks = tasks_for_state(current_state);
    uint32_t newTasks = tasks_for_state(newState);

    uint32_t notPaused = tm_pause_tasks(oldTasks);
    if ((notPaused & TM_TASK_OBDH) != 0u) {
        (void)tm_resume_tasks(oldTasks);
        change_failures++;
        printf("State change to %d refused: OBDH did not acknowledge the pause\r\n", (int)newState);
        return false;
    }

    ClockFreq_t newFreq = freq_for_state(newState);
    if (newFreq != clock_get_current()) {
        vTaskSuspendAll(); //Revise
        clock_switch_to_freq(newFreq);
        periph_reconfigure_for_freq(newFreq);
        xTaskResumeAll(); //Revise
    }

    current_state = newState;
    (void)tm_resume_tasks(newTasks);
    return true;
}
