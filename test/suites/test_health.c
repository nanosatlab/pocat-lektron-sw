/**
 * @file test_health.c
 * @brief Host tests for the software watchdog (health monitor) and when it refreshes the hardware watchdog (IWDG).
 * @date 2026-10-23
 *
 * @details
 * **What is under test**: src/utils/health.c.
 *
 * **How the health monitor works**
 * - Each running subsystem task calls `health_kick(HEALTH_BIT_X)` in its
 *   loop, which sets its bit in an event group.
 * - The OBC calls `health_check()` in its loop. Once per period (5 s on the
 *   satellite, set by `health_config()`), it compares the kicked bits with
 *   the expected ones (`health_set_expected()`):
 *   - every expected task kicked: `HAL_IWDG_Refresh()` is called and the
 *     hardware watchdog doesn't reset the MCU;
 *   - some task is missing: its bit is returned as a fault. The OBC then
 *     restarts that task through `tm_handle_health_faults()`, and the IWDG
 *     is **not** refreshed during that period.
 *   After each check all bits are cleared and a new period starts.
 * - A task that becomes expected in the middle of a period gets a **grace
 *   period**: it counts as kicked for the rest of that period.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `health.c` is `#include`d, so the tests can reset its statics.
 * - Uses the default fakes:
 *   - fake_rtos provides the event group, the tick count (1 tick = 1 ms) and
 *     the critical-section counter;
 *   - fake_hal provides the `hiwdg` handle and counts `HAL_IWDG_Refresh()`.
 * - No block hook is needed: the health module never blocks.
 *
 * **How time passes in these tests**: nothing runs on its own. A test moves
 * time with `fake_rtos_advance_ticks()` and then calls `health_check()`, just
 * as the OBC loop would after that much time.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_health
 * docker run --rm -v "${PWD}:/app" pocat-tests test_health -f iwdg
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "pocat_test_main.h"

#include "health.c"

/** @brief Health period used by most tests, in ticks (ms). Short, so the numbers are easy to follow. */
#define PERIOD 100u

/**
 * @brief Runs before every test: fresh fakes and a freshly initialised health module.
 * @details Resets every static of health.c, as if after a reboot:
 * - the event group, expected bits, period and deadline;
 * - the registered IWDG handle.
 * Then it calls `health_init()`, which creates a new event group. No IWDG
 * handle is registered and no period is configured; each test does what it
 * needs.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();

    health_eg = NULL;
    expected_bits = 0;
    period_ticks = 0;
    next_deadline = 0;
    iwdg_handle = NULL;
    health_init();
}

/**
 * @brief Runs after every test: checks that health.c closed every critical section it opened.
 * @details health.c protects its state with `taskENTER_CRITICAL()`. A missing
 * exit would disable interrupts on the satellite forever.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/**
 * @brief Helper: sets up the module as `obc.c` does at boot, then completes the first period.
 * @details This is the boot order in `setup_obc()`: register the IWDG, set the
 * period, then create the tasks, which makes them expected. Because the tasks
 * become expected after the period started, the first period passes thanks to
 * the grace rule. The helper ends at the start of period 2, where every
 * expected task must kick by itself.
 * @param expected Health bits of the tasks to monitor.
 */
static void boot_and_finish_grace_period(EventBits_t expected)
{
    health_register_iwdg(&hiwdg);
    health_config(PERIOD);
    health_set_expected(expected);

    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT(1, fake_hal_iwdg_refresh_count());
}

/* =========================================================================
 * 1. Timing of the check
 * ========================================================================= */

/**
 * @brief Before the period ends, `health_check()` reports no fault and doesn't touch the IWDG, even if no task has kicked.
 * @details The OBC calls `health_check()` much more often than once per
 * period. Those early calls must do nothing; otherwise tasks would be
 * faulted before they had the full period to kick.
 */
