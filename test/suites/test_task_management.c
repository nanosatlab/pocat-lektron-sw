/**
 * @file test_task_management.c
 * @brief Host tests for the OBC task manager: task table, pause/resume handshake and health-driven reset.
 * @date 2026-10-23
 *
 * @details
 * **What is under test**
 * - src/subsystems/obc/task_management.c:
 *   - `tm_create_tasks()`
 *   - `tm_pause_tasks()` / `tm_resume_tasks()`
 *   - `tm_check_pause()`
 *   - `tm_handle_health_faults()` and `tm_reset_task()`
 *   - `tm_get_task_handle()`
 * - The parts of src/utils/health.c that task_management drives (which tasks
 *   are expected to report health).
 * - That only the OBC task creates, pauses, resumes or resets tasks, and runs
 *   `health_check()`, `health_config()` and `health_register_iwdg()`
 *   (`obc_is_caller()`).
 *
 * **The pause/resume handshake in short**
 * 1. The OBC sends `N_TASK_PAUSE` to the selected tasks.
 * 2. Each task sees it in its loop, calls `tm_check_pause()`, and answers by
 *    setting its ACK bit in an event group.
 * 3. The OBC waits up to `TM_PAUSE_ACK_TIMEOUT_MS` (10 s) for all the ACKs,
 *    then marks those tasks paused and removes them from health monitoring.
 *
 * Resume works the same way with `N_TASK_RESUME`.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `task_management.c` and `health.c` are `#include`d below, so the tests can
 *   read and reset their `static` variables (`task_table[]`,
 *   `task_event_group_handle`, `expected_bits`, ...).
 * - SOURCES: `notifications.c`, `stub_task_entries.c` and `sim_tasks.c`.
 *   `stub_task_entries.c` provides empty `eps_task()`, `comms_task()`, ...,
 *   because the task table points at them. They are never run.
 *
 * **Fake setup**
 * - The test task plays the OBC task: `main_get_obc_handle()` returns it.
 * - `xTaskCreate` only records tasks. Nothing runs in parallel.
 * - Block hook = `sim_tasks_run_pending()`. When `tm_pause_tasks()` waits for
 *   ACKs, the fake RTOS calls this hook first. The hook plays the part of each
 *   task that has a pending notification: it makes that task current and runs
 *   its `tm_check_pause()` once, which sends the ACK.
 * - `sim_tasks_set_unresponsive()` simulates a stuck task that never answers.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_task_management
 * docker run --rm -v "${PWD}:/app" pocat-tests test_task_management -f pause
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "sim_tasks.h"
#include "stub_task_entries.h"
#include "pocat_test_main.h"

#include "task_management.c"
#include "health.c"

/**
 * @brief Runs before every test: fresh fakes, a reset task manager, and the test task as the OBC task.
 * @details
 * 1. Resets the fake RTOS, the fake HAL and the simulated tasks.
 * 2. Resets the module statics by hand. `tm_create_tasks()` and
 *    `health_init()` assume a fresh boot, and stale handles would point at
 *    tasks freed by `fake_rtos_reset()`:
 *    - task_management.c: every table handle and `paused` flag, plus the ACK
 *      event group;
 *    - health.c: event group, expected bits, period, deadline and watchdog
 *      handle. Then `health_init()` runs again, as at boot.
 * 3. Installs `sim_tasks_run_pending()` as the block hook, so pause/resume
 *    handshakes complete as they would on the satellite.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
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
    stub_set_obc_handle(fake_rtos_test_task());

    fake_rtos_set_block_hook(sim_tasks_run_pending);
}

/**
 * @brief Runs after every test: checks that the code left the RTOS balanced.
 * @details An unclosed critical section or scheduler suspend would freeze
 * the real satellite.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/* =========================================================================
 * 1. Task creation and the task table
 * ========================================================================= */

/**
 * @brief `tm_create_tasks(TM_TASK_ALL, 0)` creates the 7 subsystem tasks, each under its own name.
 * @details Expected: the call returns `pdPASS`, exactly 7 tasks exist, and
 * each name can be found. The names matter because they appear in debuggers
 * and FreeRTOS traces.
 */
