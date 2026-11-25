/**
 * @file test_subsystem_tasks.c
 * @brief Host tests for the contract shared by the EPS, ADCS and PAYLOAD tasks, run through their real `process_*()` functions.
 * @date 2026-10-29
 *
 * @details
 * **What is under test**
 * - src/subsystems/eps/eps.c: `setup_eps()` / `process_eps()`.
 * - src/subsystems/adcs/adcs.c: `setup_adcs()` / `process_adcs()`.
 * - src/subsystems/payloads/payload.c: `setup_payload()` / `process_payload()`.
 *
 * The three tasks follow the same contract (see task_management.h):
 * 1. wait up to 1 s for a notification (this sets the health-kick cadence);
 * 2. hand it to `tm_check_pause()`: PAUSE makes the task ACK and stop,
 *    RESUME makes it ACK and continue, and any other bit is **deferred**
 *    while the task is paused;
 * 3. after a resume, run the deferred bits.
 *
 * The existing pause tests (`sim_tasks`) only call `tm_check_pause()`
 * directly. Here the *real* `process_*()` functions run, so a task that
 * forgot to call `tm_check_pause()`, or that dropped its deferred word,
 * would be caught.
 *
 * Also covered: EPS reads its three thresholds from flash on
 * `N_EPS_NEW_THRESHOLDS`, and the ADCS private mode macros (KI-6).
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `eps.c`, `adcs.c`, `payload.c`, `task_management.c` and `health.c` are
 *   `#include`d into this one translation unit. Each of the three task files
 *   defines `static uint32_t deferred_notifications;`, so that name would
 *   clash. The macro trick below renames it per include
 *   (`eps_deferred`, `adcs_deferred`, `payload_deferred`) and restores it
 *   with `#undef`. The tests can then read each task's deferred word.
 * - The real `eps_task` / `adcs_task` / `payload_task` override the weak
 *   empty versions from `stub_task_entries.c`.
 * - SOURCES: `notifications.c`, `stub_task_entries.c`, `fake_obdh_requests.c`.
 *
 * **Fake setup**
 * - The tests make the subsystem task "current" (`run_as()`) and call its
 *   `process_*()`. The block hook of the pause handshake is `run_real_tasks()`
 *   (not `sim_tasks_run_pending`, which does not call the real functions).
 * - `fake_obdh_requests` serves EPS's threshold read from a RAM flash image.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_subsystem_tasks
 * docker run --rm -v "${PWD}:/app" pocat-tests test_subsystem_tasks -f contract
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_obdh_requests.h"
#include "pocat_test_main.h"

/* Each task file has its own `static uint32_t deferred_notifications;`: rename per include. */
#define deferred_notifications eps_deferred
#include "eps.c"
#undef  deferred_notifications
#define deferred_notifications adcs_deferred
#include "adcs.c"
#undef  deferred_notifications
#define deferred_notifications payload_deferred
#include "payload.c"
#undef  deferred_notifications
#include "task_management.c"
#include "health.c"

/** @brief Describes one task for the shared-contract tests. */
typedef struct {
    const char  *name;         /**< Task name for failure messages. */
    uint32_t     tm_bit;       /**< `TM_TASK_*` bit of the task. */
    EventBits_t  health_bit;   /**< Its `HEALTH_BIT_*`. */
    void       (*process)(void); /**< The real `process_<task>()`. */
    uint32_t    *deferred;     /**< The task's deferred-notification word. */
    uint32_t     own_bit;      /**< A notification bit of its own that has no flash side effect. */
} contract_t;

/** @brief The three tasks that follow the contract. */
static const contract_t contracts[] = {
    { "EPS",     TM_TASK_EPS,     HEALTH_BIT_EPS,     process_eps,     &eps_deferred,     N_EPS_ENABLE_AUTO_HEAT },
    { "ADCS",    TM_TASK_ADCS,    HEALTH_BIT_ADCS,    process_adcs,    &adcs_deferred,    N_ADCS_NEW_TLE },
    { "PAYLOAD", TM_TASK_PAYLOAD, HEALTH_BIT_PAYLOAD, process_payload, &payload_deferred, N_PAYLOAD_ACTIVATE },
};
/** @brief Number of entries of #contracts. */
#define N_CONTRACTS (sizeof(contracts) / sizeof(contracts[0]))