void test_no_fault_and_no_refresh_before_period_elapses(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    fake_rtos_advance_ticks(PERIOD - 1u);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT(1, fake_hal_iwdg_refresh_count());   /* unchanged since the grace period */
}

/**
 * @brief The check happens exactly when the period ends (deadline tick included).
 * @details EPS doesn't kick in period 2. At tick `PERIOD` after the period
 * started, `health_check()` must already report EPS.
 */
void test_check_runs_exactly_at_deadline(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS, health_check());
}

/**
 * @brief A fault is reported once per period, not on every call after the deadline.
 * @details After a check finds a fault, a new period starts. Calling
 * `health_check()` again right away must return 0, so the OBC doesn't restart
 * the same task several times for one missed period.
 */
void test_fault_reported_once_per_period(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS, health_check());
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
}

/**
 * @brief A period of 0 is turned into 1 tick instead of making every call a check.
 * @details `health_config(0)` would otherwise give a deadline equal to "now".
 * The module enforces a minimum of 1 tick.
 */
void test_zero_period_is_treated_as_one_tick(void)
{
    health_config(0);
    TEST_ASSERT_EQUAL_UINT32(1u, period_ticks);

    fake_rtos_advance_ticks(1);
    health_register_iwdg(&hiwdg);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT(1, fake_hal_iwdg_refresh_count());
}

/**
 * @brief The deadline still works when the 32-bit tick counter wraps around.
 * @details At 1 kHz the tick counter wraps after about 49.7 days, well within
 * a mission. Here the period starts at tick 0xFFFFFFF0, so the deadline
 * wraps to 0x54. Three checks:
 * - **Right after configuring** (tick 0xFFFFFFF0): no check may happen. A
 *   naive unsigned `now >= deadline` gives `0xFFFFFFF0 >= 0x54`, which is
 *   true, and would run the check at once. This step is the one that catches
 *   that bug.
 * - **After 50 ticks** (the counter has wrapped to 0x22): the period hasn't
 *   ended, so still no check.
 * - **After 110 ticks** (0x5E): the period has ended. The check runs and
 *   refreshes the IWDG.
 */
void test_deadline_survives_tick_wraparound(void)
{
    health_register_iwdg(&hiwdg);
    fake_rtos_set_tick(0xFFFFFFF0u);
    health_config(PERIOD);

    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_hal_iwdg_refresh_count(), "check ran immediately: deadline compare is not wrap-safe");

    fake_rtos_advance_ticks(50);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_hal_iwdg_refresh_count(), "check ran before the wrapped deadline");

    fake_rtos_advance_ticks(60);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, fake_hal_iwdg_refresh_count(), "check never ran after the wrapped deadline");
}

/* =========================================================================
 * 2. Which tasks are reported
 * ========================================================================= */

/**
 * @brief A task that kicks during the period isn't reported, and the IWDG is refreshed.
 */
void test_kick_within_period_passes_and_refreshes_iwdg(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    fake_rtos_advance_ticks(PERIOD / 2u);
    health_kick(HEALTH_BIT_EPS);
    fake_rtos_advance_ticks(PERIOD / 2u);

    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT(2, fake_hal_iwdg_refresh_count());
}

/**
 * @brief Only the tasks that didn't kick are reported, and the IWDG isn't refreshed.
 * @details EPS, COMMS and OBDH are expected and only COMMS kicks. The result
 * must be exactly EPS | OBDH, so the OBC restarts those two and leaves COMMS
 * running. While any task is faulty the IWDG stays unrefreshed. If faults
 * persist, the hardware watchdog eventually resets the whole MCU.
 */
void test_only_missing_tasks_reported_and_no_refresh(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS | HEALTH_BIT_COMMS | HEALTH_BIT_OBDH);

    health_kick(HEALTH_BIT_COMMS);
    fake_rtos_advance_ticks(PERIOD);

    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS | HEALTH_BIT_OBDH, health_check());
    TEST_ASSERT_EQUAL_INT(1, fake_hal_iwdg_refresh_count());   /* only the grace-period refresh */
}

