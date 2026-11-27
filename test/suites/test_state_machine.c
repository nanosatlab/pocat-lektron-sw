/**
 * @file test_state_machine.c
 * @brief Host tests for the OBC operational state machine: Init and its steps, the end of Init, transitions between the other states, and the caller check.
 * @date 2026-10-23
 * @note Reworked in 2026-11: every boot starts in Init; the state is no longer stored.
 *
 * @details
 * **What is under test**
 * - src/subsystems/obc/state_machine.c:
 *   - `freq_for_state()`
 *   - `state_machine_boot()` and `state_machine_current_state()`
 *   - `check_next_state()`: Init's steps, the end of Init, and through it the
 *     private `change_state()`.
 * - `obc_is_caller()` in include/subsystems/obc/obc.h.
 *
 * **Init** (SPF §1.5, §1.6.1; this is what the tests check)
 * - Every boot starts in Init, at 2 MHz (main() sets the clock). Its steps:
 *   - standby: EPS and OBDH run, nothing receives or emits;
 *   - deployment of the COMMS antenna: ADCS runs too;
 *   - first contact: COMMS, the transceiver and the beacon run too.
 *   The payload stays paused, and the clock never changes.
 * - The boot skips the standby when PARAM_STANDBY_DONE is 1, and the
 *   deployment when PARAM_DEPLOY_COMMS is 1. Nothing is written at boot.
 * - The standby ends 45 minutes after power-on and stores PARAM_STANDBY_DONE.
 * - Only N_OBC_PING_RECEIVED at the first-contact step ends Init, for
 *   NOMINAL; a PING before it is dropped. A lower-power request made during
 *   Init (EPS's, on a low battery) is carried out right after; any other
 *   state request in Init is dropped.
 *
 * **What `check_next_state()` does after Init**
 * - A call from any task but the OBC changes nothing, and so does a
 *   `state_machine_boot()` from any task but the OBC (`obc_is_caller()`).
 * - A request for the current state changes nothing.
 * - Otherwise `change_state()`, in order:
 *   1. Pauses every task of the old state (`tm_pause_tasks`).
 *   2. If OBDH did not acknowledge the pause, no flash operation is known to
 *      be stopped, so nothing changes: it resumes those tasks, counts the
 *      failure, and `check_next_state()` keeps the request, which it tries
 *      again at its next call, the OBC's next cycle.
 *   3. If the new state needs a different clock: suspends the scheduler,
 *      switches the clock, reconfigures the peripherals, and resumes the
 *      scheduler.
 *   4. Makes the new state current and resumes every task of the new state
 *      (`tm_resume_tasks`). Nothing is stored.
 *
 * Every test also checks, through fake hooks, that the clock never switches
 * while OBDH runs, and that no OBDH request is sent while OBDH is paused.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below: `state_machine.c`, `task_management.c` and `health.c`.
 *   The real task manager runs underneath the state machine, so the
 *   pause/resume handshakes are real and not stubbed.
 * - SOURCES:
 *   - `notifications.c`, `stub_task_entries.c` and `sim_tasks.c`;
 *   - `fake_clock.c`, which counts clock switches and peripheral
 *     reconfigurations and starts at 80 MHz;
 *   - `uptime.c`, over the fake tick;
 *   - `fake_obdh_requests.c`, which serves the parameter writes at once and logs
 *     every call: each runs the real `param_store_write()` as the OBDH task
 *     of the task table, the only task the store accepts;
 *   - the real `param_store.c` (with `crc32.c`) over `fake_flash_driver.c`.
 *
 * **Fake setup**
 * - The test task plays the OBC task: `main_get_obc_handle()` returns it.
 * - Block hook = `sim_tasks_run_pending()`, so every task answers PAUSE and
 *   RESUME immediately.
 * - The tick starts at 0, the power-on.
 * - The store's flash starts erased, so every parameter has its default.
 *   `reboot()` rebuilds the store from flash alone, as main() does at boot.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_state_machine
 * docker run --rm -v "${PWD}:/app" pocat-tests test_state_machine -f NOMINAL_to_SURVIVAL
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_clock.h"
#include "fake_obdh_requests.h"
#include "fake_flash_driver.h"
#include "sim_tasks.h"
#include "stub_task_entries.h"
#include "pocat_test_main.h"

#include "state_machine.c"
#include "task_management.c"
#include "health.c"

/** @name Tasks that run in each state and at each Init step
 *  Copied from `tasks_for_state()` and `tasks_for_init_step()` in
 *  state_machine.c. If those functions change on purpose, update these sets too.
 *  @{ */
#define SET_INIT_STANDBY       (TM_TASK_EPS | TM_TASK_OBDH)                                                   /**< Init's standby: nothing receives or emits. */
#define SET_INIT_DEPLOY_COMMS  (TM_TASK_EPS | TM_TASK_OBDH | TM_TASK_ADCS)                                    /**< Init's deployment step: ADCS too. */
#define SET_INIT_FIRST_CONTACT (TM_TASK_ALL & ~TM_TASK_PAYLOAD)                                               /**< Init's first contact: everything but the payload. */
#define SET_NOMINAL            (TM_TASK_ALL)                                                                  /**< Nominal mode: everything runs. */
#define SET_CONTINGENCY        (TM_TASK_ALL & ~TM_TASK_PAYLOAD)                                               /**< Contingency mode: payload off. */
#define SET_SURVIVAL           (TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_OBDH | TM_TASK_TRANSCEIVER)            /**< Survival mode: payload, ADCS and beacon off. */
/** @} */

