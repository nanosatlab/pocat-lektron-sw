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

/** @name Task bitmask definitions
 *  Used to select which tasks to create, pause, resume, or ACK.
 *  They coincide with the FreeRTOS task numbers assigned to each task when creating them with vTaskSetTaskNumber(). 
 * @{ */
#define TM_TASK_PAYLOAD     (1u << 0)
#define TM_TASK_EPS         (1u << 1)
#define TM_TASK_COMMS       (1u << 2)
#define TM_TASK_ADCS        (1u << 3)
#define TM_TASK_OBDH        (1u << 4)
#define TM_TASK_TRANSCEIVER (1u << 5)
#define TM_TASK_BEACON      (1u << 6)

#define TM_TASK_ALL         (TM_TASK_PAYLOAD | TM_TASK_EPS | TM_TASK_COMMS | \
                             TM_TASK_ADCS | TM_TASK_OBDH | TM_TASK_TRANSCEIVER | \
                             TM_TASK_BEACON)

/** @} */

/** @name Task creation, pause, resume and reset
 * @{ */

/**
 * @brief Create subsystem tasks selected by bitmask.
 * @param running Bitwise OR of TM_TASK_* flags for tasks that start running.
 * @param paused  Bitwise OR of TM_TASK_* flags for tasks that start paused.
 * @return pdPASS on success, pdFAIL if any task creation failed.
 */
BaseType_t tm_create_tasks(uint32_t running, uint32_t paused);

/**
 * @brief Request selected tasks to pause and wait for ACKs.
 * @param task_mask Bitwise OR of TM_TASK_* flags.
 */
void tm_pause_tasks(uint32_t task_mask);

/**
 * @brief Request selected tasks to resume and wait for ACKs.
 * @param task_mask Bitwise OR of TM_TASK_* flags.
 */
void tm_resume_tasks(uint32_t task_mask);

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
 * ACKs the OBC automatically.
 *
 * @param notif     Raw notification bits from xTaskNotifyWait().
 * @param deferred  Pointer to the task's deferred-notification accumulator.
 *                  May be NULL if the task does not defer notifications.
 * @return true  Task is paused — caller should skip processing this cycle.
 * @return false Task is active — caller should process *deferred (then clear it).
 */
bool tm_check_pause(uint32_t notif, uint32_t *deferred);

/**
 * @brief Get the FreeRTOS task handle for a given task.
 * @param task_bit Single TM_TASK_* flag identifying the task.
 * @return TaskHandle_t or NULL if the task is not found or not yet created.
 */
TaskHandle_t tm_get_task_handle(uint32_t task_bit);

#endif /* INC_TASK_MANAGEMENT_H_ */