void test_create_tasks_creates_all_seven_named_tasks(void)
{
    TEST_ASSERT_EQUAL_INT(pdPASS, tm_create_tasks(TM_TASK_ALL, 0));
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());

    const char *names[] = { "PAYLOAD", "EPS", "COMMS", "ADCS", "OBDH", "TRANSCEIVER", "BEACON" };
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(fake_rtos_find_task(names[i]), names[i]);
    }
}

/**
 * @brief Each task's FreeRTOS "task number" equals its index in `task_table[]`.
 * @details `tm_check_pause()` runs inside the task. It finds its own table
 * entry, and so its ACK bit, through `uxTaskGetTaskNumber()`. A wrong number
 * would make a task ACK, or pause, as a different task. The test also checks
 * that `tm_get_task_handle(1u << i)` returns the handle stored in the table.
 */
void test_task_number_equals_table_index(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    for (uint32_t i = 0; i < 7; i++) {
        TaskHandle_t h = tm_get_task_handle(1u << i);
        TEST_ASSERT_NOT_NULL(h);
        TEST_ASSERT_EQUAL_UINT32(i, (uint32_t)uxTaskGetTaskNumber(h));
        TEST_ASSERT_EQUAL_PTR(task_table[i].handle, h);
    }
}

/**
 * @brief Each row of `task_table[]` holds the task its `TM_IDX_*` index names.
 * @details Each row's `[TM_IDX_*]` designator places it, so the order of the
 * lines cannot break this. What can is a row left out (it stays all zeros,
 * with no name) or a row written under another task's index. Checks:
 * - the table has 7 rows;
 * - row i has ACK bit `1u << i`, the order of the `EV_*_ACK` bits in events.h;
 * - row `TM_IDX_X` has the name of task X.
 */
void test_table_ack_bits_match_tm_bits(void)
{
    TEST_ASSERT_EQUAL_UINT(7, TASK_TABLE_SIZE);
    for (uint32_t i = 0; i < TASK_TABLE_SIZE; i++) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(1u << i, task_table[i].ack_bit, "ACK bit is not 1<<index");
    }
    TEST_ASSERT_EQUAL_STRING("PAYLOAD", task_table[TM_IDX_PAYLOAD].name);
    TEST_ASSERT_EQUAL_STRING("EPS", task_table[TM_IDX_EPS].name);
    TEST_ASSERT_EQUAL_STRING("COMMS", task_table[TM_IDX_COMMS].name);
    TEST_ASSERT_EQUAL_STRING("ADCS", task_table[TM_IDX_ADCS].name);
    TEST_ASSERT_EQUAL_STRING("OBDH", task_table[TM_IDX_OBDH].name);
    TEST_ASSERT_EQUAL_STRING("TRANSCEIVER", task_table[TM_IDX_TRANSCEIVER].name);
    TEST_ASSERT_EQUAL_STRING("BEACON", task_table[TM_IDX_BEACON].name);
}

/**
 * @brief Only tasks created as running are expected to report health; tasks created paused are not.
 * @details The health monitor raises a fault for any expected task that stops
 * reporting. If paused tasks were expected too, every state with paused tasks
 * would trigger false faults and task resets.
 * - Arrange: create EPS and COMMS as running, everything else as paused.
 * - Expected: the expected health bits are exactly EPS and COMMS, EPS is not
 *   paused, and PAYLOAD is paused.
 */
void test_health_expected_only_for_running_tasks(void)
{
    tm_create_tasks(TM_TASK_EPS | TM_TASK_COMMS, TM_TASK_ALL & ~(TM_TASK_EPS | TM_TASK_COMMS));

    TEST_ASSERT_EQUAL_HEX32(HEALTH_BIT_EPS | HEALTH_BIT_COMMS, health_get_expected());
    TEST_ASSERT_FALSE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_TRUE(task_table[TM_IDX_PAYLOAD].paused);
}

/**
 * @brief When `xTaskCreate` fails, `tm_create_tasks()` reports failure and stops creating tasks.
 * @details
 * - Arrange: make the 3rd `xTaskCreate` fail. Tasks are created in bit
 *   order, so PAYLOAD and EPS succeed and COMMS fails.
 * - Expected: the result is not `pdPASS`, only 2 tasks exist, and the COMMS
 *   handle stays NULL.
 * @note `tm_create_tasks()` returns the `xTaskCreate` error code as is. On
 * real FreeRTOS that code is `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` (-1), not
 * `pdFAIL` (0) as its doc comment says (KI-9 in test/README.md). Callers only
 * compare the result with `pdPASS`, so that is what the test asserts.
 */