/**
 * @brief A kick only counts for the period it happened in.
 * @details EPS kicks in period 2, then stays silent in period 3. Period 3 must
 * report EPS: bits are cleared at each check, so an old kick can't hide a
 * task that stopped later.
 */
void test_kick_does_not_carry_over_to_next_period(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    health_kick(HEALTH_BIT_EPS);
    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());

    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS, health_check());
}

/**
 * @brief A task that isn't expected, for example a paused one, is never reported.
 * @details Only EPS is expected. COMMS never kicks, as when it is paused, and
 * must not appear in the result. This is what lets the state machine pause
 * tasks without triggering false faults.
 */
void test_unexpected_task_is_not_reported(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    health_kick(HEALTH_BIT_EPS);
    fake_rtos_advance_ticks(PERIOD);

    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
}

/**
 * @brief With no expected tasks at all, every completed period counts as healthy and refreshes the IWDG.
 */
void test_no_expected_tasks_still_refreshes_iwdg(void)
{
    health_register_iwdg(&hiwdg);
    health_config(PERIOD);

    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT(1, fake_hal_iwdg_refresh_count());
}

/**
 * @brief After a faulty period, the IWDG is refreshed again as soon as a period is healthy.
 * @details A single missed period must not leave the IWDG unrefreshed for
 * good. Otherwise the hardware watchdog would reset the MCU even after the
 * OBC had restarted the faulty task.
 * - Period 2: EPS doesn't kick, so a fault is reported and the refresh
 *   count stays at 1.
 * - Period 3: EPS kicks, so there is no fault and the refresh count goes
 *   to 2.
 */
void test_iwdg_refresh_resumes_after_faulty_period(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS, health_check());
    TEST_ASSERT_EQUAL_INT(1, fake_hal_iwdg_refresh_count());

    health_kick(HEALTH_BIT_EPS);
    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT(2, fake_hal_iwdg_refresh_count());
}

/**
 * @brief A check that is several periods late reports once, and the next period starts from the late check, not from the old deadline.
 * @details The OBC can be delayed, for example when a pause handshake waits
 * the full 10 s ACK timeout. Here `health_check()` is called 3 periods after
 * the deadline. Expected:
 * - one report of EPS;
 * - an immediate second call returns 0, with no catch-up reports for the
 *   periods that were skipped;
 * - the next check is due exactly one period after the late check.
 * This fixes the current drift behaviour: missed periods aren't counted.
 */
void test_late_check_reports_once_and_restarts_period(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    fake_rtos_advance_ticks(3u * PERIOD);
    TickType_t late = xTaskGetTickCount();
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS, health_check());
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_UINT32(late + PERIOD, next_deadline);

    fake_rtos_advance_ticks(PERIOD - 1u);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    fake_rtos_advance_ticks(1);
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS, health_check());
}

/* =========================================================================
 * 3. Grace period and changes to the expected set
 * ========================================================================= */

/**
 * @brief A task added to the expected set in the middle of a period isn't faulted in that period, but must kick from the next one.
 * @details This happens when a task is resumed after a state change.
 * - Period 2: EPS is expected; COMMS is added halfway through and doesn't
 *   kick. EPS kicks. Expected: no fault, thanks to the grace rule.
 * - Period 3: EPS kicks again, COMMS still doesn't. Expected: COMMS is
 *   reported.
 */
void test_newly_expected_task_gets_one_grace_period(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS);

    fake_rtos_advance_ticks(PERIOD / 2u);
    health_set_expected(HEALTH_BIT_EPS | HEALTH_BIT_COMMS);
    health_kick(HEALTH_BIT_EPS);
    fake_rtos_advance_ticks(PERIOD / 2u);
    TEST_ASSERT_EQUAL_HEX32(0u, health_check());

    health_kick(HEALTH_BIT_EPS);
    fake_rtos_advance_ticks(PERIOD);
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_COMMS, health_check());
}