/**
 * @brief Helper: runs one cycle of the real `process_*()` of @p c as if its task were running.
 * @details Makes the task's handle current (so `wait_for_notification()` and
 * `tm_check_pause()` see the right task), calls `c->process()`, and makes the
 * previously current task current again.
 * @param c Task description.
 */
static void run_as(const contract_t *c)
{
    TaskHandle_t prev = fake_rtos_current_task();
    TaskHandle_t h = tm_get_task_handle(c->tm_bit);
    TEST_ASSERT_NOT_NULL_MESSAGE(h, c->name);
    fake_rtos_set_current_task(h);
    c->process();
    fake_rtos_set_current_task(prev);
}

/**
 * @brief Block hook: lets each of the three real tasks that has a pending notification run one cycle.
 * @details It checks `fake_rtos_notify_pending()` first. Without that check
 * `process_*()` would wait 1000 ticks, time would jump and a handshake would
 * look like a timeout.
 */
static void run_real_tasks(void)
{
    for (size_t i = 0; i < N_CONTRACTS; i++) {
        TaskHandle_t h = tm_get_task_handle(contracts[i].tm_bit);
        if (h != NULL && fake_rtos_notify_pending(h, 0)) {
            run_as(&contracts[i]);
        }
    }
}

/**
 * @brief Helper: sends notification @p bits (index 0, OR-ed in) to the task of @p c.
 * @param c    Task description.
 * @param bits Notification bits.
 */
static void send_to(const contract_t *c, uint32_t bits)
{
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskNotify(tm_get_task_handle(c->tm_bit), bits, eSetBits));
}

/**
 * @brief Helper: returns the `task_table[]` entry of @p c.
 * @param c Task description.
 * @return Pointer to the table entry.
 */
static TaskEntry_t *entry_of(const contract_t *c)
{
    return &task_table[__builtin_ctz(c->tm_bit)];
}

/**
 * @brief Runs before every test: fresh fakes, the 7 tasks created, and the three task modules in their boot state.
 * @details Resets the fake RTOS, HAL and OBDH flash image. Resets the statics
 * of task_management.c (handles, `paused`, ACK event group) and health.c
 * (event group, expected bits, period, deadline, IWDG handle), then
 * `health_init()`. Calls `setup_eps()`, `setup_adcs()` and `setup_payload()`,
 * which zero each deferred word. Finally `tm_create_tasks(TM_TASK_ALL, 0)`
 * creates the tasks (so handles, ACK bits and health bits exist). No block
 * hook is installed; tests that need one set it themselves.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    fake_obdh_reset();

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

    setup_eps();
    setup_adcs();
    setup_payload();

    TEST_ASSERT_EQUAL_INT(pdPASS, tm_create_tasks(TM_TASK_ALL, 0));
}

/**
 * @brief Runs after every test: checks that the RTOS fake is balanced.
 * @details An unclosed critical section or scheduler suspend would freeze the satellite.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/* =========================================================================
 * 1. The shared contract (EPS, ADCS, PAYLOAD)
 * ========================================================================= */

/**
 * @brief PAUSE makes the task mark itself paused, ACK, and leave health monitoring.
 * @details For each of the three tasks: send `N_TASK_PAUSE`, run one cycle.
 * Expected: `paused` is set, its ACK bit is set in the task event group and
 * its health bit is no longer expected. Matters because the OBC waits for
 * that ACK before changing the clock; no ACK means a 10 s timeout on every
 * state change.
 */
void test_contract_pause_acks_and_marks_paused(void)
{
    for (size_t i = 0; i < N_CONTRACTS; i++) {
        const contract_t *c = &contracts[i];
        send_to(c, N_TASK_PAUSE);
        run_as(c);
        TEST_ASSERT_TRUE_MESSAGE(entry_of(c)->paused, c->name);
        TEST_ASSERT_TRUE_MESSAGE((xEventGroupGetBits(task_event_group_handle) & entry_of(c)->ack_bit) != 0, c->name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, health_get_expected() & c->health_bit, c->name);
    }
}

/**
 * @brief While paused, a task defers its own notification bits instead of acting on them.
 * @details Pause, then send the task's own bit. Expected: the bit appears in
 * the task's deferred word and the task stays paused. Matters because a
 * telecommand that arrives during a state change must not be lost, nor
 * executed while the subsystem is supposed to be quiet.
 */
