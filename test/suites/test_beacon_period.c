/**
 * @file test_beacon_period.c
 * @brief Host tests for the beacon period: refused outside its limits, applied only once stored, and the one timer that runs with it.
 * @date 2026-11-24
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/beacon_period.c: beacon_period_set_request(),
 *   beacon_period_stored_s() and beacon_period_apply() (spec FLS-RATE-010,
 *   flash plan step 5b).
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - beacon_period.c is `#include`d below, so setUp() can forget the timer of
 *   the previous test, which `fake_rtos_reset()` frees.
 * - The real parameter store runs over the fake flash driver.
 *   `fake_obdh_requests.c` serves its writes at once, as the OBDH task, logs
 *   them with their block time and can make them fail. The suite's own store
 *   calls (load, raw write) run as main() does, before the scheduler starts:
 *   the store accepts only OBDH, or main() then (spec FLS-REQ-110).
 * - tm_get_task_handle() is faked below: it returns a beacon task created in
 *   the fake RTOS, or NULL while a test says that the task is not running,
 *   and an OBDH task.
 * - The fake timers keep their period, whether they run and whether they
 *   repeat, and refuse a period of 0 as FreeRTOS does. 1 tick = 1 ms.
 *
 * Not covered here: the telecommand (tc_handler.c, which turns the results
 * into NACKs) and the beacon task's loop (beacon.c), which are only compiled.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_beacon_period
 * docker run --rm -v "${PWD}:/app" pocat-tests test_beacon_period -f timer
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_obdh_requests.h"
#include "fake_flash_driver.h"
#include "pocat_test_main.h"

#include "beacon_period.c"

/* ---- Fake of the task manager: the beacon task and the OBDH task ---- */

static TaskHandle_t beacon_task_handle;   /**< What tm_get_task_handle(TM_TASK_BEACON) returns; NULL while the task is not running. */
static TaskHandle_t obdh_task_handle;     /**< What tm_get_task_handle(TM_TASK_OBDH) returns: the task that serves the store's requests. */

TaskHandle_t tm_get_task_handle(uint32_t task_bit)
{
    if (task_bit == TM_TASK_OBDH) return obdh_task_handle;
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(TM_TASK_BEACON, task_bit, "only the beacon and OBDH tasks are asked for");
    return beacon_task_handle;
}

static void beacon_task_stub(void *p) { (void)p; }
static void obdh_task_stub(void *p) { (void)p; }

/* ---- Helpers ---- */

/** @brief Rebuilds the store from flash as main() does at boot, before the scheduler starts. */
static void load_store_at_boot(void)
{
    fake_rtos_set_scheduler_state(taskSCHEDULER_NOT_STARTED);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_load());
    fake_rtos_set_scheduler_state(taskSCHEDULER_RUNNING);
}

static int notes_at_request;   /**< Notifications already sent when the store request went out; -1 before it. */

static void count_notes_at_request(void)
{
    notes_at_request = fake_rtos_notify_log_count();
}

/** @brief Writes the period's two bytes to the store directly, as a build with other limits, or a fault, could have left them (before the scheduler starts, where the store accepts the test). */
static void store_raw(uint16_t period_s)
{
    const uint8_t value[2] = { (uint8_t)period_s, (uint8_t)(period_s >> 8) };
    fake_rtos_set_scheduler_state(taskSCHEDULER_NOT_STARTED);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_write(PARAM_BEACON_PERIOD, value, sizeof(value)));
    fake_rtos_set_scheduler_state(taskSCHEDULER_RUNNING);
}

/**
 * @brief Runs before every test: erased flash, the store at its defaults, no timer, a running beacon task and an OBDH task.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_obdh_reset();
    fake_flashdrv_reset();
    load_store_at_boot();
    timer = NULL;   /* fake_rtos_reset() freed it */

    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(beacon_task_stub, "BEACON", 128, NULL, 4, &beacon_task_handle));
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(obdh_task_stub, "OBDH", 128, NULL, 5, &obdh_task_handle));
    notes_at_request = -1;
    fake_obdh_set_request_hook(count_notes_at_request);
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_critical_depth());
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_suspend_all_depth());
}

/* =========================================================================
 * 1. A new period (TC_SET_BEACON_PERIOD)
 * ========================================================================= */

/**
 * @brief A period inside the limits is stored, little-endian, with the caller's block time; only then is the beacon task told.
 * @details The beacon task applies a period when told; telling it before
 *          the store has the value would apply the old one.
 */