#define RADIO_TASKS (TM_TASK_COMMS | TM_TASK_TRANSCEIVER | TM_TASK_BEACON)   /**< The tasks that receive or emit. */

static int switches_with_obdh_running;   /**< Clock switches made while OBDH was not paused; must stay 0. */
static int requests_with_obdh_paused;    /**< OBDH requests sent while OBDH was paused; must stay 0. */

/** @brief True while OBDH is paused, as the task manager sees it. */
static bool obdh_paused(void)
{
    return task_table[__builtin_ctz(TM_TASK_OBDH)].paused;
}

/** @brief Fake clock switch hook: counts a switch made while OBDH runs. */
static void check_obdh_paused_at_switch(void)
{
    if (!obdh_paused()) {
        switches_with_obdh_running++;
    }
}

/** @brief Fake OBDH request hook: counts a request sent while OBDH is paused, which would wait for nothing. */
static void check_obdh_running_at_request(void)
{
    if (obdh_paused()) {
        requests_with_obdh_paused++;
    }
}

static void reboot(void);

/**
 * @brief Runs before every test: fresh fakes, erased flash, a reset task manager, the store at its defaults, and the test task as the OBC task.
 * @details Resets every fake and every static of the included modules, for
 * the same reason as in test_task_management.c: stale handles would point at
 * tasks that `fake_rtos_reset()` has freed, and a request a test left kept
 * would be tried in the next one. Then it installs the
 * `sim_tasks_run_pending()` block hook and the two OBDH checks.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    fake_clock_reset();
    fake_obdh_reset();
    fake_flashdrv_reset();
    sim_tasks_reset();
    reboot();

    for (size_t i = 0; i < TASK_TABLE_SIZE; i++) {
        task_table[i].handle = NULL;
        task_table[i].paused = false;
    }
    task_event_group_handle = NULL;

    health_eg = NULL;
    expected_bits = 0;
    period_ticks = 0;
    next_deadline = 0;
    iwdg_handle = NULL;
    health_init();

    current_state = OBC_STATE_INIT;
    init_step = INIT_STEP_STANDBY;
    pending_request = 0u;
    stub_set_obc_handle(fake_rtos_test_task());

    fake_rtos_set_block_hook(sim_tasks_run_pending);
    switches_with_obdh_running = 0;
    fake_clock_set_switch_hook(check_obdh_paused_at_switch);
    requests_with_obdh_paused = 0;
    fake_obdh_set_request_hook(check_obdh_running_at_request);
}

/**
 * @brief Runs after every test: checks that the code left the RTOS balanced and kept OBDH safe.
 * @details `change_state()` suspends the scheduler around the clock switch.
 * If it forgot to resume it, the satellite would freeze. A clock switch while
 * OBDH runs could cut a flash operation short, and a request sent while
 * OBDH is paused could only time out.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, switches_with_obdh_running, "the clock switched while OBDH was running");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, requests_with_obdh_paused, "an OBDH request was sent while OBDH was paused");
}

/**
 * @brief Helper: returns a bitmask of the tasks currently marked paused.
 * @return `TM_TASK_*` bits, one per paused entry of `task_table[]`.
 */
static uint32_t paused_mask(void)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < TASK_TABLE_SIZE; i++) {
        if (task_table[i].paused) m |= (1u << i);
    }
    return m;
}

/** @brief Helper: returns the TM_TASK_* bits of the tasks of @p mask that were ever asked to resume. */
static uint32_t resumed_among(uint32_t mask)
{
    uint32_t resumed = 0u;
    for (uint32_t i = 0; i < TASK_TABLE_SIZE; i++) {
        uint32_t bit = 1u << i;
        if (((mask & bit) != 0u) && ((fake_rtos_bits_sent_to(tm_get_task_handle(bit)) & N_TASK_RESUME) != 0u)) {
            resumed |= bit;
        }
    }
    return resumed;
}

/** @brief Helper: counts the state changes tried since the test began; each one first asks OBDH to pause. */
static int changes_tried(void)
{
    TaskHandle_t obdh = tm_get_task_handle(TM_TASK_OBDH);
    int n = 0;
    for (int i = 0; i < fake_rtos_notify_log_count(); i++) {
        fake_notify_rec_t r = fake_rtos_notify_log_at(i);
        if ((r.target == obdh) && ((r.value & N_TASK_PAUSE) != 0u)) n++;
    }
    return n;
}

/** @brief Helper: rebuilds the parameter store from flash alone, as main() does at boot, before the scheduler starts. */
static void reboot(void)
{
    fake_rtos_set_scheduler_state(taskSCHEDULER_NOT_STARTED);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_load());
    fake_rtos_set_scheduler_state(taskSCHEDULER_RUNNING);
}

/** @brief Helper: stores a parameter outside the code under test, before the scheduler starts, where the store needs no OBDH task. */
static void preset_param(param_id_t id, const uint8_t *value, size_t size)
{
    fake_rtos_set_scheduler_state(taskSCHEDULER_NOT_STARTED);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_write((uint8_t)id, value, size));
    fake_rtos_set_scheduler_state(taskSCHEDULER_RUNNING);
}

