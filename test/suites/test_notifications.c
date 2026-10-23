/**
 * @file test_notifications.c
 * @brief Host tests for inter-task notifications, plus self-tests of the fake FreeRTOS.
 * @date 2026-10-23
 *
 * @details
 * **What is under test**
 * - `wait_for_notification()` (src/utils/notifications.c), the wrapper that every
 *   task uses to read its index-0 notification bits.
 * - The bit map in include/utils/notifications.h, include/utils/events.h and
 *   task_management.h. Those tests check constants. A wrong constant fails the
 *   test here, before it becomes a silent mis-routing on the satellite.
 * - The fake FreeRTOS itself (test/mocks/fake_rtos.c). Every other suite trusts
 *   the fake, so its queues, event groups and deadlock detection are checked here.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `notifications.c` is linked through SOURCES, not `#include`d, because it has
 *   no static state the tests need to see.
 * - Links `fake_rtos.c` and `fake_hal.c` like every suite that doesn't pass `NO_FAKES`.
 *
 * **Fake setup**
 * - The fake RTOS starts with one task, the "test task" (`fake_rtos_test_task()`).
 *   It is the current task, so `wait_for_notification()` reads its notifications.
 * - No block hook is installed. Every wait here uses timeout 0, so nothing blocks,
 *   except in `test_fake_detects_forever_block`, which blocks on purpose.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_notifications          # whole suite
 * docker run --rm -v "${PWD}:/app" pocat-tests test_notifications -l       # list tests
 * docker run --rm -v "${PWD}:/app" pocat-tests test_notifications -f fake  # only the fake self-tests
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "notifications.h"
#include "task_management.h"
#include "events.h"
#include "stm32l4xx_hal.h"
#include "pocat_test_main.h"

/**
 * @brief Runs before every test: gives each test a fresh fake RTOS.
 * @details Frees every queue and event group, clears all notification values
 * and the notify log, resets the tick to 0, and makes the test task current
 * again. Without this, bits left by one test would leak into the next one.
 */
void setUp(void)
{
    fake_rtos_reset();
}

/**
 * @brief Runs after every test: checks that the code left the RTOS balanced.
 * @details A `taskENTER_CRITICAL()` without its matching exit, or a
 * `vTaskSuspendAll()` without `xTaskResumeAll()`, would freeze the real
 * satellite. The fake counts both, so an imbalance fails the test here.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/* =========================================================================
 * 1. wait_for_notification()
 * ========================================================================= */

/**
 * @brief A non-blocking poll with nothing pending returns 0.
 * @details Tasks call `wait_for_notification(0)` in their main loop, and a 0
 * result means "nothing to do". It must not return stale or random bits.
 */
void test_wait_returns_zero_when_nothing_pending(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, wait_for_notification(0));
}

/**
 * @brief The function returns all pending bits and then clears them.
 * @details
 * - Arrange: send `N_OBC_UPDATE_TIME | N_OBC_HARD_REBOOT` to the test task.
 * - Act: call `wait_for_notification(0)` twice.
 * - Expected: the first call returns both bits; the second returns 0. A
 *   notification must be handled exactly once, never twice.
 */
void test_wait_returns_pending_bits_and_drains_them(void)
{
    xTaskNotify(fake_rtos_test_task(), N_OBC_UPDATE_TIME | N_OBC_HARD_REBOOT, eSetBits);
    TEST_ASSERT_EQUAL_HEX32(N_OBC_UPDATE_TIME | N_OBC_HARD_REBOOT, wait_for_notification(0));
    TEST_ASSERT_EQUAL_HEX32(0u, wait_for_notification(0));
}

/**
 * @brief Two `eSetBits` notifications sent before the task wakes are merged, not lost.
 * @details Two senders can notify a task before it runs, for example a TC
 * handler and the OBC. With `eSetBits` the values are OR-ed together, so the
 * task sees both requests in one read.
 */
void test_set_bits_notifications_accumulate(void)
{
    xTaskNotify(fake_rtos_test_task(), N_COMMS_NEW_CONFIG, eSetBits);
    xTaskNotify(fake_rtos_test_task(), N_COMMS_STOP_RF, eSetBits);
    TEST_ASSERT_EQUAL_HEX32(N_COMMS_NEW_CONFIG | N_COMMS_STOP_RF, wait_for_notification(0));
}

/**
 * @brief An OBDH flash completion (notification index 1) is invisible to `wait_for_notification()` (index 0).
 * @details OBDH replies to a flash request by writing the HAL status to index 1
 * (`OBDH_NOTIFY_IDX`). If that value leaked into index 0, a task would read a
 * HAL status such as `HAL_ERROR` (0x01) as notification bit 0 and act on a
 * command nobody sent.
 * - Arrange: write `HAL_ERROR` to index 1 of the test task.
 * - Expected: index 0 reads as 0 and has nothing pending; index 1 still holds
 *   `HAL_ERROR` for the code that is actually waiting for it.
 */