void test_create_failure_returns_pdFAIL(void)
{
    fake_rtos_fail_task_create_at(3);   /* creation order is by bit: PAYLOAD, EPS, then COMMS fails */
    TEST_ASSERT_NOT_EQUAL_INT(pdPASS, tm_create_tasks(TM_TASK_ALL, 0));
    TEST_ASSERT_EQUAL_INT(2, fake_rtos_tasks_created());
    TEST_ASSERT_NULL(tm_get_task_handle(TM_TASK_COMMS));
}

/**
 * @brief If the ACK event group can't be created, no task is created and the result is `pdFAIL`.
 * @details Without the event group the pause/resume handshake can't work, so
 * creating tasks would be pointless. The check comes first, before any task
 * exists.
 */
void test_create_failure_when_event_group_fails_returns_pdFAIL(void)
{
    fake_rtos_fail_next_event_group_create();
    TEST_ASSERT_EQUAL_INT(pdFAIL, tm_create_tasks(TM_TASK_ALL, 0));
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_tasks_created());
}

/* =========================================================================
 * 2. Pause / resume handshake
 * ========================================================================= */

/**
 * @brief A pause request reaches only the tasks in the mask.
 * @details `tm_pause_tasks(EPS | ADCS)` must send `N_TASK_PAUSE` to EPS and
 * ADCS and send nothing to the other five. The fake keeps, for every task,
 * the OR of all the bits it was sent, and the test reads that record.
 */
void test_pause_notifies_only_selected_tasks(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    tm_pause_tasks(TM_TASK_EPS | TM_TASK_ADCS);

    for (uint32_t i = 0; i < 7; i++) {
        uint32_t expected = (i == TM_IDX_EPS || i == TM_IDX_ADCS) ? N_TASK_PAUSE : 0u;
        TEST_ASSERT_EQUAL_HEX32(expected, fake_rtos_bits_sent_to(tm_get_task_handle(1u << i)));
    }
}

/**
 * @brief A completed pause marks the task paused, removes it from health monitoring, and consumes its ACK.
 * @details
 * - Arrange: create all tasks; EPS is expected to report health.
 * - Act: `tm_pause_tasks(EPS)`. The block hook lets EPS answer the pause.
 * - Expected:
 *   - EPS is paused and COMMS isn't;
 *   - EPS is no longer an expected health reporter, while COMMS still is;
 *   - the ACK event group is back at 0, so a stale ACK can't satisfy the
 *     next handshake early.
 */
void test_pause_handshake_marks_paused_and_drops_health_bit(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    TEST_ASSERT_BITS_HIGH(HEALTH_BIT_EPS, health_get_expected());

    tm_pause_tasks(TM_TASK_EPS);

    TEST_ASSERT_TRUE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_FALSE(task_table[TM_IDX_COMMS].paused);
    TEST_ASSERT_BITS_LOW(HEALTH_BIT_EPS, health_get_expected());
    TEST_ASSERT_BITS_HIGH(HEALTH_BIT_COMMS, health_get_expected());
    TEST_ASSERT_EQUAL_HEX32(0u, xEventGroupGetBits(task_event_group_handle));
}

/**
 * @brief Resuming a paused task makes it run again and restores health monitoring.
 * @details Pause EPS and then resume it. Expected: `paused` is false, the EPS
 * health bit is expected again, and no ACK bit is left in the event group.
 */
void test_resume_handshake_restores_health_bit(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    tm_pause_tasks(TM_TASK_EPS);
    tm_resume_tasks(TM_TASK_EPS);

    TEST_ASSERT_FALSE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_BITS_HIGH(HEALTH_BIT_EPS, health_get_expected());
    TEST_ASSERT_EQUAL_HEX32(0u, xEventGroupGetBits(task_event_group_handle));
}

/**
 * @brief A paused task keeps (defers) the commands it receives instead of losing them.
 * @details A telecommand can arrive just before or during a state change. The
 * task must not act on it while paused, but must not drop it either.
 * `tm_check_pause()` stores it in the task's `deferred` word, which sim_tasks
 * keeps per task.
 * 1. `N_EPS_NEW_THRESHOLDS` arrives in the same notification value as the
 *    PAUSE. EPS pauses and defers it.
 * 2. `N_EPS_ENABLE_AUTO_HEAT` arrives while EPS is paused. EPS stays paused
 *    and defers it as well.
 * 3. EPS is resumed. Both deferred bits are still there for the task to
 *    handle now.
 */