/** @brief Helper: marks an activation step done, as an earlier boot would have: the flag holds 1. Call reboot() after. */
static void preset_done(param_id_t flag)
{
    const uint8_t done = 1u;
    preset_param(flag, &done, sizeof(done));
}

/** @brief Helper: returns the value of a one-byte parameter. */
static uint8_t flag_value(param_id_t flag)
{
    uint8_t value = 0xFFu;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(flag, &value, sizeof(value)));
    return value;
}

/** @brief Helper: counts the OBDH requests that wrote parameter @p id. */
static int param_writes(param_id_t id)
{
    int n = 0;
    for (int i = 0; i < fake_obdh_call_count(); i++) {
        fake_obdh_call_t c = fake_obdh_call_at(i);
        if (c.id == (uint8_t)id) n++;
    }
    return n;
}

/** @brief Helper: sets the time since power-on, in whole seconds. */
static void set_uptime_s(uint32_t seconds)
{
    fake_rtos_set_tick((TickType_t)(seconds * 1000u));
}

/**
 * @brief Helper: boots as main.c → obc_task does: the clock of Init, then `state_machine_boot()`, and checks that boot worked.
 * @details The step boot starts at depends on the activation flags in the store: preset them and reboot() first.
 */
static void boot(void)
{
    fake_clock_set_current(freq_for_state(OBC_STATE_INIT));
    TEST_ASSERT_TRUE(state_machine_boot());
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());
    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());
}

/** @brief Helper: boots with both activation steps done, so Init starts at first contact, and checks it. */
static void boot_at_first_contact(void)
{
    preset_done(PARAM_STANDBY_DONE);
    preset_done(PARAM_DEPLOY_COMMS);
    reboot();
    boot();
    TEST_ASSERT_EQUAL_INT(INIT_STEP_FIRST_CONTACT, init_step);
}

/**
 * @brief Helper: puts the state machine in a state after Init, with its tasks and its clock, as if it had got there through Init.
 * @details Every boot is in Init; the tests of the other states start from them directly.
 * @param state A state after Init.
 */
static void start_in(obc_state_t state)
{
    current_state = state;
    uint32_t running = tasks_for_state(state);
    TEST_ASSERT_EQUAL_INT(pdPASS, tm_create_tasks(running, TM_TASK_ALL & ~running));
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());
    fake_clock_set_current(freq_for_state(state));
}

/* =========================================================================
 * 1. State → clock frequency, and the boot in Init
 * ========================================================================= */

/**
 * @brief Each state maps to the documented clock frequency.
 * @details INIT and SURVIVAL run at 2 MHz to save power (SPF Table 3.19).
 * NOMINAL, CONTINGENCY and SAFE_HOLD run at 80 MHz. `main.c`
 * boots at INIT's frequency, before FreeRTOS starts, so a wrong value would
 * boot the satellite at the wrong speed.
 */
void test_freq_for_state_mapping(void)
{
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ,  freq_for_state(OBC_STATE_INIT));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, freq_for_state(OBC_STATE_NOMINAL));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, freq_for_state(OBC_STATE_CONTINGENCY));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ,  freq_for_state(OBC_STATE_SURVIVAL));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, freq_for_state(OBC_STATE_SAFE_HOLD));
}

/**
 * @brief A first boot, with no activation history, starts Init at the standby: only EPS and OBDH run.
 * @details Blank flash, so both flags have their default, 0. All 7 tasks are
 * created; the others are created paused, so only EPS and OBDH are expected
 * to report health. The boot switches no clock and writes nothing.
 */
void test_boot_without_activation_history_starts_in_standby(void)
{
    boot();

    TEST_ASSERT_EQUAL_INT(INIT_STEP_STANDBY, init_step);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_INIT_STANDBY, paused_mask());
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS | HEALTH_BIT_OBDH, health_get_expected());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
}

/**
 * @brief With the standby done, the boot starts Init at the deployment: ADCS runs too, still nothing receives or emits.
 */
void test_boot_with_standby_done_starts_at_the_deployment(void)
{
    preset_done(PARAM_STANDBY_DONE);
    reboot();

    boot();

    TEST_ASSERT_EQUAL_INT(INIT_STEP_DEPLOY_COMMS, init_step);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_INIT_DEPLOY_COMMS, paused_mask());
}

/**
 * @brief The deployment flag alone does not skip the standby, and the standby's end then skips the deployment.
 * @details Each flag skips its own step. Here the standby was never
 * completed (its flag is 0), so the satellite keeps the 45 minutes of radio
 * silence; when they end it goes straight to first contact.
 */
void test_deploy_flag_alone_does_not_skip_the_standby(void)
{
    preset_done(PARAM_DEPLOY_COMMS);
    reboot();
    boot();
    TEST_ASSERT_EQUAL_INT(INIT_STEP_STANDBY, init_step);

    set_uptime_s(INIT_STANDBY_S);
    check_next_state(0);

    TEST_ASSERT_EQUAL_INT(INIT_STEP_FIRST_CONTACT, init_step);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD, paused_mask());
}

/**
 * @brief Only the value 1 marks a step done: any other flag value repeats the step.
 * @details A value of 2 in PARAM_STANDBY_DONE is not "done", so the satellite
 * keeps the standby rather than break the radio silence.
 */