void test_obdh_index1_does_not_leak_into_index0(void)
{
    TaskHandle_t t = fake_rtos_test_task();
    xTaskNotifyIndexed(t, OBDH_NOTIFY_IDX, (uint32_t)HAL_ERROR, eSetValueWithOverwrite);

    TEST_ASSERT_EQUAL_HEX32(0u, wait_for_notification(0));
    TEST_ASSERT_TRUE(fake_rtos_notify_pending(t, OBDH_NOTIFY_IDX));
    TEST_ASSERT_EQUAL_HEX32((uint32_t)HAL_ERROR, fake_rtos_notify_value(t, OBDH_NOTIFY_IDX));
    TEST_ASSERT_FALSE(fake_rtos_notify_pending(t, 0));
}

/* =========================================================================
 * 2. Bit map consistency (notifications.h / events.h / task_management.h)
 * ========================================================================= */

/**
 * @brief The global PAUSE/RESUME bits (30 and 29) never overlap any per-task bit.
 * @details Each task gives its own meaning to the low bits, but every task also
 * receives `N_TASK_PAUSE` and `N_TASK_RESUME` from the OBC. If someone added a
 * per-task bit at position 29 or 30, that task would pause itself when it got
 * an ordinary command.
 * The test ORs every `N_<TASK>_*` bit defined in notifications.h and checks:
 * - none of them overlaps PAUSE/RESUME;
 * - PAUSE and RESUME have their documented values.
 * @note When you add a new `N_*` bit to notifications.h, add it to the list below too.
 */
void test_global_pause_resume_bits_do_not_collide_with_task_bits(void)
{
    const uint32_t task_bits =
        N_ADCS_DESIRED_STATE_IDLE | N_ADCS_DESIRED_STATE_DETUMBLING | N_ADCS_DESIRED_STATE_NADIR |
        N_ADCS_NEW_CALIBRATION | N_ADCS_NEW_TLE |
        N_OBC_EXIT_STATE_TO_OBC_STATE_NM | N_OBC_EXIT_STATE_TO_OBC_STATE_CM |
        N_OBC_EXIT_STATE_TO_OBC_STATE_SSM | N_OBC_EXIT_STATE_TO_OBC_STATE_SM |
        N_OBC_UPDATE_TIME | N_OBC_HARD_REBOOT | N_OBC_SOFT_REBOOT | N_OBC_PERIPHERALS_REBOOT |
        N_COMMS_NEW_CONFIG | N_COMMS_NEW_PARAMS | N_COMMS_STOP_RF | N_COMMS_RESUME_RF | N_COMMS_TRANSMIT_BEACON |
        N_TRANSCEIVER_RADIO_IRQ_BIT | N_TRANSCEIVER_TX_READY_BIT | N_TRANSCEIVER_ARQ_DL_BIT |
        N_EPS_NEW_THRESHOLDS | N_EPS_ENABLE_AUTO_HEAT | N_EPS_DISABLE_AUTO_HEAT |
        N_OBDH_CLEAR_PAYLOAD | N_OBDH_CLEAR_FLASH | N_OBDH_CLEAR_HT |
        N_PAYLOAD_ACTIVATE | N_PAYLOAD_DEACTIVATE;

    TEST_ASSERT_EQUAL_HEX32(0u, task_bits & (N_TASK_PAUSE | N_TASK_RESUME));
    TEST_ASSERT_NOT_EQUAL_UINT32(N_TASK_PAUSE, N_TASK_RESUME);
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, N_TASK_PAUSE);
    TEST_ASSERT_EQUAL_HEX32(0x20000000u, N_TASK_RESUME);
}

/**
 * @brief `N_OBC_EXIT_STATE_GROUP_MASK` contains exactly the four state-change bits (NM, CM, SSM, SM).
 * @details `obc.c` and `state_machine.c` use this mask to decide whether a
 * notification asks for a state change. A missing bit would ignore one
 * transition; an extra bit would treat, for example, a time update as a
 * state change.
 */
void test_exit_state_group_mask_is_four_bits(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x0Fu, N_OBC_EXIT_STATE_GROUP_MASK);
}

/**
 * @brief Every `TM_TASK_*` bit equals that task's `EV_*_ACK` bit.
 * @details When the OBC pauses tasks, it waits on an event group for the ACK
 * bits of the same tasks it asked (`task_management.c` uses one mask for both).
 * If, say, `TM_TASK_EPS` and `EV_EPS_ACK` differed, the OBC would wait for the
 * wrong task's ACK and hit the 10 s timeout on every state change.
 */
void test_tm_task_bits_equal_ack_event_bits(void)
{
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_PAYLOAD,     EV_PAYLOAD_ACK);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_EPS,         EV_EPS_ACK);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_COMMS,       EV_COMMS_ACK);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_ADCS,        EV_ADCS_ACK);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_OBDH,        EV_OBDH_ACK);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_TRANSCEIVER, EV_TRANSCEIVER_ACK);
    TEST_ASSERT_EQUAL_HEX32(TM_TASK_BEACON,      EV_BEACON_ACK);
}

/* =========================================================================
 * 3. Self-tests of the fake FreeRTOS
 *    Every other suite relies on the fake behaving like the real FreeRTOS,
 *    so these tests check the fake itself.
 * ========================================================================= */

