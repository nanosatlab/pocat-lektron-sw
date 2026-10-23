/**
 * @file fake_rtos.h
 * @brief Single-threaded fake of FreeRTOS for host tests, plus the API tests use to control and inspect it.
 * @date 2026-10-23
 *
 * @details
 * The flight code calls the normal FreeRTOS functions (`xTaskCreate`,
 * `xTaskNotify`, `xQueueSend`, `xEventGroupWaitBits`, ...). In a host test
 * those calls land in test/mocks/fake_rtos.c, which implements them with the
 * same rules as FreeRTOS, with three differences:
 *
 * - **Nothing runs in parallel.** `xTaskCreate` records the task (name,
 *   priority, stack size, task number) but never runs its function. The
 *   "current task", the one `xTaskGetCurrentTaskHandle()` and
 *   `xTaskNotifyWait()` refer to, is whatever the test sets with
 *   fake_rtos_set_current_task(). It starts as the test task.
 * - **Blocking is simulated.** When a call would block (a queue is full or
 *   empty, event bits are missing, no notification is pending) and its
 *   timeout is non-zero, the fake does the following:
 *   1. calls the block hook once, if one is set. The hook is never
 *      re-entered;
 *   2. checks again whether the call can complete now;
 *   3. if not: with a finite timeout, it advances the tick by the timeout
 *      and returns "timed out". With `portMAX_DELAY`, it fails the test with
 *      "fake_rtos: deadlock in <api>", because on the satellite the task
 *      would hang forever.
 * - **Everything is recorded.** Every notification is logged with target,
 *   index, value and action, and per-task totals are kept, so tests can
 *   assert on what the code sent.
 *
 * `configASSERT()` in the stub FreeRTOS.h fails the running test with the
 * file and line.
 *
 * **Typical use in a suite**
 * @code
 * void setUp(void) {
 *     fake_rtos_reset();                               // clean state for each test
 *     fake_rtos_set_block_hook(sim_tasks_run_pending); // optional: let tasks answer PAUSE/RESUME
 * }
 * void tearDown(void) {
 *     TEST_ASSERT_EQUAL_INT(0, fake_rtos_critical_depth());
 *     TEST_ASSERT_EQUAL_INT(0, fake_rtos_suspend_all_depth());
 * }
 * @endcode
 */
#ifndef FAKE_RTOS_H
#define FAKE_RTOS_H
#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "timers.h"

/* ---------------------------------------------------------------- state */

/**
 * @brief Puts the fake back in its initial state. Call it first in every `setUp()`.
 * @details Frees every task, queue, event group, mutex and timer. Then it:
 * - sets the tick to 0;
 * - makes the test task current again;
 * - removes the block hook;
 * - clears the notify log and all fault injections;
 * - turns deadlock-expect mode off.
 * @warning Handles the code under test kept in its own `static` variables now
 * point at freed objects. The suite must reset those variables too.
 */
void fake_rtos_reset(void);

/** @brief Returns the task that represents the test itself. It is current after fake_rtos_reset(). */
TaskHandle_t fake_rtos_test_task(void);

/**
 * @brief Chooses which task is "running".
 * @details This changes what `xTaskGetCurrentTaskHandle()` returns and whose
 * notifications `xTaskNotifyWait()` reads.
 * @param t Task to make current; NULL selects the test task again.
 */
void fake_rtos_set_current_task(TaskHandle_t t);

/** @brief Returns the task that is currently "running". */
TaskHandle_t fake_rtos_current_task(void);

/* ---------------------------------------------------------------- time and blocking */

/** @brief Function the fake calls when the code under test would block. See the file description. */
typedef void (*fake_rtos_block_hook_t)(void);

/**
 * @brief Installs the block hook, or removes it with NULL.
 * @details Usually `sim_tasks_run_pending` (test/support/sim_tasks.h), which
 * lets the subsystem tasks answer pending PAUSE/RESUME requests.
 */
void fake_rtos_set_block_hook(fake_rtos_block_hook_t hook);

/** @brief Sets the tick count (1 tick = 1 ms, as on the target) to an absolute value. */
void fake_rtos_set_tick(TickType_t t);