void test_a_flag_other_than_1_does_not_skip_its_step(void)
{
    const uint8_t two = 2u;
    preset_param(PARAM_STANDBY_DONE, &two, sizeof(two));
    reboot();

    boot();

    TEST_ASSERT_EQUAL_INT(INIT_STEP_STANDBY, init_step);
}

/* =========================================================================
 * 2. Init's steps
 * ========================================================================= */

/**
 * @brief The standby lasts 45 minutes from power-on, then stores PARAM_STANDBY_DONE and moves to the deployment.
 * @details
 * - At 44 min 59 s: still the standby, nothing written, nothing resumed.
 * - At 45 min: one write of PARAM_STANDBY_DONE, which now holds 1; the step
 *   is the deployment, and only ADCS was resumed. The radio tasks stay
 *   paused, and the clock never changed.
 */
void test_standby_lasts_45_minutes_and_is_stored(void)
{
    boot();

    set_uptime_s(INIT_STANDBY_S - 1u);
    check_next_state(0);
    TEST_ASSERT_EQUAL_INT(INIT_STEP_STANDBY, init_step);
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_HEX32(0u, resumed_among(TM_TASK_ALL));

    set_uptime_s(INIT_STANDBY_S);
    check_next_state(0);

    TEST_ASSERT_EQUAL_INT(INIT_STEP_DEPLOY_COMMS, init_step);
    TEST_ASSERT_EQUAL_INT(1, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(1, param_writes(PARAM_STANDBY_DONE));
    TEST_ASSERT_EQUAL_UINT8(1u, flag_value(PARAM_STANDBY_DONE));
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ADCS, resumed_among(TM_TASK_ALL));
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_INIT_DEPLOY_COMMS, paused_mask());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
}

/**
 * @brief The deployment step moves to first contact at the next cycle: the radio tasks start, and nothing is written.
 * @details PARAM_DEPLOY_COMMS is the ground's confirmation of the deployment,
 * so the step itself does not set it. Until the radio tasks are resumed here,
 * none of them was ever asked to resume.
 */
void test_deployment_step_leads_to_first_contact(void)
{
    preset_done(PARAM_STANDBY_DONE);
    reboot();
    boot();
    TEST_ASSERT_EQUAL_HEX32(0u, resumed_among(RADIO_TASKS));

    check_next_state(0);

    TEST_ASSERT_EQUAL_INT(INIT_STEP_FIRST_CONTACT, init_step);
    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());
    TEST_ASSERT_EQUAL_HEX32(RADIO_TASKS, resumed_among(TM_TASK_ALL));
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD, paused_mask());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_UINT8(0u, flag_value(PARAM_DEPLOY_COMMS));
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
}

/**
 * @brief A standby the store cannot record still ends, and the next boot waits it again.
 * @details OBDH does not answer the write in time (HAL_TIMEOUT). The 45 minutes
 * have passed in this boot, so Init moves on; after a reset the flag is still
 * 0, so the boot would start at the standby again.
 */
void test_standby_not_stored_still_ends(void)
{
    boot();
    fake_obdh_inject_error(HAL_TIMEOUT);

    set_uptime_s(INIT_STANDBY_S);
    check_next_state(0);

    TEST_ASSERT_EQUAL_INT(INIT_STEP_DEPLOY_COMMS, init_step);
    TEST_ASSERT_EQUAL_INT(1, param_writes(PARAM_STANDBY_DONE));
    reboot();
    TEST_ASSERT_EQUAL_UINT8(0u, flag_value(PARAM_STANDBY_DONE));
}

/* =========================================================================
 * 3. The end of Init
 * ========================================================================= */

/**
 * @brief A reboot with both activation steps done waits at first contact, at 2 MHz, until the ground's PING.
 * @details
 * - Arrange: both flags hold 1.
 * - Act: boot, then OBC cycles with no notification over two hours.
 * - Expected: Init at first contact from the boot on, everything but the
 *   payload running, the clock at 2 MHz and never switched, nothing written.
 *   Then the PING ends Init, for NOMINAL at 80 MHz.
 */
void test_reboot_with_both_flags_waits_for_PING_at_first_contact(void)
{
    boot_at_first_contact();
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, clock_get_current());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD, paused_mask());

    for (uint32_t minute = 0u; minute <= 120u; minute += 10u) {
        set_uptime_s(minute * 60u);
        check_next_state(0);
    }

    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(INIT_STEP_FIRST_CONTACT, init_step);
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, clock_get_current());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());

    check_next_state(N_OBC_PING_RECEIVED);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, clock_get_current());
}

/**
 * @brief The PING at first contact ends Init in NOMINAL: one clock switch to 80 MHz, every task running, nothing written.
 * @details The switch happens with the scheduler suspended and OBDH paused
 * (checked in tearDown()). The payload, paused since the boot, now runs, and
 * all 7 tasks are expected to report health.
 */
void test_PING_at_first_contact_ends_Init_in_NOMINAL(void)
{
    boot_at_first_contact();

    check_next_state(N_OBC_PING_RECEIVED);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, fake_clock_last_switch_freq());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_last_switch_suspend_depth());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_reconfigure_count());
    TEST_ASSERT_EQUAL_HEX32(0u, paused_mask());
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_PAYLOAD | HEALTH_BIT_EPS | HEALTH_BIT_COMMS | HEALTH_BIT_ADCS |
                            HEALTH_BIT_OBDH | HEALTH_BIT_TRANSCEIVER | HEALTH_BIT_BEACON, health_get_expected());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
}

