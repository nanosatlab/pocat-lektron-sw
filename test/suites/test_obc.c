/**
 * @file test_obc.c
 * @brief Host tests for the OBC task: boot sequence (`setup_obc`) and one processing cycle (`process_obc`).
 * @date 2026-10-29
 *
 * @details
 * **What is under test**
 * - src/subsystems/obc/obc.c:
 *   - `setup_obc()`: creates the OBDH request queue (10 entries), starts the
 *     health module, registers the IWDG, sets the 5 s health period, creates
 *     the subsystem tasks for the boot state and **persists the boot time**
 *     at `BOOT_TIME_ADDR`;
 *   - `process_obc()`: waits up to 2 s for a notification, forwards the state
 *     exit bits to the state machine and ignores every other bit.
 * - The transitions themselves are tested in test_state_machine; here only
 *   the glue between the notification and `check_next_state()` is checked.
 *
 * `obc_task()` and `setup_obc()`'s queue-creation failure branch end in
 * endless loops (`for(;;)`, `while(1)`) and are not driven. The tests call
 * the `static` `setup_obc()` / `process_obc()` directly, which works because
 * the `.c` is `#include`d.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below: `obc.c`, `state_machine.c`, `task_management.c`, `health.c`.
 * - SOURCES: `notifications.c`, `stub_task_entries.c`, `sim_tasks.c`,
 *   `fake_clock.c`, `fake_obdh_requests.c`.
 * - Suite-local definitions of the two symbols that no linked file provides:
 *   `obdh_queue_handle` (normally in obdh.c) and `time_get_unix()` (normally
 *   in time.c, backed by the RTC).
 *
 * **Fake setup**
 * - Block hook = `sim_tasks_run_pending()`, so the pause/resume handshakes of
 *   a state change complete.
 * - The OBC itself is not in the task table, so tests create a dummy "OBC"
 *   task with `obc_task_handle()` and make it current: `process_obc()` waits
 *   on the current task's notifications.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_obc
 * docker run --rm -v "${PWD}:/app" pocat-tests test_obc -f process
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_clock.h"
#include "fake_obdh_requests.h"
#include "sim_tasks.h"
#include "pocat_test_main.h"

#include "obc.c"
#include "state_machine.c"
#include "task_management.c"
#include "health.c"

/** @brief RTC value returned by the suite-local `time_get_unix()`; tests may change it. */
static uint32_t fake_now;

/** @brief Suite-local replacement of `time_get_unix()` (time.c is not linked): returns #fake_now. */
uint32_t time_get_unix(void) { return fake_now; }

/** @brief Suite-local definition of the OBDH queue handle (normally defined in obdh.c, which is not linked here). */
QueueHandle_t obdh_queue_handle;

/**
 * @brief Runs before every test: fresh fakes, an erased flash image and a reset task manager / health module.
 * @details Resets the fake RTOS, HAL, clock, OBDH flash and simulated tasks;
 * resets the statics of task_management.c (table handles, `paused`, ACK event
 * group) and health.c (event group, expected bits, period, deadline, IWDG
 * handle) and calls `health_init()` again. `obdh_queue_handle` is set to NULL
 * so a test can prove `setup_obc()` created it, and the RTC to 1 700 000 000.
 * Installs `sim_tasks_run_pending()` as the block hook.
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

    obdh_queue_handle = NULL;
    fake_now = 1700000000u;

    fake_rtos_set_block_hook(sim_tasks_run_pending);
}

/**
 * @brief Runs after every test: checks that the RTOS fake is balanced.
 * @details The state change suspends the scheduler around the clock switch;
 * forgetting to resume it would freeze the satellite.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/**
 * @brief Helper: creates a dummy OBC task, makes it the current task and returns its handle.
 * @details The real `obc_task` body is never run by the fake RTOS. The
 * handle is needed as the target of `xTaskNotify()` and as the "running"
 * task whose notifications `process_obc()` reads.
 * @return Handle of the OBC task.
 */
static TaskHandle_t obc_task_handle(void)
{
    TaskHandle_t h = NULL;
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(obc_task, "OBC", OBC_STACK_SIZE, NULL, OBC_PRIORITY, &h));
    fake_rtos_set_current_task(h);
    return h;
}

