/**
 * @file task_management.h
 * @brief Task management for subsystem tasks.
 */

#ifndef INC_TASK_MANAGEMENT_H_
#define INC_TASK_MANAGEMENT_H_

#include <stdbool.h>
#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"
#include "obc.h"

/** @name Task table rows
 *  Each subsystem task has one row in the task table (task_management.c), at
 *  its TM_IDX_* index. When the task is created, that index becomes its
 *  FreeRTOS task number (vTaskSetTaskNumber()), which is how a running task
 *  finds its own row.
 * @{ */
typedef enum {
    TM_IDX_PAYLOAD,
    TM_IDX_EPS,
    TM_IDX_COMMS,
    TM_IDX_ADCS,
    TM_IDX_OBDH,
    TM_IDX_TRANSCEIVER,
    TM_IDX_BEACON,
    TM_TASK_COUNT       /**< Tasks in the task table: indices 0 to TM_TASK_COUNT - 1. Keep it last. */
} tm_task_index_t;

#define TM_OBC_INDEX        TM_TASK_COUNT   /**< Index tm_current_index() gives the OBC task, which is not in the table. */

/** @} */

/** @name Task bitmask definitions
 *  Used to select which tasks to create, pause, resume, or ACK. Bit TM_IDX_X
 *  selects the row TM_IDX_X.
 * @{ */
#define TM_TASK_PAYLOAD     (1u << TM_IDX_PAYLOAD)
#define TM_TASK_EPS         (1u << TM_IDX_EPS)
#define TM_TASK_COMMS       (1u << TM_IDX_COMMS)
#define TM_TASK_ADCS        (1u << TM_IDX_ADCS)
#define TM_TASK_OBDH        (1u << TM_IDX_OBDH)
#define TM_TASK_TRANSCEIVER (1u << TM_IDX_TRANSCEIVER)
#define TM_TASK_BEACON      (1u << TM_IDX_BEACON)

#define TM_TASK_ALL         ((1u << TM_TASK_COUNT) - 1u)   /**< Every task in the table. */

/** @} */

/** @name Task creation, pause, resume and reset
 *  OBC task only: any other caller gets nothing done (obc_is_caller()).
 * @{ */

/**
 * @brief Create subsystem tasks selected by bitmask.
 * @param running Bitwise OR of TM_TASK_* flags for tasks that start running.
 * @param paused  Bitwise OR of TM_TASK_* flags for tasks that start paused.
 * @return pdPASS on success, pdFAIL if any task creation failed or the caller is not the OBC task.
 */
BaseType_t tm_create_tasks(uint32_t running, uint32_t paused);

/**
 * @brief Request selected tasks to pause and wait for ACKs.
 * @param task_mask Bitwise OR of TM_TASK_* flags.
 * @return The TM_TASK_* flags of the tasks that did not acknowledge in time; 0 if all did; @p task_mask, with no task notified, if the caller is not the OBC task.
 */
uint32_t tm_pause_tasks(uint32_t task_mask);

/**
 * @brief Request selected tasks to resume and wait for ACKs.
 * @param task_mask Bitwise OR of TM_TASK_* flags.
 * @return The TM_TASK_* flags of the tasks that did not acknowledge in time; 0 if all did; @p task_mask, with no task notified, if the caller is not the OBC task.
 */
uint32_t tm_resume_tasks(uint32_t task_mask);

/**
 * @brief Reset a task by suspending, deleting, and recreating it.
 * @param task_bit Single TM_TASK_* flag identifying the task to reset.
 * @todo Test correctness and check possible conditions with mutex ownership, blocked states, etc.
 */
void tm_reset_task(uint32_t task_bit);

/**
 * @brief Reset tasks that failed the software health check.
 * @param faults Bitmask returned by health_check().
 */
void tm_handle_health_faults(EventBits_t faults);

/** @} */

/**
 * @brief Handle pause/resume protocol from within a subsystem task.
 *
 * Call this at the top of the task's processing loop with the raw notification
 * value.  It manages the paused state, defers unrelated notifications, and
 * ACKs the OBC automatically, with one event bit for a pause and another for a
 * resume (events.h). A resume wins over a pause that arrives with it (the OBC
 * gave up waiting for the pause before the task saw it), and a task that is
 * already running acknowledges a resume too.
 *
 * @param notif     Raw notification bits from xTaskNotifyWait().
 * @param deferred  Pointer to the task's deferred-notification accumulator.
 *                  May be NULL if the task does not defer notifications.
 * @return true  Task is paused — caller should skip processing this cycle.
 * @return false Task is active — caller should process *deferred (then clear it).
 */
bool tm_check_pause(uint32_t notif, uint32_t *deferred);

/**
 * @brief Index of the calling task: its row in the task table, which a restarted task keeps.
 * @return 0 to TM_TASK_COUNT - 1 for a task of the table, TM_OBC_INDEX for the OBC task, -1 for any other task.
 */
int tm_current_index(void);

/**
 * @brief Get the FreeRTOS task handle for a given task.
 * @param task_bit Single TM_TASK_* flag identifying the task.
 * @return TaskHandle_t or NULL if the task is not found or not yet created.
 */
TaskHandle_t tm_get_task_handle(uint32_t task_bit);

#endif /* INC_TASK_MANAGEMENT_H_ */