void test_a_period_inside_the_limits_is_stored_then_the_beacon_task_told(void)
{
    TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_STORED, beacon_period_set_request(600, 77));

    uint8_t value[2];
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_BEACON_PERIOD, value, sizeof(value)));
    TEST_ASSERT_EQUAL_HEX8(0x58, value[0]);   /* 600 = 0x0258 */
    TEST_ASSERT_EQUAL_HEX8(0x02, value[1]);

    TEST_ASSERT_EQUAL_INT(1, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_HEX8(PARAM_BEACON_PERIOD, fake_obdh_call_at(0).id);
    TEST_ASSERT_EQUAL_UINT32(2, fake_obdh_call_at(0).len);
    TEST_ASSERT_EQUAL_UINT32(77, fake_obdh_call_at(0).ticks);

    TEST_ASSERT_EQUAL_INT(0, notes_at_request);
    TEST_ASSERT_EQUAL_INT(1, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_HEX32(N_COMMS_NEW_BEACON_PERIOD, fake_rtos_bits_sent_to(beacon_task_handle));
}

/** @brief The limits themselves, 10 s and 3,600 s, are periods a telecommand may set. */
void test_both_limits_are_accepted(void)
{
    TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_STORED, beacon_period_set_request(10, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_UINT16(10, beacon_period_stored_s());
    TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_STORED, beacon_period_set_request(3600, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_UINT16(3600, beacon_period_stored_s());
    TEST_ASSERT_EQUAL_INT(2, fake_rtos_notify_log_count());
}

/**
 * @brief A period outside the limits is refused before anything is sent: nothing stored, the beacon task not told.
 * @details Before step 5b any period was applied: 1 s beacons, or 0, which
 *          stops FreeRTOS on a configASSERT (F9).
 */
void test_a_period_outside_the_limits_is_refused_and_changes_nothing(void)
{
    const uint16_t refused[] = { 0u, 1u, 9u, 3601u, 0xFFFFu };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_OUT_OF_RANGE, beacon_period_set_request(refused[i], OBDH_REQUEST_WAIT));
    }
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_UINT16(30, beacon_period_stored_s());
}

/**
 * @brief A period the store did not take changes nothing: the beacon task is not told, and the stored period stays.
 * @details The store refused it (HAL_ERROR), could not queue it (HAL_BUSY) or
 *          did not answer in time (HAL_TIMEOUT). The telecommand gets NACK_BUSY
 *          and the ground sends it again.
 */
void test_a_period_the_store_did_not_take_changes_nothing(void)
{
    const HAL_StatusTypeDef failures[] = { HAL_ERROR, HAL_BUSY, HAL_TIMEOUT };
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        fake_obdh_inject_error(failures[i]);
        TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_NOT_STORED, beacon_period_set_request(600, OBDH_REQUEST_WAIT));
    }
    TEST_ASSERT_EQUAL_INT(3, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_UINT16(30, beacon_period_stored_s());
}

/**
 * @brief The same period again is stored without a new write and applied again.
 * @details If the store answered after the block time, the first telecommand
 *          got NACK_BUSY although OBDH stored the period. The ground's repeat
 *          must still make the beacon task apply it.
 */