/**
 * @brief The fake queue is FIFO, copies items by value, and refuses a send when it is full.
 * @details
 * - Create a queue of 2 ints and send 11 and 22. A third send (33) returns
 *   `errQUEUE_FULL`, because the timeout is 0.
 * - Receive one item (11, first in first out), then send 33. The internal
 *   ring buffer now wraps around its end.
 * - Receive 22 and then 33, in order. A further receive on the empty queue
 *   returns `pdFALSE`.
 */
void test_fake_queue_is_fifo_and_reports_full(void)
{
    QueueHandle_t q = xQueueCreate(2, sizeof(int));
    TEST_ASSERT_NOT_NULL(q);
    int a = 11, b = 22, c = 33, out = 0;

    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(q, &a, 0));
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(q, &b, 0));
    TEST_ASSERT_EQUAL_INT(errQUEUE_FULL, xQueueSend(q, &c, 0));
    TEST_ASSERT_EQUAL_UINT(2, fake_rtos_queue_items(q));

    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(q, &out, 0));
    TEST_ASSERT_EQUAL_INT(11, out);
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(q, &c, 0));   /* wrap-around inside the ring */
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(q, &out, 0));
    TEST_ASSERT_EQUAL_INT(22, out);
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(q, &out, 0));
    TEST_ASSERT_EQUAL_INT(33, out);
    TEST_ASSERT_EQUAL_INT(pdFALSE, xQueueReceive(q, &out, 0));
}

/**
 * @brief `xEventGroupWaitBits()` in the fake follows the FreeRTOS rules.
 * @details The pause/resume handshake depends on these rules:
 * - **Satisfied wait:** with bits 0x3 set and a wait for all of 0x3 with
 *   clear-on-exit, the call returns 0x3 (the value before clearing) and leaves
 *   the group at 0.
 * - **Unsatisfied wait:** with only 0x1 set and a wait for all of 0x3 with a
 *   5-tick timeout, the call returns the current value (0x1), clears nothing,
 *   and advances the fake clock by exactly 5 ticks.
 */
void test_fake_event_wait_returns_bits_and_clears(void)
{
    EventGroupHandle_t eg = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(eg);

    xEventGroupSetBits(eg, 0x3u);
    TEST_ASSERT_EQUAL_HEX32(0x3u, xEventGroupWaitBits(eg, 0x3u, pdTRUE, pdTRUE, 0));
    TEST_ASSERT_EQUAL_HEX32(0x0u, xEventGroupGetBits(eg));

    /* Not satisfied (waiting for all of 0x3, only 0x1 set): nothing cleared, tick advances by the timeout. */
    xEventGroupSetBits(eg, 0x1u);
    TickType_t before = xTaskGetTickCount();
    TEST_ASSERT_EQUAL_HEX32(0x1u, xEventGroupWaitBits(eg, 0x3u, pdTRUE, pdTRUE, 5));
    TEST_ASSERT_EQUAL_HEX32(0x1u, xEventGroupGetBits(eg));
    TEST_ASSERT_EQUAL_UINT32(before + 5u, xTaskGetTickCount());
}

/**
 * @brief A wait with `portMAX_DELAY` that can never be satisfied is reported as a deadlock instead of hanging.
 * @details On the satellite such a wait would block the task forever. In the
 * fake it normally fails the running test with "fake_rtos: deadlock in ...".
 * Here `fake_rtos_expect_deadlock(true)` turns that failure into a recorded
 * flag, so the test can check that the detection happened.
 */
void test_fake_detects_forever_block(void)
{
    EventGroupHandle_t eg = xEventGroupCreate();
    fake_rtos_expect_deadlock(true);
    TEST_ASSERT_FALSE(fake_rtos_deadlock_detected());

    (void)xEventGroupWaitBits(eg, 0x1u, pdTRUE, pdTRUE, portMAX_DELAY);

    TEST_ASSERT_TRUE(fake_rtos_deadlock_detected());
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 * @details A test function that isn't listed here never runs.
 * `pocat_parse_args()` handles `-l` (list), `-f <text>` (filter), `-n <name>`
 * and `-x <text>`; see test/support/pocat_test_main.h.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    POCAT_RUN_TEST(test_wait_returns_zero_when_nothing_pending);
    POCAT_RUN_TEST(test_wait_returns_pending_bits_and_drains_them);
    POCAT_RUN_TEST(test_set_bits_notifications_accumulate);
    POCAT_RUN_TEST(test_obdh_index1_does_not_leak_into_index0);
    POCAT_RUN_TEST(test_global_pause_resume_bits_do_not_collide_with_task_bits);
    POCAT_RUN_TEST(test_exit_state_group_mask_is_four_bits);
    POCAT_RUN_TEST(test_tm_task_bits_equal_ack_event_bits);
    POCAT_RUN_TEST(test_fake_queue_is_fifo_and_reports_full);
    POCAT_RUN_TEST(test_fake_event_wait_returns_bits_and_clears);
    POCAT_RUN_TEST(test_fake_detects_forever_block);
    return POCAT_END();
}
