#include "sim_tasks.h"
#include "fake_rtos.h"
#include "notifications.h"
#include "task_management.h"
#include <string.h>

#define SIM_TASK_COUNT 7

static uint32_t deferred[SIM_TASK_COUNT];
static uint32_t last_notification[SIM_TASK_COUNT];
static uint32_t unresponsive_mask;

void sim_tasks_reset(void)
{
    memset(deferred, 0, sizeof(deferred));
    memset(last_notification, 0, sizeof(last_notification));
    unresponsive_mask = 0;
}

void sim_tasks_set_unresponsive(uint32_t tm_mask) { unresponsive_mask = tm_mask; }

static uint32_t idx_of(uint32_t tm_bit) { return (uint32_t)__builtin_ctz(tm_bit); }

uint32_t sim_tasks_deferred(uint32_t tm_bit) { return deferred[idx_of(tm_bit)]; }
uint32_t sim_tasks_last_notification(uint32_t tm_bit) { return last_notification[idx_of(tm_bit)]; }

void sim_tasks_run_pending(void)
{
    TaskHandle_t previous = fake_rtos_current_task();

    for (uint32_t i = 0; i < SIM_TASK_COUNT; i++) {
        uint32_t bit = 1u << i;
        TaskHandle_t h = tm_get_task_handle(bit);
        if (h == NULL || !fake_rtos_task_alive(h)) continue;
        if (unresponsive_mask & bit) continue;
        if (!fake_rtos_notify_pending(h, 0)) continue;

        fake_rtos_set_current_task(h);
        uint32_t n = wait_for_notification(0);
        last_notification[i] = n;
        (void)tm_check_pause(n, &deferred[i]);
    }

    fake_rtos_set_current_task(previous);
}