void test_paused_task_defers_other_notifications(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    TaskHandle_t eps = tm_get_task_handle(TM_TASK_EPS);

    /* One notification arrives in the same value as the PAUSE. */
    xTaskNotify(eps, N_EPS_NEW_THRESHOLDS, eSetBits);
    tm_pause_tasks(TM_TASK_EPS);
    TEST_ASSERT_TRUE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_EQUAL_HEX32(N_EPS_NEW_THRESHOLDS, sim_tasks_deferred(TM_TASK_EPS));

    /* Another arrives while paused: the task stays paused and defers it too. */
    xTaskNotify(eps, N_EPS_ENABLE_AUTO_HEAT, eSetBits);
    sim_tasks_run_pending();
    TEST_ASSERT_TRUE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_EQUAL_HEX32(N_EPS_NEW_THRESHOLDS | N_EPS_ENABLE_AUTO_HEAT, sim_tasks_deferred(TM_TASK_EPS));

    /* After resume the deferred bits are still there for the task to process. */
    tm_resume_tasks(TM_TASK_EPS);
    TEST_ASSERT_FALSE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_EQUAL_HEX32(N_EPS_NEW_THRESHOLDS | N_EPS_ENABLE_AUTO_HEAT, sim_tasks_deferred(TM_TASK_EPS));
}

/**
 * @brief A task that never answers makes the pause wait the full 10 s ACK timeout, and then the OBC carries on.
 * @details
 * - Arrange: EPS is stuck (`sim_tasks_set_unresponsive`); COMMS works.
 * - Act: `tm_pause_tasks(EPS | COMMS)`.
 * - Expected:
 *   - the fake clock advanced by exactly `TM_PAUSE_ACK_TIMEOUT_MS`;
 *   - COMMS, which answered, is paused;
 *   - EPS is not marked paused and is still expected to report health, so
 *     the health monitor can detect the stuck task and reset it.
 */
void test_unresponsive_task_times_out_and_pause_returns(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    sim_tasks_set_unresponsive(TM_TASK_EPS);
    TickType_t before = xTaskGetTickCount();

    uint32_t missing = tm_pause_tasks(TM_TASK_EPS | TM_TASK_COMMS);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(TM_TASK_EPS, missing, "the pause must report the task that did not acknowledge");
    TEST_ASSERT_EQUAL_UINT32(before + TM_PAUSE_ACK_TIMEOUT_MS, xTaskGetTickCount());
    TEST_ASSERT_TRUE(task_table[TM_IDX_COMMS].paused);
    TEST_ASSERT_FALSE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_BITS_HIGH(HEALTH_BIT_EPS, health_get_expected());   /* the stuck task is still monitored */
}

/**
 * @brief If PAUSE and RESUME are pending together, the resume wins: the task runs and keeps its health bit (was KI-4).
 * @details This happens when a task is busy for longer than the 10 s ACK
 * timeout during a state change: the OBC gives up on the PAUSE and sends the
 * RESUME, so the task finds both bits in one notification value. If the pause
 * won, the task would stay paused forever, and since a paused task is not
 * monitored, the health check would never recover it.
 */
void test_pause_and_resume_pending_together_leaves_task_running(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    TaskHandle_t eps = tm_get_task_handle(TM_TASK_EPS);

    xTaskNotify(eps, N_TASK_PAUSE, eSetBits);
    xTaskNotify(eps, N_TASK_RESUME, eSetBits);
    sim_tasks_run_pending();

    TEST_ASSERT_FALSE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_BITS_HIGH(HEALTH_BIT_EPS, health_get_expected());
}

/**
 * @brief The acknowledgement of a resume is never taken for that of a pause (spec §5.2.1 rule 7).
 * @details A resume from an earlier, abandoned state change is still pending
 * in EPS when the OBC pauses it again. EPS finds both bits, the resume wins and
 * EPS keeps running. It acknowledges the resume, with the resume's own event
 * bit, so the pause reports EPS as not paused. With one shared bit the OBC
 * would believe EPS paused while it runs: for OBDH, that is a clock switch in
 * the middle of a flash operation (F17).
 */