void test_the_same_period_again_is_applied_again(void)
{
    TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_STORED, beacon_period_set_request(600, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_STORED, beacon_period_set_request(600, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(2, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_UINT16(600, beacon_period_stored_s());
}

/**
 * @brief While the beacon task is not running (being restarted) nobody is told; the period is stored, for the task's start.
 */
void test_a_beacon_task_not_running_is_not_told(void)
{
    beacon_task_handle = NULL;
    TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_STORED, beacon_period_set_request(600, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_UINT16(600, beacon_period_stored_s());
}

/* =========================================================================
 * 2. The stored period
 * ========================================================================= */

/** @brief Never set, the period is the default of PARAM_TABLE, 30 s, which BEACON_PERIOD_DEFAULT_S must equal. */
void test_the_period_is_30_s_until_one_is_stored(void)
{
    TEST_ASSERT_EQUAL_UINT16(30, beacon_period_stored_s());
    TEST_ASSERT_EQUAL_UINT16(30, BEACON_PERIOD_DEFAULT_S);
}

/**
 * @brief A stored period outside the limits reads as the default; the limits themselves read as stored.
 * @details Only a build with other limits, or a fault, can leave such a
 *          value: the owner checks what it reads (spec §5.4).
 */
void test_a_stored_period_outside_the_limits_reads_as_the_default(void)
{
    const uint16_t outside[] = { 0u, 9u, 3601u, 0xFFFFu };
    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); i++) {
        store_raw(outside[i]);
        TEST_ASSERT_EQUAL_UINT16(30, beacon_period_stored_s());
    }
    store_raw(10);
    TEST_ASSERT_EQUAL_UINT16(10, beacon_period_stored_s());
    store_raw(3600);
    TEST_ASSERT_EQUAL_UINT16(3600, beacon_period_stored_s());
}

/* =========================================================================
 * 3. The beacon timer
 * ========================================================================= */

/** @brief The first apply creates one repeating timer and starts it with the stored period: 30 s on a new satellite. */
void test_the_first_apply_starts_a_repeating_timer_with_the_stored_period(void)
{
    beacon_period_apply();

    TEST_ASSERT_NOT_NULL(timer);
    TEST_ASSERT_EQUAL_UINT32(30000, xTimerGetPeriod(timer));
    TEST_ASSERT_TRUE(xTimerIsTimerActive(timer));
    TEST_ASSERT_EQUAL_UINT32(pdTRUE, uxTimerGetReloadMode(timer));
}

/** @brief After a reboot the beacon starts with the period stored before it, not the default. */
void test_after_a_reboot_the_timer_starts_with_the_stored_period(void)
{
    TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_STORED, beacon_period_set_request(1234, OBDH_REQUEST_WAIT));
    load_store_at_boot();   /* the next boot reads the store again */

    beacon_period_apply();
    TEST_ASSERT_EQUAL_UINT32(1234000, xTimerGetPeriod(timer));
}

/** @brief A new period changes the period of the same timer; the longest one, 3,600 s, fits the tick counter. */
void test_a_new_period_changes_the_same_timer(void)
{
    beacon_period_apply();
    TimerHandle_t first = timer;

    TEST_ASSERT_EQUAL_INT(BEACON_PERIOD_STORED, beacon_period_set_request(3600, OBDH_REQUEST_WAIT));
    beacon_period_apply();
    TEST_ASSERT_EQUAL_PTR(first, timer);
    TEST_ASSERT_EQUAL_UINT32(3600000, xTimerGetPeriod(timer));
    TEST_ASSERT_TRUE(xTimerIsTimerActive(timer));
}

/**
 * @brief A beacon task restarted by the health monitor keeps the one timer, which outlives it, and sets its period from the store again.
 * @details Before step 5b each start of the task created a new timer and the
 *          old one kept running, so after a restart beacons came twice as often.
 */
void test_a_restarted_beacon_task_keeps_the_one_timer(void)
{
    beacon_period_apply();
    TimerHandle_t first = timer;

    store_raw(600);
    beacon_period_apply();   /* the restarted task starts again */
    TEST_ASSERT_EQUAL_PTR(first, timer);
    TEST_ASSERT_EQUAL_UINT32(600000, xTimerGetPeriod(timer));
}

/** @brief A stored period outside the limits runs the timer at the default: a period of 0 never reaches FreeRTOS. */
void test_a_stored_period_outside_the_limits_runs_the_timer_at_the_default(void)
{
    store_raw(0);
    beacon_period_apply();
    TEST_ASSERT_EQUAL_UINT32(30000, xTimerGetPeriod(timer));
}

/** @brief When the timer expires it tells the beacon task to transmit; while the task is not running, it tells nobody. */
void test_the_timer_tells_the_beacon_task_to_transmit(void)
{
    beacon_period_apply();
    fake_rtos_fire_timer(timer);
    TEST_ASSERT_EQUAL_INT(1, fake_rtos_notify_log_count());
    TEST_ASSERT_EQUAL_HEX32(N_COMMS_TRANSMIT_BEACON, fake_rtos_bits_sent_to(beacon_task_handle));

    beacon_task_handle = NULL;
    fake_rtos_fire_timer(timer);
    TEST_ASSERT_EQUAL_INT(1, fake_rtos_notify_log_count());
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();

    // 1. A new period (TC_SET_BEACON_PERIOD)
    POCAT_RUN_TEST(test_a_period_inside_the_limits_is_stored_then_the_beacon_task_told);
    POCAT_RUN_TEST(test_both_limits_are_accepted);
    POCAT_RUN_TEST(test_a_period_outside_the_limits_is_refused_and_changes_nothing);
    POCAT_RUN_TEST(test_a_period_the_store_did_not_take_changes_nothing);
    POCAT_RUN_TEST(test_the_same_period_again_is_applied_again);
    POCAT_RUN_TEST(test_a_beacon_task_not_running_is_not_told);

    // 2. The stored period
    POCAT_RUN_TEST(test_the_period_is_30_s_until_one_is_stored);
    POCAT_RUN_TEST(test_a_stored_period_outside_the_limits_reads_as_the_default);

    // 3. The beacon timer
    POCAT_RUN_TEST(test_the_first_apply_starts_a_repeating_timer_with_the_stored_period);
    POCAT_RUN_TEST(test_after_a_reboot_the_timer_starts_with_the_stored_period);
    POCAT_RUN_TEST(test_a_new_period_changes_the_same_timer);
    POCAT_RUN_TEST(test_a_restarted_beacon_task_keeps_the_one_timer);
    POCAT_RUN_TEST(test_a_stored_period_outside_the_limits_runs_the_timer_at_the_default);
    POCAT_RUN_TEST(test_the_timer_tells_the_beacon_task_to_transmit);

    return POCAT_END();
}