/**
 * @brief Helper: returns a bitmask of the tasks currently marked paused in `task_table[]`.
 * @return `TM_TASK_*` bits.
 */
static uint32_t paused_mask(void)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < TASK_TABLE_SIZE; i++) {
        if (task_table[i].paused) m |= (1u << i);
    }
    return m;
}

/* =========================================================================
 * 1. setup_obc()
 * ========================================================================= */

/**
 * @brief `setup_obc()` creates the OBDH request queue with room for exactly 10 requests.
 * @details Expected: the queue handle is set; 10 sends with timeout 0
 * succeed and the 11th fails with `errQUEUE_FULL`. Matters because every
 * flash access of every task goes through this queue; a smaller queue would
 * make requests fail under load.
 */
void test_setup_creates_obdh_queue_for_10_requests(void)
{
    setup_obc(OBC_STATE_NM);
    TEST_ASSERT_NOT_NULL(obdh_queue_handle);

    obdh_request req;
    memset(&req, 0, sizeof(req));
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(obdh_queue_handle, &req, 0));
    }
    TEST_ASSERT_EQUAL_INT(errQUEUE_FULL, xQueueSend(obdh_queue_handle, &req, 0));
}

/**
 * @brief `setup_obc()` registers the hardware watchdog and sets a 5 s health period.
 * @details Expected: `iwdg_handle == &hiwdg` and `period_ticks == 5000`.
 * Matters because the IWDG is the last line of defence: if it is not
 * registered, `health_check()` never refreshes it (or never starts it); a
 * wrong period either resets healthy tasks or detects hangs too late.
 */
void test_setup_registers_iwdg_and_5s_health_period(void)
{
    setup_obc(OBC_STATE_NM);
    TEST_ASSERT_TRUE(iwdg_handle == &hiwdg);
    TEST_ASSERT_EQUAL_UINT32(5000u, (uint32_t)period_ticks);
}

/**
 * @brief Booting in NM creates the 7 subsystem tasks, none paused, all monitored by the health module.
 * @details Expected: 7 tasks created, `paused_mask() == 0` and
 * `health_get_expected()` has all 7 health bits. Matters because a task that
 * exists but is not monitored can hang unnoticed.
 */
void test_setup_NM_creates_all_tasks_running(void)
{
    setup_obc(OBC_STATE_NM);
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());
    TEST_ASSERT_EQUAL_HEX32(0u, paused_mask());
    TEST_ASSERT_EQUAL_HEX32(0x7Fu, health_get_expected());
}

/**
 * @brief Booting in SM creates all tasks but leaves PAYLOAD, ADCS and BEACON paused and unmonitored.
 * @details SM keeps EPS, COMMS, OBDH and TRANSCEIVER running. Expected: 7
 * tasks exist, the paused set is PAYLOAD|ADCS|BEACON and their health bits
 * are absent from the expected set. Matters because a satellite that
 * rebooted in survival mode must not wake the payload (power budget) and the
 * watchdog must not fault tasks that are paused on purpose.
 */
void test_setup_SM_creates_payload_adcs_beacon_paused(void)
{
    setup_obc(OBC_STATE_SM);
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD | TM_TASK_ADCS | TM_TASK_BEACON, paused_mask());
    EventBits_t exp = health_get_expected();
    TEST_ASSERT_EQUAL_HEX32(0u, exp & (HEALTH_BIT_PAYLOAD | HEALTH_BIT_ADCS | HEALTH_BIT_BEACON));
    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS | HEALTH_BIT_COMMS | HEALTH_BIT_OBDH | HEALTH_BIT_TRANSCEIVER, exp);
}

/**
 * @brief `setup_obc()` writes the boot time (the RTC value) as 4 bytes at `BOOT_TIME_ADDR`.
 * @details Expected: the flash image holds `fake_now` there and the call log
 * has exactly one WRITE to that address with length 4. Matters because the
 * beacon reports uptime as `now - boot_time`; without this write the uptime
 * telemetry is wrong after every reboot.
 */
void test_setup_persists_boot_time(void)
{
    fake_now = 1712345678u;
    setup_obc(OBC_STATE_NM);

    uint32_t stored;
    memcpy(&stored, fake_obdh_flash(BOOT_TIME_ADDR), sizeof(stored));
    TEST_ASSERT_EQUAL_UINT32(1712345678u, stored);

    int writes = 0;
    for (int i = 0; i < fake_obdh_call_count(); i++) {
        fake_obdh_call_t c = fake_obdh_call_at(i);
        if (c.op == FAKE_OBDH_WRITE && c.addr == BOOT_TIME_ADDR) {
            TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)c.len);
            writes++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, writes);
}