void test_contract_paused_task_defers_own_bits(void)
{
    for (size_t i = 0; i < N_CONTRACTS; i++) {
        const contract_t *c = &contracts[i];
        send_to(c, N_TASK_PAUSE);
        run_as(c);
        send_to(c, c->own_bit);
        run_as(c);
        TEST_ASSERT_TRUE_MESSAGE((*c->deferred & c->own_bit) != 0, c->name);
        TEST_ASSERT_TRUE_MESSAGE(entry_of(c)->paused, c->name);
    }
}

/**
 * @brief RESUME runs the deferred bits, clears the deferred word, restores health monitoring and ACKs.
 * @details Pause, defer one bit, clear the ACK bit, send RESUME. Expected:
 * not paused, deferred word 0, the health bit expected again, ACK bit set
 * again. Matters because a task that stays out of the expected set is never
 * supervised by the watchdog again.
 */
void test_contract_resume_clears_deferred_and_restores_health(void)
{
    for (size_t i = 0; i < N_CONTRACTS; i++) {
        const contract_t *c = &contracts[i];
        send_to(c, N_TASK_PAUSE);
        run_as(c);
        send_to(c, c->own_bit);
        run_as(c);
        xEventGroupClearBits(task_event_group_handle, entry_of(c)->ack_bit);

        send_to(c, N_TASK_RESUME);
        run_as(c);

        TEST_ASSERT_FALSE_MESSAGE(entry_of(c)->paused, c->name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0u, *c->deferred, c->name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(c->health_bit, health_get_expected() & c->health_bit, c->name);
        TEST_ASSERT_TRUE_MESSAGE((xEventGroupGetBits(task_event_group_handle) & entry_of(c)->ack_bit) != 0, c->name);
    }
}

/**
 * @brief With no notification, one cycle returns after 1 s.
 * @details Expected: the tick advances by exactly 1000 for each task. This
 * sets the health-kick cadence, which must stay well below the 5 s health
 * period configured by the OBC.
 */
void test_contract_idle_cycle_returns_after_1s(void)
{
    for (size_t i = 0; i < N_CONTRACTS; i++) {
        TickType_t t0 = xTaskGetTickCount();
        run_as(&contracts[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000u, (uint32_t)(xTaskGetTickCount() - t0), contracts[i].name);
    }
}

/**
 * @brief The OBC's pause/resume handshake completes against the real task functions.
 * @details Installs `run_real_tasks()` as the block hook, pauses EPS|ADCS|PAYLOAD
 * with `tm_pause_tasks()`, then resumes them. Expected: no ACK timeout
 * (tick stays below the 10 s limit), all three paused, then all three running.
 * This is the end-to-end proof that these tasks answer the OBC.
 */
void test_real_tasks_complete_pause_handshake(void)
{
    uint32_t mask = TM_TASK_EPS | TM_TASK_ADCS | TM_TASK_PAYLOAD;
    fake_rtos_set_block_hook(run_real_tasks);

    tm_pause_tasks(mask);
    TEST_ASSERT_TRUE(xTaskGetTickCount() < 10000u);
    for (size_t i = 0; i < N_CONTRACTS; i++) {
        TEST_ASSERT_TRUE_MESSAGE(entry_of(&contracts[i])->paused, contracts[i].name);
    }

    tm_resume_tasks(mask);
    TEST_ASSERT_TRUE(xTaskGetTickCount() < 10000u);
    for (size_t i = 0; i < N_CONTRACTS; i++) {
        TEST_ASSERT_FALSE_MESSAGE(entry_of(&contracts[i])->paused, contracts[i].name);
    }
}

/* =========================================================================
 * 2. EPS specifics
 * ========================================================================= */

/**
 * @brief `N_EPS_NEW_THRESHOLDS` makes EPS read its 3 threshold bytes from `EPS_THRESHOLDS_ADDR`.
 * @details Expected: exactly one READ request, at that address, 3 bytes.
 * Matters because the ground uploads the nominal / contingency / sun-safe
 * battery thresholds with a TC and EPS must pick up the stored values.
 */
void test_eps_new_thresholds_reads_3_bytes_from_flash(void)
{
    send_to(&contracts[0], N_EPS_NEW_THRESHOLDS);
    run_as(&contracts[0]);

    TEST_ASSERT_EQUAL_INT(1, fake_obdh_call_count());
    fake_obdh_call_t c = fake_obdh_call_at(0);
    TEST_ASSERT_EQUAL_INT(FAKE_OBDH_READ, c.op);
    TEST_ASSERT_EQUAL_HEX32(EPS_THRESHOLDS_ADDR, c.addr);
    TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)c.len);
}