void test_resume_ack_does_not_count_as_pause_ack(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    xTaskNotify(tm_get_task_handle(TM_TASK_EPS), N_TASK_RESUME, eSetBits);   /* left over */

    uint32_t missing = tm_pause_tasks(TM_TASK_EPS | TM_TASK_COMMS);

    TEST_ASSERT_EQUAL_HEX32(TM_TASK_EPS, missing);
    TEST_ASSERT_FALSE(task_table[TM_IDX_EPS].paused);
    TEST_ASSERT_TRUE(task_table[TM_IDX_COMMS].paused);
}

/**
 * @brief A running task acknowledges a resume too, and a pause then a resume both complete without a timeout.
 * @details The OBC resumes every task of the new state, including tasks that
 * were never paused. Each must answer at once, or the OBC would wait the full
 * 10 s ACK timeout for it.
 */
void test_running_task_acknowledges_resume(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    TickType_t before = xTaskGetTickCount();

    TEST_ASSERT_EQUAL_HEX32(0u, tm_resume_tasks(TM_TASK_ALL));
    TEST_ASSERT_EQUAL_HEX32(0u, tm_pause_tasks(TM_TASK_EPS));
    TEST_ASSERT_EQUAL_HEX32(0u, tm_resume_tasks(TM_TASK_EPS));

    TEST_ASSERT_EQUAL_UINT32(before, xTaskGetTickCount());
    TEST_ASSERT_FALSE(task_table[TM_IDX_EPS].paused);
}

/* =========================================================================
 * 3. Health-driven reset and handles
 * ========================================================================= */

/**
 * @brief A health fault on OBDH deletes and recreates only OBDH; EPS isn't touched.
 * @details Health bits and `TM_TASK_*` bits use different orders, so a
 * mapping mistake would reset the wrong task.
 * - Act: `tm_handle_health_faults(HEALTH_BIT_OBDH)`.
 * - Expected:
 *   - OBDH has a new, live handle, and the old handle is deleted;
 *   - EPS keeps the same live handle;
 *   - 8 tasks have been created in total (7 at boot plus the new OBDH);
 *   - the new OBDH task has task number 4 (its table index);
 *   - OBDH is expected to report health again.
 */
void test_health_fault_on_obdh_resets_obdh_not_eps(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    TaskHandle_t old_obdh = tm_get_task_handle(TM_TASK_OBDH);
    TaskHandle_t old_eps  = tm_get_task_handle(TM_TASK_EPS);

    tm_handle_health_faults(HEALTH_BIT_OBDH);

    TaskHandle_t new_obdh = tm_get_task_handle(TM_TASK_OBDH);
    TEST_ASSERT_NOT_NULL(new_obdh);
    TEST_ASSERT_NOT_EQUAL(old_obdh, new_obdh);
    TEST_ASSERT_FALSE(fake_rtos_task_alive(old_obdh));
    TEST_ASSERT_TRUE(fake_rtos_task_alive(new_obdh));
    TEST_ASSERT_EQUAL_PTR(old_eps, tm_get_task_handle(TM_TASK_EPS));
    TEST_ASSERT_TRUE(fake_rtos_task_alive(old_eps));
    TEST_ASSERT_EQUAL_INT(8, fake_rtos_tasks_created());
    TEST_ASSERT_EQUAL_UINT32(TM_IDX_OBDH, (uint32_t)uxTaskGetTaskNumber(new_obdh));
    TEST_ASSERT_BITS_HIGH(HEALTH_BIT_OBDH, health_get_expected());
}

/**
 * @brief `tm_reset_task()` deletes and recreates only the task it names.
 * @details Expected for EPS: a new live handle with EPS's task number, the old
 * handle deleted, COMMS untouched, and 8 tasks created in total.
 */
void test_reset_task_restarts_only_that_task(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    TaskHandle_t old_eps   = tm_get_task_handle(TM_TASK_EPS);
    TaskHandle_t old_comms = tm_get_task_handle(TM_TASK_COMMS);

    tm_reset_task(TM_TASK_EPS);

    TaskHandle_t new_eps = tm_get_task_handle(TM_TASK_EPS);
    TEST_ASSERT_NOT_EQUAL(old_eps, new_eps);
    TEST_ASSERT_FALSE(fake_rtos_task_alive(old_eps));
    TEST_ASSERT_TRUE(fake_rtos_task_alive(new_eps));
    TEST_ASSERT_EQUAL_UINT32(TM_IDX_EPS, (uint32_t)uxTaskGetTaskNumber(new_eps));
    TEST_ASSERT_EQUAL_PTR(old_comms, tm_get_task_handle(TM_TASK_COMMS));
    TEST_ASSERT_EQUAL_INT(8, fake_rtos_tasks_created());
}