/**
 * @brief Even if creating a subsystem task fails, `setup_obc()` returns and still persists the boot time.
 * @details Arrange `fake_rtos_fail_task_create_at(3)`. Expected: no hang,
 * the boot time is in flash. Documents the degraded-boot behaviour: the OBC
 * logs an error and carries on, so the watchdog and the surviving tasks keep
 * the satellite recoverable.
 */
void test_setup_persists_boot_time_even_if_task_creation_fails(void)
{
    fake_now = 1712345678u;
    fake_rtos_fail_task_create_at(3);
    setup_obc(OBC_STATE_NM);

    uint32_t stored;
    memcpy(&stored, fake_obdh_flash(BOOT_TIME_ADDR), sizeof(stored));
    TEST_ASSERT_EQUAL_UINT32(1712345678u, stored);
}

/* =========================================================================
 * 2. process_obc()
 * ========================================================================= */

/**
 * @brief A state-exit bit received by the OBC is forwarded to the state machine and the transition happens.
 * @details Boot in NM, send `N_OBC_EXIT_STATE_TO_OBC_STATE_SM`, run one
 * cycle. Expected: the state becomes SM, the byte at `CURRENT_STATE_ADDR` is
 * SM, and the clock was switched to 2 MHz exactly once. Matters because this
 * path is how every state-changing telecommand and the EPS logic take effect.
 */
void test_process_forwards_exit_bit_to_state_machine(void)
{
    setup_obc(OBC_STATE_NM);
    TaskHandle_t obc = obc_task_handle();
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskNotify(obc, N_OBC_EXIT_STATE_TO_OBC_STATE_SM, eSetBits));

    obc_state_t s = OBC_STATE_NM;
    process_obc(&s);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_SM, s);
    TEST_ASSERT_EQUAL_UINT8(OBC_STATE_SM, fake_obdh_flash(CURRENT_STATE_ADDR)[0]);
    TEST_ASSERT_EQUAL_INT(1, fake_clock_switch_count());
    TEST_ASSERT_EQUAL_INT(CLK_FREQ_2MHZ, fake_clock_last_switch_freq());
}

/**
 * @brief With nothing pending, one cycle waits 2 s, keeps the state and touches no flash.
 * @details Expected: the tick advances by exactly 2000, the state is unchanged
 * and the OBDH call count did not grow. The 2 s cadence is what drives
 * `health_check()` in the real loop.
 */
void test_process_without_notification_waits_2s_and_keeps_state(void)
{
    setup_obc(OBC_STATE_NM);
    obc_task_handle();
    int calls = fake_obdh_call_count();
    TickType_t t0 = xTaskGetTickCount();

    obc_state_t s = OBC_STATE_NM;
    process_obc(&s);

    TEST_ASSERT_EQUAL_UINT32(2000u, (uint32_t)(xTaskGetTickCount() - t0));
    TEST_ASSERT_EQUAL_INT(OBC_STATE_NM, s);
    TEST_ASSERT_EQUAL_INT(calls, fake_obdh_call_count());
}

/**
 * @brief Notifications that are not state-exit bits do not change state, flash or clock.
 * @details Sends UPDATE_TIME | HARD_REBOOT | SOFT_REBOOT | PERIPHERALS_REBOOT.
 * Expected: state unchanged, no new flash access, no clock switch.
 * @note KI-25: these four commands are acknowledged to the ground but
 * unimplemented in `process_obc()` (empty `if` blocks; PERIPHERALS_REBOOT is
 * not even tested). This test passes today and documents that gap. If someone
 * implements a reboot, this test must be updated on purpose.
 */
void test_process_non_exit_bits_do_not_change_state(void)
{
    setup_obc(OBC_STATE_NM);
    TaskHandle_t obc = obc_task_handle();
    int calls = fake_obdh_call_count();
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskNotify(obc, N_OBC_UPDATE_TIME | N_OBC_HARD_REBOOT |
                                                   N_OBC_SOFT_REBOOT | N_OBC_PERIPHERALS_REBOOT, eSetBits));

    obc_state_t s = OBC_STATE_NM;
    process_obc(&s);

    TEST_ASSERT_EQUAL_INT(OBC_STATE_NM, s);
    TEST_ASSERT_EQUAL_INT(calls, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(0, fake_clock_switch_count());
}

