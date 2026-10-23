/**
 * @file sim_tasks.h
 * @brief Simulates the 7 subsystem tasks answering PAUSE/RESUME, so the OBC's pause handshake can complete in a single-threaded test.
 * @date 2026-10-23
 *
 * @details
 * **The problem**: `tm_pause_tasks()` notifies tasks and then waits for their
 * ACKs. In the fake RTOS no task runs on its own, so the ACKs would never
 * arrive.
 *
 * **The solution**: install sim_tasks_run_pending() as the fake RTOS block
 * hook. When the OBC is about to block, the hook runs one loop iteration of
 * each task, the part that calls `tm_check_pause()`, and that sends the ACK.
 *
 * Requires the real task_management.c in the same test program: tasks are
 * found through `tm_get_task_handle()`. Each simulated task's `deferred`
 * word lives here, playing the local variable a real task keeps in its loop.
 *
 * @code
 * fake_rtos_set_block_hook(sim_tasks_run_pending);
 * sim_tasks_set_unresponsive(TM_TASK_EPS);   // optional: EPS is stuck and never answers
 * @endcode
 */
#ifndef SIM_TASKS_H
#define SIM_TASKS_H
#include <stdint.h>

/** @brief Clears the deferred and last-notification records of every task and makes all tasks responsive again. Call it in `setUp()`. */
void sim_tasks_reset(void);

/**
 * @brief Block hook: lets every task with a pending notification handle it once.
 * @details For each of the 7 tasks that exists, is alive, isn't marked
 * unresponsive, and has a pending index-0 notification, it does the
 * following:
 * 1. makes that task the current task;
 * 2. reads and clears its notification with `wait_for_notification(0)`;
 * 3. passes it to `tm_check_pause()`, which ACKs PAUSE/RESUME and defers
 *    other bits while the task is paused.
 *
 * Afterwards, the previously current task is current again. It can also be
 * called directly from a test to deliver notifications sent outside a
 * handshake.
 */
void sim_tasks_run_pending(void);

/**
 * @brief Marks tasks as stuck: sim_tasks_run_pending() skips them, so they never ACK.
 * @param tm_mask `TM_TASK_*` bits of the stuck tasks (0 = all responsive).
 */
void sim_tasks_set_unresponsive(uint32_t tm_mask);

/**
 * @brief Returns the notification bits a task has deferred while paused.
 * @param tm_bit Exactly one `TM_TASK_*` bit.
 */
uint32_t sim_tasks_deferred(uint32_t tm_bit);

/**
 * @brief Returns the last raw notification value the simulated task read.
 * @param tm_bit Exactly one `TM_TASK_*` bit.
 */
uint32_t sim_tasks_last_notification(uint32_t tm_bit);

#endif