/**
 * @brief A PING before the first-contact step is dropped: it does not end Init later.
 * @details PINGs come during the standby and with the cycle that ends it, and
 * during the deployment step. At first contact, the next cycle without a
 * PING leaves Init as it is; only a new PING ends it.
 */
void test_PING_before_first_contact_is_dropped(void)
{
    boot();
    check_next_state(N_OBC_PING_RECEIVED);
    TEST_ASSERT_EQUAL_INT(INIT_STEP_STANDBY, init_step);

    set_uptime_s(INIT_STANDBY_S);
    check_next_state(N_OBC_PING_RECEIVED);   /* ends the standby */
    check_next_state(N_OBC_PING_RECEIVED);   /* ends the deployment step */
    TEST_ASSERT_EQUAL_INT(INIT_STEP_FIRST_CONTACT, init_step);

    check_next_state(0);
    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());

    check_next_state(N_OBC_PING_RECEIVED);
    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
}

/**
 * @brief No state request ends Init: no task is asked to pause, no clock switch.
 * @details All four requests together, then NOMINAL alone, at first contact.
 * The telecommands that ask for them are refused during Init (tc_handler.c);
 * EPS's requests arrive here.
 */
void test_state_requests_do_not_end_Init(void)
{
    boot_at_first_contact();
    int notifications_before = fake_rtos_notify_log_count();

    check_next_state(N_OBC_EXIT_STATE_GROUP_MASK);
    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_NOMINAL);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(INIT_STEP_FIRST_CONTACT, init_step);
    TEST_ASSERT_EQUAL_INT(notifications_before, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
}

/**
 * @brief A lower-power request made during Init is carried out right after the PING: NOMINAL, then at once SURVIVAL.
 * @details The low-battery case of SPF §1.6.1. EPS asks for SURVIVAL during
 * the standby; the request waits through the standby's end, the deployment
 * and several cycles at first contact. The PING ends Init in NOMINAL (the
 * payload is resumed) and SURVIVAL follows in the same cycle: two clock
 * switches, 2 → 80 → 2 MHz, and nothing is left to do at the next cycle.
 */
void test_lower_power_request_during_Init_follows_the_PING(void)
{
    boot();
    set_uptime_s(10u * 60u);
    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);
    set_uptime_s(INIT_STANDBY_S);
    check_next_state(0);
    check_next_state(0);
    check_next_state(0);
    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(INIT_STEP_FIRST_CONTACT, init_step);

    check_next_state(N_OBC_PING_RECEIVED);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(2, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, clock_get_current());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD, resumed_among(TM_TASK_PAYLOAD));
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_SURVIVAL, paused_mask());

    check_next_state(0);
    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(2, changes_tried());
}

/**
 * @brief A lower-power request in the same notification as the PING is carried out at once too.
 */
void test_lower_power_request_with_the_PING(void)
{
    boot_at_first_contact();

    check_next_state(N_OBC_PING_RECEIVED | N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, clock_get_current());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_SURVIVAL, paused_mask());
}

/**
 * @brief An end of Init refused because OBDH did not acknowledge the pause is tried again at the next cycle, with the lower-power request.
 * @details OBDH is stuck at the PING: Init stays at first contact, the clock
 * stays, the tasks run again and the failure is counted. The health check
 * then restarts OBDH, and the OBC's next cycle, with no new notification,
 * ends Init and goes on to CONTINGENCY.
 */
void test_refused_end_of_Init_is_tried_again(void)
{
    boot_at_first_contact();
    sim_tasks_set_unresponsive(TM_TASK_OBDH);
    uint32_t failures = state_machine_change_failures();

    check_next_state(N_OBC_PING_RECEIVED | N_OBC_EXIT_STATE_TO_OBC_STATE_CONTINGENCY);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD, paused_mask());
    TEST_ASSERT_EQUAL_UINT32(failures + 1u, state_machine_change_failures());

    tm_handle_health_faults(HEALTH_BIT_OBDH);
    sim_tasks_set_unresponsive(0);
    check_next_state(0);   /* the OBC's next cycle */

    TEST_ASSERT_EQUAL_INT(OBC_STATE_CONTINGENCY, state_machine_current_state());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_CONTINGENCY, paused_mask());
}

/* =========================================================================
 * 4. Transitions after Init
 * ========================================================================= */

/**
 * @brief NOMINAL → SURVIVAL switches the clock to 2 MHz exactly once, with the scheduler suspended, and reconfigures the peripherals.
 * @details
 * - Arrange: NOMINAL. The fake clock is at 80 MHz.
 * - Act: `check_next_state()` with the "go to SURVIVAL" bit.
 * - Expected:
 *   - the state is SURVIVAL;
 *   - `clock_switch_to_freq(2 MHz)` was called once, at a moment when the
 *     scheduler was suspended (suspend depth 1). Switching the clock while
 *     tasks run would corrupt UART and SPI timings in the middle of a
 *     transfer;
 *   - `periph_reconfigure_for_freq(2 MHz)` was called once, so baud rates
 *     etc. match the new clock;
 *   - the clock now reports 2 MHz.
 */
void test_NOMINAL_to_SURVIVAL_switches_clock_to_2MHz_once(void)
{
    start_in(OBC_STATE_NOMINAL);

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, fake_clock_last_switch_freq());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_last_switch_suspend_depth());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_reconfigure_count());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, fake_clock_last_reconfigure_freq());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, clock_get_current());
}