/** @brief Moves time forward by @p dt ticks without calling anything. */
void fake_rtos_advance_ticks(TickType_t dt);

/* ---------------------------------------------------------------- fault injection */

/**
 * @brief Makes a future `xTaskCreate` call fail with `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`.
 * @param nth Which upcoming call fails: 1 = the next one, 3 = the third one from now. 0 turns it off.
 */
void fake_rtos_fail_task_create_at(int nth);

/** @brief Makes the next `xQueueCreate` return NULL (out of memory). */
void fake_rtos_fail_next_queue_create(void);

/** @brief Makes the next `xEventGroupCreate` return NULL (out of memory). */
void fake_rtos_fail_next_event_group_create(void);

/* ---------------------------------------------------------------- inspection: tasks */

/** @brief Returns the newest live task with that name, or NULL. The test task is never returned. */
TaskHandle_t fake_rtos_find_task(const char *name);

/** @brief Returns true if @p t was created and not yet deleted with `vTaskDelete`. */
bool fake_rtos_task_alive(TaskHandle_t t);

/** @brief Returns true if `vTaskSuspend` was called on @p t. */
bool fake_rtos_task_suspended(TaskHandle_t t);

/** @brief Returns the number of successful `xTaskCreate` calls since the last reset (deleted tasks still count). */
int  fake_rtos_tasks_created(void);

/* ---------------------------------------------------------------- inspection: notifications */

/** @brief Returns the current notification value of task @p t at index @p idx (0 = task bits, 1 = OBDH completion). */
uint32_t fake_rtos_notify_value(TaskHandle_t t, UBaseType_t idx);

/** @brief Returns true if task @p t has a notification at index @p idx that nobody has read yet. */
bool     fake_rtos_notify_pending(TaskHandle_t t, UBaseType_t idx);

/** @brief One entry of the notify log: one delivered `xTaskNotify` / `xTaskNotifyIndexed` call. */
typedef struct {
    TaskHandle_t  target; /**< Task that received the notification. */
    UBaseType_t   index;  /**< Notification index (0 or 1). */
    uint32_t      value;  /**< Value passed by the sender. */
    eNotifyAction action; /**< How the value was applied (eSetBits, eSetValueWithOverwrite, ...). */
} fake_notify_rec_t;

/** @brief Returns the number of notifications logged since the last reset. A rejected `eSetValueWithoutOverwrite` isn't logged. */
int               fake_rtos_notify_log_count(void);

/** @brief Returns log entry @p i (0 = oldest). An index out of range fails the test. */
fake_notify_rec_t fake_rtos_notify_log_at(int i);

/**
 * @brief Returns the OR of every index-0 value sent to task @p t since the last reset.
 * @details It answers "was this task ever asked to X?" even after the task
 * has read and cleared the notification.
 */
uint32_t          fake_rtos_bits_sent_to(TaskHandle_t t);

/* ---------------------------------------------------------------- inspection: other objects */

/** @brief Returns the number of items waiting in queue @p q. */
UBaseType_t fake_rtos_queue_items(QueueHandle_t q);

/** @brief Returns the current `taskENTER_CRITICAL` nesting depth. It must be 0 at the end of every test. */
int  fake_rtos_critical_depth(void);

/** @brief Returns the current `vTaskSuspendAll` nesting depth. It must be 0 at the end of every test. */
int  fake_rtos_suspend_all_depth(void);

/** @brief Calls the callback of software timer @p t now, as if its period had expired. */
void fake_rtos_fire_timer(TimerHandle_t t);

/* ---------------------------------------------------------------- deadlock detection */

/**
 * @brief Chooses what happens when a `portMAX_DELAY` wait can never be satisfied.
 * @param expect false (default): the test fails with "deadlock". true: the
 * deadlock is only recorded (see fake_rtos_deadlock_detected()) and the call
 * returns as if it had timed out. Use true only in tests that check the
 * detection itself.
 */
void fake_rtos_expect_deadlock(bool expect);

/** @brief Returns true if a deadlock was detected since the last reset. */
bool fake_rtos_deadlock_detected(void);

#endif
