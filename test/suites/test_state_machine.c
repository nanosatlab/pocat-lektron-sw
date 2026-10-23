/**
 * @file test_state_machine.c
 * @brief Host tests for the OBC operational state machine: clock frequency per state, task sets at boot, and transitions.
 * @date 2026-10-23
 *
 * @details
 * **What is under test**
 * - src/subsystems/obc/state_machine.c:
 *   - `freq_for_state()`
 *   - `state_machine_boot()`
 *   - `check_next_state()`, and through it the private `change_state()`.
 *
 * **What `change_state()` does, in order** (this is what the tests check)
 * 1. Writes the old state to `PREVIOUS_STATE_ADDR` (OBDH write request).
 * 2. Pauses every task of the old state (`tm_pause_tasks`).
 * 3. If the new state needs a different clock: suspends the scheduler,
 *    switches the clock, reconfigures the peripherals, and resumes the
 *    scheduler.
 * 4. Resumes every task of the new state (`tm_resume_tasks`).
 * 5. Writes the new state to `CURRENT_STATE_ADDR`.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below: `state_machine.c`, `task_management.c` and `health.c`.
 *   The real task manager runs underneath the state machine, so the
 *   pause/resume handshakes are real and not stubbed.
 * - SOURCES:
 *   - `notifications.c`, `stub_task_entries.c` and `sim_tasks.c`;
 *   - `fake_clock.c`, which counts clock switches and peripheral
 *     reconfigurations and starts at 80 MHz;
 *   - `fake_obdh_requests.c`, which serves OBDH requests from a 1 MB RAM
 *     copy of flash (initially 0xFF) and logs every call.
 *
 * **Fake setup**
 * - Block hook = `sim_tasks_run_pending()`, so every task answers PAUSE and
 *   RESUME immediately.
 * - `fake_obdh_flash(addr)` gives a pointer into the RAM flash image, used to
 *   check what was written or to preset bytes before a test.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_state_machine
 * docker run --rm -v "${PWD}:/app" pocat-tests test_state_machine -f NM_to_SM
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_clock.h"
#include "fake_obdh_requests.h"
#include "sim_tasks.h"
#include "pocat_test_main.h"

#include "state_machine.c"
#include "task_management.c"
#include "health.c"

/** @name Tasks that run in each state
 *  Copied from `tasks_for_state()` in state_machine.c. If that function
 *  changes on purpose, update these sets too.
 *  @{ */
#define SET_NM  (TM_TASK_ALL)                          /**< Nominal mode: everything runs. */
#define SET_CM  (TM_TASK_ALL & ~TM_TASK_PAYLOAD)       /**< Contingency mode: payload off. */
#define SET_SSM (TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_OBDH | TM_TASK_TRANSCEIVER | TM_TASK_BEACON) /**< Sun-safe mode: payload and ADCS off. */
#define SET_SM  (TM_TASK_EPS | TM_TASK_COMMS | TM_TASK_OBDH | TM_TASK_TRANSCEIVER)                  /**< Survival mode: also no beacon. */
/** @} */

/**
 * @brief Runs before every test: fresh fakes, an erased fake flash, and a reset task manager.
 * @details Resets every fake and every static of the included modules, for
 * the same reason as in test_task_management.c: stale handles would point at
 * tasks that `fake_rtos_reset()` has freed. Then it installs the
 * `sim_tasks_run_pending()` block hook.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    fake_clock_reset();
    fake_obdh_reset();
    sim_tasks_reset();

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

    fake_rtos_set_block_hook(sim_tasks_run_pending);
}

/**
 * @brief Runs after every test: checks that the code left the RTOS balanced.
 * @details `change_state()` suspends the scheduler around the clock switch.
 * If it forgot to resume it, the satellite would freeze. This check catches
 * that.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
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

/**
 * @brief Helper: boots into a state, as `main.c` → `obc_task` does, and checks that boot worked.
 * @param state State to boot in (on the satellite, it is read back from flash).
 */