/**
 * @brief A thresholds request that arrives while EPS is paused is served on resume, not before.
 * @details Pause EPS, send the thresholds bit: no flash call yet. Send
 * RESUME: the read happens in the resume cycle. Matters because thresholds
 * uploaded during a state change must not be lost.
 */
void test_eps_threshold_request_while_paused_is_served_on_resume(void)
{
    const contract_t *eps = &contracts[0];
    send_to(eps, N_TASK_PAUSE);
    run_as(eps);

    send_to(eps, N_EPS_NEW_THRESHOLDS);
    run_as(eps);
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());

    send_to(eps, N_TASK_RESUME);
    run_as(eps);
    TEST_ASSERT_EQUAL_INT(1, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(FAKE_OBDH_READ, fake_obdh_call_at(0).op);
    TEST_ASSERT_EQUAL_HEX32(EPS_THRESHOLDS_ADDR, fake_obdh_call_at(0).addr);
}

/**
 * @brief The heater enable / disable bits do not touch flash.
 * @details Expected: no OBDH request. Documents that the heater logic is
 * still a TODO in eps.c: a future implementation that persists the setting
 * must update this test on purpose.
 */
void test_eps_heater_bits_touch_no_flash(void)
{
    send_to(&contracts[0], N_EPS_ENABLE_AUTO_HEAT | N_EPS_DISABLE_AUTO_HEAT);
    run_as(&contracts[0]);
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
}

/**
 * @brief A failed threshold read does not crash EPS and the cycle completes.
 * @details Injects `HAL_ERROR` into every OBDH request. Expected: the read
 * is attempted once and `process_eps()` returns normally. (The code has a TODO
 * for default thresholds; today it ignores the status and keeps zeros.)
 */
void test_eps_threshold_read_failure_does_not_crash(void)
{
    fake_obdh_inject_error(HAL_ERROR);
    send_to(&contracts[0], N_EPS_NEW_THRESHOLDS);
    run_as(&contracts[0]);
    TEST_ASSERT_EQUAL_INT(1, fake_obdh_call_count());
}

/* =========================================================================
 * 3. ADCS specifics
 * ========================================================================= */

/**
 * @brief The private ADCS mode macros should equal the shared notification bits.
 * @details Expected: `ADCS_DETUMBLING_MODE == N_ADCS_DESIRED_STATE_DETUMBLING`
 * and `ADCS_NADIR_POINTING_MODE == N_ADCS_DESIRED_STATE_NADIR`. Actual:
 * adcs.c uses bit 0 and bit 1, while notifications.h defines IDLE = bit 0,
 * DETUMBLING = bit 1, NADIR = bit 2. On the satellite an IDLE request would
 * start detumbling, a DETUMBLING request would start nadir pointing, and a
 * NADIR request would be ignored. ADCS also never handles
 * `N_ADCS_NEW_TLE` / `N_ADCS_NEW_CALIBRATION`.
 * @bug KI-6 (test/README.md) expected bits 1 and 2, actual bits 0 and 1.
 */
void test_adcs_private_mode_bits_match_notification_map(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-6: adcs.c private mode bits (0,1) do not match N_ADCS_DESIRED_STATE_* (1,2)");
    TEST_ASSERT_EQUAL_HEX32(N_ADCS_DESIRED_STATE_DETUMBLING, ADCS_DETUMBLING_MODE);
    TEST_ASSERT_EQUAL_HEX32(N_ADCS_DESIRED_STATE_NADIR, ADCS_NADIR_POINTING_MODE);
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
    POCAT_RUN_TEST(test_contract_pause_acks_and_marks_paused);
    POCAT_RUN_TEST(test_contract_paused_task_defers_own_bits);
    POCAT_RUN_TEST(test_contract_resume_clears_deferred_and_restores_health);
    POCAT_RUN_TEST(test_contract_idle_cycle_returns_after_1s);
    POCAT_RUN_TEST(test_real_tasks_complete_pause_handshake);
    POCAT_RUN_TEST(test_eps_new_thresholds_reads_3_bytes_from_flash);
    POCAT_RUN_TEST(test_eps_threshold_request_while_paused_is_served_on_resume);
    POCAT_RUN_TEST(test_eps_heater_bits_touch_no_flash);
    POCAT_RUN_TEST(test_eps_threshold_read_failure_does_not_crash);
    POCAT_RUN_TEST(test_adcs_private_mode_bits_match_notification_map);
    return POCAT_END();
}