/**
 * @brief A reset of a task whose re-creation failed changes nothing.
 * @details The failed re-creation leaves the row NULL. Suspending or deleting
 * a NULL handle would act on the caller, the OBC task itself (here the test
 * task). Expected: the second reset suspends, deletes and creates nothing.
 */
void test_reset_of_a_task_that_failed_to_restart_changes_nothing(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    fake_rtos_fail_task_create_at(1);   /* the next creation, the re-creation, fails */
    tm_reset_task(TM_TASK_COMMS);
    TEST_ASSERT_NULL(tm_get_task_handle(TM_TASK_COMMS));

    tm_reset_task(TM_TASK_COMMS);

    TEST_ASSERT_NULL(tm_get_task_handle(TM_TASK_COMMS));
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());
    TEST_ASSERT_FALSE(fake_rtos_task_suspended(fake_rtos_test_task()));
}

/**
 * @brief Before `tm_create_tasks()` runs, every task handle is NULL.
 * @details Code that notifies a task through `tm_get_task_handle()` before boot
 * has finished must get NULL, which it can check, never a garbage pointer.
 */
void test_get_task_handle_null_before_creation(void)
{
    for (uint32_t i = 0; i < 7; i++) {
        TEST_ASSERT_NULL(tm_get_task_handle(1u << i));
    }
}

/**
 * @brief `tm_current_index()` gives each task its table row, the OBC task `TM_OBC_INDEX`, and any other task -1; a restarted task keeps its row.
 * @details OBDH finds a client's reply queue by this index (spec §5.2.1 rule
 * 3), so it must survive a restart, and a task outside the table must not be
 * mistaken for PAYLOAD, whose task number 0 is also the default of every task.
 */
void test_current_index_identifies_clients(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    TaskHandle_t obc = NULL, stray = NULL;
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(NULL, "OBC", 256, NULL, 6, &obc));
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(NULL, "STRAY", 256, NULL, 1, &stray));
    stub_set_obc_handle(obc);

    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_PAYLOAD));
    TEST_ASSERT_EQUAL_INT(TM_IDX_PAYLOAD, tm_current_index());
    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_OBDH));
    TEST_ASSERT_EQUAL_INT(TM_IDX_OBDH, tm_current_index());
    fake_rtos_set_current_task(obc);
    TEST_ASSERT_EQUAL_INT((int)TM_OBC_INDEX, tm_current_index());
    fake_rtos_set_current_task(stray);
    TEST_ASSERT_EQUAL_INT(-1, tm_current_index());

    TaskHandle_t old_eps = tm_get_task_handle(TM_TASK_EPS);
    fake_rtos_set_current_task(obc);   /* only the OBC task restarts tasks */
    tm_handle_health_faults(HEALTH_BIT_EPS);
    TEST_ASSERT_NOT_EQUAL(old_eps, tm_get_task_handle(TM_TASK_EPS));
    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_EPS));
    TEST_ASSERT_EQUAL_INT(TM_IDX_EPS, tm_current_index());
    fake_rtos_set_current_task(NULL);
}

/* =========================================================================
 * 4. Only the OBC task manages the tasks and runs the health check
 * ========================================================================= */

/**
 * @brief Another task can create, pause, resume or reset no task.
 * @details COMMS calls the task manager itself instead of notifying the OBC.
 * Expected: the creation returns `pdFAIL` and creates nothing; the pause and
 * the resume notify nothing and report EPS as not acknowledged; the reset and
 * the health fault delete and create nothing, so EPS keeps its live handle
 * and runs.
 */