/**
 * @brief NOMINAL → CONTINGENCY doesn't touch the clock, because both states run at 80 MHz, and pauses only the payload.
 * @details A clock switch is slow and risky, so it must happen only when the
 * frequency actually changes. Expected: state CONTINGENCY, 0 switches and 0
 * peripheral reconfigurations.
 */
void test_NOMINAL_to_CONTINGENCY_does_not_switch_clock(void)
{
    start_in(OBC_STATE_NOMINAL);

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_CONTINGENCY);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_CONTINGENCY, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_reconfigure_count());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD, paused_mask());
}

/**
 * @brief A transition stores nothing, and leaves the activation flags as they were.
 * @details The old code wrote the state at every change, over the deployment
 * flags (KB-3). Now a change sends no OBDH request at all.
 */
void test_transition_stores_nothing(void)
{
    preset_done(PARAM_DEPLOY_COMMS);
    preset_done(PARAM_DEPLOY_PAYLOAD);
    reboot();
    start_in(OBC_STATE_NOMINAL);

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    reboot();
    TEST_ASSERT_EQUAL_UINT8(1u, flag_value(PARAM_DEPLOY_COMMS));
    TEST_ASSERT_EQUAL_UINT8(1u, flag_value(PARAM_DEPLOY_PAYLOAD));
}

/**
 * @brief A transition pauses every task of the old state, then resumes exactly the tasks of the new state.
 * @details For NOMINAL → SURVIVAL:
 * - all 7 tasks (the NOMINAL set) receive PAUSE;
 * - only the SURVIVAL tasks (EPS, COMMS, OBDH, TRANSCEIVER) receive RESUME;
 * - PAYLOAD, ADCS and BEACON end up paused;
 * - only the 4 running tasks are expected to report health.
 *
 * Health bits are numbered in a different order from the `TM_TASK_*` bits, so
 * the last check uses the `HEALTH_BIT_*` names.
 */
void test_transition_pauses_old_and_resumes_new_tasks(void)
{
    start_in(OBC_STATE_NOMINAL);

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);

    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_SURVIVAL, paused_mask());
    for (uint32_t i = 0; i < 7; i++) {
        uint32_t bit = 1u << i;
        uint32_t sent = fake_rtos_bits_sent_to(tm_get_task_handle(bit));
        TEST_ASSERT_BITS_HIGH_MESSAGE(N_TASK_PAUSE, sent, "old task was not asked to pause");
        if (SET_SURVIVAL & bit) {
            TEST_ASSERT_BITS_HIGH_MESSAGE(N_TASK_RESUME, sent, "new task was not asked to resume");
        } else {
            TEST_ASSERT_BITS_LOW_MESSAGE(N_TASK_RESUME, sent, "task outside the new state was resumed");
        }
    }
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS | HEALTH_BIT_COMMS | HEALTH_BIT_OBDH | HEALTH_BIT_TRANSCEIVER,
                            health_get_expected());
}

/**
 * @brief A notification without a state-change bit changes nothing.
 * @details The OBC passes every notification it receives to
 * `check_next_state()`, including time updates, reboot requests and PINGs,
 * which after Init only check the link. Those must not trigger a
 * transition. After a mix of those bits and an empty value, the test expects:
 * - the state is still NOMINAL;
 * - no OBDH request and no clock switch happened;
 * - no notification was sent;
 * - no task is paused.
 */
void test_non_exit_notification_changes_nothing(void)
{
    start_in(OBC_STATE_NOMINAL);
    int notifications_before = fake_rtos_notify_log_count();

    check_next_state(N_OBC_UPDATE_TIME | N_OBC_HARD_REBOOT | N_OBC_PERIPHERALS_REBOOT | N_OBC_PING_RECEIVED);
    check_next_state(0);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(notifications_before, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_HEX32(0u, paused_mask());
}

/**
 * @brief A request for the current state changes nothing: no pause, no clock switch.
 * @details A repeated telecommand, or an EPS request during an eclipse, asks
 * for the state the satellite is already in.
 */
void test_request_for_the_current_state_changes_nothing(void)
{
    start_in(OBC_STATE_SURVIVAL);
    int notifications_before = fake_rtos_notify_log_count();

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(notifications_before, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_SURVIVAL, paused_mask());
}

/**
 * @brief When several state-change bits arrive together, NOMINAL wins.
 * @details `check_next_state()` checks the bits in a fixed order: NOMINAL,
 * CONTINGENCY, then SURVIVAL. With SURVIVAL and NOMINAL both requested at
 * once, the satellite goes to NOMINAL, and only one transition happens.
 */
void test_multiple_exit_bits_NOMINAL_has_priority(void)
{
    start_in(OBC_STATE_CONTINGENCY);

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL |
                     N_OBC_EXIT_STATE_TO_OBC_STATE_NOMINAL);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(1, changes_tried());
}

/**
 * @brief Each request is carried out in turn: a change leaves no request behind.
 * @details From SURVIVAL, NOMINAL is asked, then SURVIVAL. Only a refused
 * request is kept, so NOMINAL, which comes first in the priority order, does
 * not take SURVIVAL's turn.
 */
void test_carried_out_request_is_not_kept(void)
{
    start_in(OBC_STATE_SURVIVAL);

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_NOMINAL);
    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(2, changes_tried());
}