static void boot_in(obc_state_t state)
{
    TEST_ASSERT_TRUE(state_machine_boot(state));
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());
}

/* =========================================================================
 * 1. State → clock frequency, and boot
 * ========================================================================= */

/**
 * @brief Each state maps to the documented clock frequency.
 * @details SSM runs at 8 MHz and SM at 2 MHz to save power. BOOT, NM, CM and
 * SAFE_HOLD run at 80 MHz. `main.c` uses this mapping at boot, before
 * FreeRTOS starts, so a wrong value would boot the satellite at the wrong
 * speed.
 */
void test_freq_for_state_mapping(void)
{
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, freq_for_state(OBC_STATE_BOOT));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, freq_for_state(OBC_STATE_NM));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, freq_for_state(OBC_STATE_CM));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_8MHZ,  freq_for_state(OBC_STATE_SSM));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ,  freq_for_state(OBC_STATE_SM));
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_80MHZ, freq_for_state(OBC_STATE_SAFE_HOLD));
}

/**
 * @brief Booting in NM creates all 7 tasks, pauses none, and expects all 7 to report health.
 */
void test_boot_NM_runs_all_tasks(void)
{
    boot_in(OBC_STATE_NM);
    TEST_ASSERT_EQUAL_HEX32(0u, paused_mask());
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_PAYLOAD | HEALTH_BIT_EPS | HEALTH_BIT_COMMS | HEALTH_BIT_ADCS |
                            HEALTH_BIT_OBDH | HEALTH_BIT_TRANSCEIVER | HEALTH_BIT_BEACON, health_get_expected());
}

/**
 * @brief Booting in CM creates all 7 tasks but pauses only the payload.
 * @details Every task is always created; the ones that shouldn't run are
 * created paused, so a later state change only has to resume them.
 */
void test_boot_CM_pauses_only_payload(void)
{
    boot_in(OBC_STATE_CM);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD, paused_mask());
}

/**
 * @brief Booting in SM (survival) pauses the payload, ADCS and the beacon.
 */
void test_boot_SM_pauses_payload_adcs_beacon(void)
{
    boot_in(OBC_STATE_SM);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD | TM_TASK_ADCS | TM_TASK_BEACON, paused_mask());
}

/* =========================================================================
 * 2. Transitions
 * ========================================================================= */

/**
 * @brief NM → SM switches the clock to 2 MHz exactly once, with the scheduler suspended, and reconfigures the peripherals.
 * @details
 * - Arrange: boot in NM. The fake clock is at 80 MHz.
 * - Act: `check_next_state()` with the "go to SM" bit.
 * - Expected:
 *   - the state is SM;
 *   - `clock_switch_to_freq(2 MHz)` was called once, at a moment when the
 *     scheduler was suspended (suspend depth 1). Switching the clock while
 *     tasks run would corrupt UART and SPI timings in the middle of a
 *     transfer;
 *   - `periph_reconfigure_for_freq(2 MHz)` was called once, so baud rates
 *     etc. match the new clock;
 *   - the clock now reports 2 MHz.
 */
void test_NM_to_SM_switches_clock_to_2MHz_once(void)
{
    obc_state_t state = OBC_STATE_NM;
    boot_in(state);

    check_next_state(&state, N_OBC_EXIT_STATE_TO_OBC_STATE_SM);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SM, state);
    TEST_ASSERT_EQUAL_INT(1, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, fake_clock_last_switch_freq());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_last_switch_suspend_depth());
    TEST_ASSERT_EQUAL_INT(1, fake_clock_reconfigure_count());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, fake_clock_last_reconfigure_freq());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, clock_get_current());
}

/**
 * @brief NM → CM doesn't touch the clock, because both states run at 80 MHz.
 * @details A clock switch is slow and risky, so it must happen only when the
 * frequency actually changes. Expected: state CM, 0 switches and 0
 * peripheral reconfigurations.
 */
