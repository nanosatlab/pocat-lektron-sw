/**
 * @file stub_task_entries.h
 * @brief Test control of the stubs in stub_task_entries.c: the task entry points and main.c's OBC task handle.
 * @date 2026-11-11
 */
#ifndef STUB_TASK_ENTRIES_H
#define STUB_TASK_ENTRIES_H
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief Sets what main_get_obc_handle() returns. The OBC task is not created in host tests, so it is NULL until a test sets it.
 * @param handle The task that plays the OBC task; NULL for none.
 */
void stub_set_obc_handle(TaskHandle_t handle);

#endif