/**
 * @brief One cycle consumes the whole notification: a second cycle just idles for 2 s.
 * @details Send SM | UPDATE_TIME; the first cycle moves to SM. The second
 * cycle must return after 2000 ticks with no further change. Matters
 * because a notification that is not cleared would re-trigger the same
 * transition forever.
 */
void test_process_drains_notification_in_one_cycle(void)
{
    setup_obc(OBC_STATE_NM);
    TaskHandle_t obc = obc_task_handle();
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskNotify(obc, N_OBC_EXIT_STATE_TO_OBC_STATE_SM | N_OBC_UPDATE_TIME, eSetBits));

    obc_state_t s = OBC_STATE_NM;
    process_obc(&s);
    TEST_ASSERT_EQUAL_INT(OBC_STATE_SM, s);

    int calls = fake_obdh_call_count();
    int switches = fake_clock_switch_count();
    TickType_t t0 = xTaskGetTickCount();
    process_obc(&s);
    TEST_ASSERT_EQUAL_UINT32(2000u, (uint32_t)(xTaskGetTickCount() - t0));
    TEST_ASSERT_EQUAL_INT(OBC_STATE_SM, s);
    TEST_ASSERT_EQUAL_INT(calls, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(switches, fake_clock_switch_count());
}

/**
 * @brief A transition to the state the satellite is already in should do nothing.
 * @details In NM, send the NM exit bit. Expected: no PAUSE is sent to any
 * subsystem task and no flash write happens. Actual: `change_state()` pauses
 * and resumes every task and writes PREVIOUS_STATE and CURRENT_STATE. On the
 * satellite this causes a pointless service interruption (payload, ADCS,
 * beacon paused for the handshake) and two flash writes whenever the ground
 * repeats a "go to NM" telecommand.
 * @bug KI-26 (test/README.md) expected no PAUSE and no flash write, actual both happen.
 */
void test_exit_to_current_state_is_a_no_op(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-26: a transition to the current state pauses/resumes every task and writes flash");
    setup_obc(OBC_STATE_NM);
    TaskHandle_t obc = obc_task_handle();
    int calls = fake_obdh_call_count();
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskNotify(obc, N_OBC_EXIT_STATE_TO_OBC_STATE_NM, eSetBits));

    obc_state_t s = OBC_STATE_NM;
    process_obc(&s);

    for (uint32_t i = 0; i < TASK_TABLE_SIZE; i++) {
        TEST_ASSERT_EQUAL_HEX32(0u, fake_rtos_bits_sent_to(task_table[i].handle) & N_TASK_PAUSE);
    }
    TEST_ASSERT_EQUAL_INT(calls, fake_obdh_call_count());
}

/**
 * @brief Test runner: registers and runs every test of the suite.
 * @param argc Argument count (the `-l`, `-f`, `-n`, `-x` options are handled by pocat_parse_args()).
 * @param argv Arguments.
 * @return Number of failed tests (0 = all pass).
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    POCAT_RUN_TEST(test_setup_creates_obdh_queue_for_10_requests);
    POCAT_RUN_TEST(test_setup_registers_iwdg_and_5s_health_period);
    POCAT_RUN_TEST(test_setup_NM_creates_all_tasks_running);
    POCAT_RUN_TEST(test_setup_SM_creates_payload_adcs_beacon_paused);
    POCAT_RUN_TEST(test_setup_persists_boot_time);
    POCAT_RUN_TEST(test_setup_persists_boot_time_even_if_task_creation_fails);
    POCAT_RUN_TEST(test_process_forwards_exit_bit_to_state_machine);
    POCAT_RUN_TEST(test_process_without_notification_waits_2s_and_keeps_state);
    POCAT_RUN_TEST(test_process_non_exit_bits_do_not_change_state);
    POCAT_RUN_TEST(test_process_drains_notification_in_one_cycle);
    POCAT_RUN_TEST(test_exit_to_current_state_is_a_no_op);
    return POCAT_END();
}