void test_NM_to_CM_does_not_switch_clock(void)
{
    obc_state_t state = OBC_STATE_NM;
    boot_in(state);

    check_next_state(&state, N_OBC_EXIT_STATE_TO_OBC_STATE_CM);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_CM, state);
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_reconfigure_count());
}

/**
 * @brief After a transition, the new state is saved to flash at `CURRENT_STATE_ADDR`.
 * @details `main.c` reads this address at the next boot to restore the state,
 * so a satellite that reboots in SM must come back in SM. The test checks the
 * byte in the fake flash, and that the last OBDH call was a WRITE to that
 * address.
 */
void test_transition_persists_current_state(void)
{
    obc_state_t state = OBC_STATE_NM;
    boot_in(state);

    check_next_state(&state, N_OBC_EXIT_STATE_TO_OBC_STATE_SM);

    TEST_ASSERT_EQUAL_UINT8(OBC_STATE_SM, fake_obdh_flash(CURRENT_STATE_ADDR)[0]);
    fake_obdh_call_t last = fake_obdh_call_at(fake_obdh_call_count() - 1);
    TEST_ASSERT_EQUAL_INT(FAKE_OBDH_WRITE, last.op);
    TEST_ASSERT_EQUAL_HEX32(CURRENT_STATE_ADDR, last.addr);
}

/**
 * @brief A transition pauses every task of the old state, then resumes exactly the tasks of the new state.
 * @details For NM → SM:
 * - all 7 tasks (the NM set) receive PAUSE;
 * - only the SM tasks (EPS, COMMS, OBDH, TRANSCEIVER) receive RESUME;
 * - PAYLOAD, ADCS and BEACON end up paused;
 * - only the 4 running tasks are expected to report health.
 *
 * Health bits are numbered in a different order from the `TM_TASK_*` bits, so
 * the last check uses the `HEALTH_BIT_*` names.
 */