/* =========================================================================
 * 5. OBDH must acknowledge the pause
 * ========================================================================= */

/**
 * @brief Without OBDH's acknowledgement of the pause the state does not change, and the state machine keeps the request.
 * @details OBDH is stuck, so it may be in the middle of a flash operation.
 * Expected after NOMINAL → SURVIVAL is asked:
 * - the state is still NOMINAL, the clock was not switched, nothing was written;
 * - every task runs again (the others had paused; they are resumed);
 * - the failure is counted;
 * - nothing is notified to the OBC task: the state machine keeps the
 *   request itself (the next tests try it again).
 */
void test_transition_refused_without_obdh_pause_ack(void)
{
    start_in(OBC_STATE_NOMINAL);
    sim_tasks_set_unresponsive(TM_TASK_OBDH);
    uint32_t failures = state_machine_change_failures();

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_HEX32(0u, paused_mask());
    TEST_ASSERT_EQUAL_UINT32(failures + 1u, state_machine_change_failures());
    TEST_ASSERT_FALSE(fake_rtos_notify_pending(fake_rtos_test_task(), 0));
}

/**
 * @brief A refused change is tried again at each OBC cycle, and happens once OBDH really pauses.
 * @details OBDH is stuck for the first attempt, then works again without a
 * restart. Its notifications still hold that attempt's PAUSE and RESUME, so at
 * the second attempt it finds both: the resume wins, and its acknowledgement
 * of the resume is not taken for one of the pause, so the second attempt is
 * refused as well. The third succeeds, with OBDH paused during the clock
 * switch (checked in tearDown()).
 */
void test_refused_transition_is_retried_until_obdh_pauses(void)
{
    start_in(OBC_STATE_NOMINAL);
    uint32_t failures = state_machine_change_failures();
    sim_tasks_set_unresponsive(TM_TASK_OBDH);
    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);
    sim_tasks_set_unresponsive(0);

    for (int cycle = 0; (cycle < 3) && (state_machine_current_state() != OBC_STATE_SURVIVAL); cycle++) {
        check_next_state(0);   /* the OBC's next cycle, with no new notification */
    }

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_UINT32(failures + 2u, state_machine_change_failures());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_SURVIVAL, paused_mask());
}

/**
 * @brief A stuck OBDH that the health monitor restarts acknowledges the next pause at once.
 * @details This is the usual recovery: a stuck OBDH stops reporting, the
 * health check restarts it, and the new task starts with no notifications.
 */
void test_restarted_obdh_acknowledges_the_next_pause(void)
{
    start_in(OBC_STATE_NOMINAL);
    sim_tasks_set_unresponsive(TM_TASK_OBDH);
    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);   /* refused */

    tm_handle_health_faults(HEALTH_BIT_OBDH);
    sim_tasks_set_unresponsive(0);
    check_next_state(0);   /* the OBC's next cycle */

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_switch_count());
}

/**
 * @brief A request refused and then carried out is not tried again.
 * @details NOMINAL → SURVIVAL is refused once, then carried out at the next
 * cycle, once the health check has restarted OBDH. A telecommand then brings
 * the satellite back to NOMINAL: the OBC's next cycle, with no new
 * notification, must leave it in NOMINAL. Two changes, two clock switches.
 */
void test_request_carried_out_is_not_tried_again(void)
{
    start_in(OBC_STATE_NOMINAL);
    sim_tasks_set_unresponsive(TM_TASK_OBDH);
    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);   /* refused */
    tm_handle_health_faults(HEALTH_BIT_OBDH);
    sim_tasks_set_unresponsive(0);
    check_next_state(0);                                   /* carried out */
    TEST_ASSERT_EQUAL_INT(OBC_STATE_SURVIVAL, state_machine_current_state());

    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_NOMINAL);
    check_next_state(0);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(2, fake_clock_switch_count());
}

/* =========================================================================
 * 6. Only the OBC task changes the state
 * ========================================================================= */

/**
 * @brief obc_is_caller() is true in the OBC task only: not in another task, and not before the scheduler starts.
 * @details Before the scheduler starts, the current task FreeRTOS reports is
 * not the caller, so main() would pass for the OBC task without the
 * scheduler check.
 */
void test_obc_is_caller_only_in_the_obc_task(void)
{
    boot();
    TEST_ASSERT_TRUE(obc_is_caller());

    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_COMMS));
    TEST_ASSERT_FALSE(obc_is_caller());
    fake_rtos_set_current_task(NULL);

    fake_rtos_set_scheduler_state(taskSCHEDULER_NOT_STARTED);
    TEST_ASSERT_FALSE(obc_is_caller());
    fake_rtos_set_scheduler_state(taskSCHEDULER_RUNNING);
}

/**
 * @brief A state change that another task asks for directly changes nothing.
 * @details COMMS calls `check_next_state()` itself instead of notifying the
 * OBC. Expected: the state is still NOMINAL, no notification was sent (no
 * task was asked to pause), the clock did not switch and nothing was written.
 * Nothing is kept either: the OBC's next cycle changes nothing.
 */
void test_other_task_cannot_change_the_state(void)
{
    start_in(OBC_STATE_NOMINAL);
    int notifications_before = fake_rtos_notify_log_count();

    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_COMMS));
    check_next_state(N_OBC_EXIT_STATE_TO_OBC_STATE_SURVIVAL);
    fake_rtos_set_current_task(NULL);
    check_next_state(0);   /* the OBC's next cycle */

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NOMINAL, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(notifications_before, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_HEX32(0u, paused_mask());
}