void test_other_task_cannot_manage_tasks(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    TaskHandle_t eps = tm_get_task_handle(TM_TASK_EPS);
    int notifications_before = fake_rtos_notify_log_count();

    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_COMMS));
    BaseType_t created = tm_create_tasks(TM_TASK_ALL, 0);
    uint32_t not_paused = tm_pause_tasks(TM_TASK_EPS);
    uint32_t not_resumed = tm_resume_tasks(TM_TASK_EPS);
    tm_reset_task(TM_TASK_EPS);
    tm_handle_health_faults(HEALTH_BIT_EPS);
    fake_rtos_set_current_task(NULL);

    TEST_ASSERT_EQUAL_INT(pdFAIL, created);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_EPS, not_paused);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_EPS, not_resumed);
    TEST_ASSERT_EQUAL_INT(7, fake_rtos_tasks_created());
    TEST_ASSERT_EQUAL_INT(notifications_before, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_PTR(eps, tm_get_task_handle(TM_TASK_EPS));
    TEST_ASSERT_TRUE(fake_rtos_task_alive(eps));
    TEST_ASSERT_FALSE(task_table[TM_IDX_EPS].paused);
}

/**
 * @brief Another task can neither run the health check nor change its period or watchdog handle.
 * @details Every task has reported and the period has elapsed, so a check
 * would pass and refresh the watchdog. COMMS calls `health_check()` first: it
 * gets 0, and neither the watchdog nor the period window changes. COMMS's
 * `health_config()` and `health_register_iwdg()` leave the period and the
 * handle as they were. The OBC's check that follows still finds the period
 * elapsed and refreshes the watchdog once.
 */
void test_other_task_cannot_run_the_health_check(void)
{
    tm_create_tasks(TM_TASK_ALL, 0);
    health_register_iwdg(&hiwdg);
    health_config(pdMS_TO_TICKS(HEALTH_PERIOD_MS));
    health_kick(health_get_expected());   /* every running task reports */
    fake_rtos_advance_ticks(pdMS_TO_TICKS(HEALTH_PERIOD_MS));

    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_COMMS));
    EventBits_t faults = health_check();
    health_config(1);
    health_register_iwdg(NULL);
    fake_rtos_set_current_task(NULL);

    TEST_ASSERT_EQUAL_HEX32(0u, faults);
    TEST_ASSERT_EQUAL_INT(0, fake_hal_iwdg_refresh_count());
    TEST_ASSERT_EQUAL_UINT32(pdMS_TO_TICKS(HEALTH_PERIOD_MS), period_ticks);
    TEST_ASSERT_EQUAL_PTR(&hiwdg, iwdg_handle);

    TEST_ASSERT_EQUAL_HEX32(0u, health_check());   /* the OBC's check */
    TEST_ASSERT_EQUAL_INT(1, fake_hal_iwdg_refresh_count());
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
    POCAT_RUN_TEST(test_create_tasks_creates_all_seven_named_tasks);
    POCAT_RUN_TEST(test_task_number_equals_table_index);
    POCAT_RUN_TEST(test_table_ack_bits_match_tm_bits);
    POCAT_RUN_TEST(test_health_expected_only_for_running_tasks);
    POCAT_RUN_TEST(test_create_failure_returns_pdFAIL);
    POCAT_RUN_TEST(test_create_failure_when_event_group_fails_returns_pdFAIL);
    POCAT_RUN_TEST(test_pause_notifies_only_selected_tasks);
    POCAT_RUN_TEST(test_pause_handshake_marks_paused_and_drops_health_bit);
    POCAT_RUN_TEST(test_resume_handshake_restores_health_bit);
    POCAT_RUN_TEST(test_paused_task_defers_other_notifications);
    POCAT_RUN_TEST(test_unresponsive_task_times_out_and_pause_returns);
    POCAT_RUN_TEST(test_pause_and_resume_pending_together_leaves_task_running);
    POCAT_RUN_TEST(test_resume_ack_does_not_count_as_pause_ack);
    POCAT_RUN_TEST(test_running_task_acknowledges_resume);
    POCAT_RUN_TEST(test_health_fault_on_obdh_resets_obdh_not_eps);
    POCAT_RUN_TEST(test_reset_task_restarts_only_that_task);
    POCAT_RUN_TEST(test_reset_of_a_task_that_failed_to_restart_changes_nothing);
    POCAT_RUN_TEST(test_get_task_handle_null_before_creation);
    POCAT_RUN_TEST(test_current_index_identifies_clients);
    POCAT_RUN_TEST(test_other_task_cannot_manage_tasks);
    POCAT_RUN_TEST(test_other_task_cannot_run_the_health_check);
    return POCAT_END();
}