void test_transition_pauses_old_and_resumes_new_tasks(void)
{
    obc_state_t state = OBC_STATE_NM;
    boot_in(state);

    check_next_state(&state, N_OBC_EXIT_STATE_TO_OBC_STATE_SM);

    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ALL & ~SET_SM, paused_mask());
    for (uint32_t i = 0; i < 7; i++) {
        uint32_t bit = 1u << i;
        uint32_t sent = fake_rtos_bits_sent_to(tm_get_task_handle(bit));
        TEST_ASSERT_BITS_HIGH_MESSAGE(N_TASK_PAUSE, sent, "old task was not asked to pause");
        if (SET_SM & bit) {
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
 * `check_next_state()`, including time updates and reboot requests. Those
 * must not trigger a transition. After a time-update/reboot bit mix and an
 * empty value, the test expects:
 * - the state is still NM;
 * - no flash write and no clock switch happened;
 * - no notification was sent;
 * - no task is paused.
 */
void test_non_exit_notification_changes_nothing(void)
{
    obc_state_t state = OBC_STATE_NM;
    boot_in(state);
    int notifications_before = fake_rtos_notify_log_count();

    check_next_state(&state, N_OBC_UPDATE_TIME | N_OBC_HARD_REBOOT | N_OBC_PERIPHERALS_REBOOT);
    check_next_state(&state, 0);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NM, state);
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(notifications_before, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_HEX32(0u, paused_mask());
}

/**
 * @brief When several state-change bits arrive together, NM wins.
 * @details `check_next_state()` checks the bits in a fixed order: NM, CM, SSM,
 * then SM. With SM, SSM and NM all requested at once, the satellite goes to
 * NM, and only one transition happens.
 */
void test_multiple_exit_bits_NM_has_priority(void)
{
    obc_state_t state = OBC_STATE_CM;
    boot_in(state);

    check_next_state(&state, N_OBC_EXIT_STATE_TO_OBC_STATE_SM | N_OBC_EXIT_STATE_TO_OBC_STATE_SSM |
                             N_OBC_EXIT_STATE_TO_OBC_STATE_NM);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NM, state);
}

/* =========================================================================
 * 3. Known bugs (ignored until the production code is fixed)
 * ========================================================================= */

/**
 * @brief The state before a transition is kept at `PREVIOUS_STATE_ADDR`.
 * @details
 * - Expected: after NM → SM, the byte at `PREVIOUS_STATE_ADDR` (0x08030001)
 *   is NM (1).
 * - Actual: 0. `change_state()` writes the old state with
 *   `sizeof(obc_state_t)` (4 bytes) at 0x08030001. Then the 4-byte write of the
 *   new state at 0x08030000 overwrites 0x08030001 with the upper (zero) byte of
 *   the new state.
 * @bug KB-3 (test/README.md). Delete the TEST_IGNORE_MESSAGE line once the
 * state writes are 1 byte each.
 */
void test_transition_preserves_previous_state(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KB-3: change_state() writes 4 bytes at PREVIOUS_STATE_ADDR, overwritten by the CURRENT_STATE write");
    obc_state_t state = OBC_STATE_NM;
    boot_in(state);

    check_next_state(&state, N_OBC_EXIT_STATE_TO_OBC_STATE_SM);

    TEST_ASSERT_EQUAL_UINT8(OBC_STATE_NM, fake_obdh_flash(PREVIOUS_STATE_ADDR)[0]);
}

/**
 * @brief A state change doesn't modify the neighbouring deployment flags.
 * @details `DEPLOYMENT_STATE_ADDR` (0x08030002) and `DEPLOYMENTRF_STATE_ADDR`
 * (0x08030003) record whether the antennas were already deployed. Losing them
 * could repeat the deployment sequence after a reboot.
 * - Arrange: preset them to 0xAA and 0xBB in the fake flash.
 * - Expected: both still hold their values after NM → SM.
 * - Actual: both are 0x00, overwritten by the 4-byte state writes.
 * @bug KB-3 (test/README.md). Same root cause as
 * test_transition_preserves_previous_state().
 */
void test_transition_does_not_touch_deployment_flags(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KB-3: overlapping 4-byte state writes zero the deployment flags");
    obc_state_t state = OBC_STATE_NM;
    boot_in(state);
    fake_obdh_flash(DEPLOYMENT_STATE_ADDR)[0]   = 0xAA;
    fake_obdh_flash(DEPLOYMENTRF_STATE_ADDR)[0] = 0xBB;

    check_next_state(&state, N_OBC_EXIT_STATE_TO_OBC_STATE_SM);

    TEST_ASSERT_EQUAL_HEX8(0xAA, fake_obdh_flash(DEPLOYMENT_STATE_ADDR)[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, fake_obdh_flash(DEPLOYMENTRF_STATE_ADDR)[0]);
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
    POCAT_RUN_TEST(test_boot_NM_runs_all_tasks);
    POCAT_RUN_TEST(test_boot_CM_pauses_only_payload);
    POCAT_RUN_TEST(test_boot_SM_pauses_payload_adcs_beacon);
    POCAT_RUN_TEST(test_NM_to_SM_switches_clock_to_2MHz_once);
    POCAT_RUN_TEST(test_NM_to_CM_does_not_switch_clock);
    POCAT_RUN_TEST(test_transition_persists_current_state);
    POCAT_RUN_TEST(test_transition_pauses_old_and_resumes_new_tasks);
    POCAT_RUN_TEST(test_non_exit_notification_changes_nothing);
    POCAT_RUN_TEST(test_multiple_exit_bits_NM_has_priority);
    POCAT_RUN_TEST(test_transition_preserves_previous_state);
    POCAT_RUN_TEST(test_transition_does_not_touch_deployment_flags);
    return POCAT_END();
}