/**
 * @brief Removing a task from the expected set stops it from being reported at once, even in the current period.
 * @details This is what happens when a task is paused. EPS and COMMS are
 * expected; COMMS is removed halfway through and never kicks. Expected: no
 * fault at the end of the period.
 */
void test_removed_task_is_not_reported(void)
{
    boot_and_finish_grace_period(HEALTH_BIT_EPS | HEALTH_BIT_COMMS);

    health_kick(HEALTH_BIT_EPS);
    fake_rtos_advance_ticks(PERIOD / 2u);
    health_set_expected(HEALTH_BIT_EPS);
    fake_rtos_advance_ticks(PERIOD / 2u);

    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS, health_get_expected());
}

/* =========================================================================
 * 4. Robustness
 * ========================================================================= */

/**
 * @brief Without a registered IWDG handle, a healthy check works and simply doesn't refresh anything.
 * @details Guards against a NULL dereference if `health_register_iwdg()` is
 * forgotten or called late.
 */
void test_no_iwdg_registered_does_not_refresh(void)
{
    health_config(PERIOD);
    fake_rtos_advance_ticks(PERIOD);

    TEST_ASSERT_EQUAL_HEX32(0u, health_check());
    TEST_ASSERT_EQUAL_INT(0, fake_hal_iwdg_refresh_count());
}

/**
 * @brief A `health_kick()` before `health_init()` is ignored instead of crashing.
 * @details A task could kick before the OBC has created the event group, for
 * example during a reset race. The module checks for NULL, and the kick must
 * not create a group as a side effect: `health_eg` stays NULL.
 */
void test_kick_before_init_is_ignored(void)
{
    health_eg = NULL;                 /* as if health_init() had not run yet */
    health_kick(HEALTH_BIT_EPS);      /* must not crash */
    TEST_ASSERT_NULL(health_eg);
}

/**
 * @brief Calling `health_init()` twice keeps the same event group, so kicks already recorded aren't lost.
 */
void test_init_twice_keeps_event_group(void)
{
    EventGroupHandle_t first = health_eg;
    health_kick(HEALTH_BIT_EPS);

    health_init();

    TEST_ASSERT_EQUAL_PTR(first, health_eg);
    TEST_ASSERT_BITS_HIGH(HEALTH_BIT_EPS, xEventGroupGetBits(health_eg));
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
    POCAT_RUN_TEST(test_no_fault_and_no_refresh_before_period_elapses);
    POCAT_RUN_TEST(test_check_runs_exactly_at_deadline);
    POCAT_RUN_TEST(test_fault_reported_once_per_period);
    POCAT_RUN_TEST(test_zero_period_is_treated_as_one_tick);
    POCAT_RUN_TEST(test_deadline_survives_tick_wraparound);
    POCAT_RUN_TEST(test_kick_within_period_passes_and_refreshes_iwdg);
    POCAT_RUN_TEST(test_only_missing_tasks_reported_and_no_refresh);
    POCAT_RUN_TEST(test_kick_does_not_carry_over_to_next_period);
    POCAT_RUN_TEST(test_unexpected_task_is_not_reported);
    POCAT_RUN_TEST(test_no_expected_tasks_still_refreshes_iwdg);
    POCAT_RUN_TEST(test_iwdg_refresh_resumes_after_faulty_period);
    POCAT_RUN_TEST(test_late_check_reports_once_and_restarts_period);
    POCAT_RUN_TEST(test_newly_expected_task_gets_one_grace_period);
    POCAT_RUN_TEST(test_removed_task_is_not_reported);
    POCAT_RUN_TEST(test_no_iwdg_registered_does_not_refresh);
    POCAT_RUN_TEST(test_kick_before_init_is_ignored);
    POCAT_RUN_TEST(test_init_twice_keeps_event_group);
    return POCAT_END();
}