/**
 * @brief Another task cannot end Init.
 * @details At first contact, COMMS passes a PING to `check_next_state()`
 * itself: Init goes on, and the PING is not kept for the OBC's next cycle.
 */
void test_other_task_cannot_end_Init(void)
{
    boot_at_first_contact();

    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_COMMS));
    check_next_state(N_OBC_PING_RECEIVED);
    fake_rtos_set_current_task(NULL);
    check_next_state(0);   /* the OBC's next cycle */

    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
}

/**
 * @brief Another task cannot end the standby.
 * @details At 45 minutes, COMMS calls `check_next_state()` itself: the
 * standby goes on and nothing is written, until the OBC's own call ends it.
 */
void test_other_task_cannot_end_the_standby(void)
{
    boot();
    set_uptime_s(INIT_STANDBY_S);

    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_COMMS));
    check_next_state(0);
    fake_rtos_set_current_task(NULL);

    TEST_ASSERT_EQUAL_INT(INIT_STEP_STANDBY, init_step);
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());

    check_next_state(0);   /* the OBC's own call */
    TEST_ASSERT_EQUAL_INT(INIT_STEP_DEPLOY_COMMS, init_step);
}

/**
 * @brief A boot from another task creates nothing and changes nothing.
 * @details A second boot would create every task again and overwrite the
 * task table's handles. Expected: false, still 7 tasks created, COMMS keeps
 * its handle, and Init is still at the standby with the same tasks paused.
 */
void test_other_task_cannot_boot(void)
{
    boot();
    TaskHandle_t comms = tm_get_task_handle(TM_TASK_COMMS);

    fake_rtos_set_current_task(comms);
    bool booted = state_machine_boot();
    fake_rtos_set_current_task(NULL);

    TEST_ASSERT_FALSE(booted);
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());
    TEST_ASSERT_EQUAL_PTR(comms, tm_get_task_handle(TM_TASK_COMMS));
    TEST_ASSERT_EQUAL_INT(OBC_STATE_INIT, state_machine_current_state());
    TEST_ASSERT_EQUAL_INT(INIT_STEP_STANDBY, init_step);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_INIT_STANDBY, paused_mask());
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 * @details A test function that isn't listed here never runs. See
 * test/support/pocat_test_main.h for the `-l` / `-f` / `-n` / `-x` options.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    POCAT_RUN_TEST(test_freq_for_state_mapping);
    POCAT_RUN_TEST(test_boot_without_activation_history_starts_in_standby);
    POCAT_RUN_TEST(test_boot_with_standby_done_starts_at_the_deployment);
    POCAT_RUN_TEST(test_deploy_flag_alone_does_not_skip_the_standby);
    POCAT_RUN_TEST(test_a_flag_other_than_1_does_not_skip_its_step);
    POCAT_RUN_TEST(test_standby_lasts_45_minutes_and_is_stored);
    POCAT_RUN_TEST(test_deployment_step_leads_to_first_contact);
    POCAT_RUN_TEST(test_standby_not_stored_still_ends);
    POCAT_RUN_TEST(test_reboot_with_both_flags_waits_for_PING_at_first_contact);
    POCAT_RUN_TEST(test_PING_at_first_contact_ends_Init_in_NOMINAL);
    POCAT_RUN_TEST(test_PING_before_first_contact_is_dropped);
    POCAT_RUN_TEST(test_state_requests_do_not_end_Init);
    POCAT_RUN_TEST(test_lower_power_request_during_Init_follows_the_PING);
    POCAT_RUN_TEST(test_lower_power_request_with_the_PING);
    POCAT_RUN_TEST(test_refused_end_of_Init_is_tried_again);
    POCAT_RUN_TEST(test_NOMINAL_to_SURVIVAL_switches_clock_to_2MHz_once);
    POCAT_RUN_TEST(test_NOMINAL_to_CONTINGENCY_does_not_switch_clock);
    POCAT_RUN_TEST(test_transition_stores_nothing);
    POCAT_RUN_TEST(test_transition_pauses_old_and_resumes_new_tasks);
    POCAT_RUN_TEST(test_non_exit_notification_changes_nothing);
    POCAT_RUN_TEST(test_request_for_the_current_state_changes_nothing);
    POCAT_RUN_TEST(test_multiple_exit_bits_NOMINAL_has_priority);
    POCAT_RUN_TEST(test_carried_out_request_is_not_kept);
    POCAT_RUN_TEST(test_transition_refused_without_obdh_pause_ack);
    POCAT_RUN_TEST(test_refused_transition_is_retried_until_obdh_pauses);
    POCAT_RUN_TEST(test_restarted_obdh_acknowledges_the_next_pause);
    POCAT_RUN_TEST(test_request_carried_out_is_not_tried_again);
    POCAT_RUN_TEST(test_obc_is_caller_only_in_the_obc_task);
    POCAT_RUN_TEST(test_other_task_cannot_change_the_state);
    POCAT_RUN_TEST(test_other_task_cannot_end_Init);
    POCAT_RUN_TEST(test_other_task_cannot_end_the_standby);
    POCAT_RUN_TEST(test_other_task_cannot_boot);
    return POCAT_END();
}
