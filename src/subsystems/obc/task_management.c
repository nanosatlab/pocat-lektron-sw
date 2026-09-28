/**
 * @file task_management.c
 * @brief Task creation and reset management for OBC subsystem tasks.
 * @version 0.2
 * @date 2026-03-30
 *
 * @copyright Copyright (c) 2026
 */

/* ---- Includes ---- */
#include "task_management.h"
#include "obc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "events.h"
#include "eps.h"
#include "comms.h"
#include "obdh.h"
#include "payload.h"
#include "adcs.h"
#include "transceiver.h"
#include "beacon.h"
#include "health.h"
#include "flash.h"
#include "notifications.h"
#include <stdio.h>

#define TM_PAUSE_ACK_TIMEOUT_MS 10000u

/* ---- Task table ---- */

/*
 * Table is indexed directly by bit position: TM_TASK_X = (1u << idx).
 * Entry order MUST match the TM_TASK_* bit definitions in task_management.h.
 */
typedef struct {
    TaskHandle_t     handle;
    TaskFunction_t   func;
    const char      *name;
    uint16_t         stack_size;
    UBaseType_t      priority;
    EventBits_t      health_bit;
    EventBits_t      ack_bit;
    bool             paused;
} TaskEntry_t;

static TaskEntry_t task_table[] = {
    /* idx 0 = TM_TASK_PAYLOAD     */ { NULL, payload_task,     "PAYLOAD",     PAYLOAD_STACK_SIZE,     PAYLOAD_PRIORITY,     HEALTH_BIT_PAYLOAD,     EV_PAYLOAD_ACK,     false },
    /* idx 1 = TM_TASK_EPS         */ { NULL, eps_task,         "EPS",         EPS_STACK_SIZE,         EPS_PRIORITY,         HEALTH_BIT_EPS,         EV_EPS_ACK,         false },
    /* idx 2 = TM_TASK_COMMS       */ { NULL, comms_task,       "COMMS",       COMMS_STACK_SIZE,       COMMS_PRIORITY,       HEALTH_BIT_COMMS,       EV_COMMS_ACK,       false },
    /* idx 3 = TM_TASK_ADCS        */ { NULL, adcs_task,        "ADCS",        ADCS_STACK_SIZE,        ADCS_PRIORITY,        HEALTH_BIT_ADCS,        EV_ADCS_ACK,        false },
    /* idx 4 = TM_TASK_OBDH        */ { NULL, obdh_task,        "OBDH",        OBDH_STACK_SIZE,        OBDH_PRIORITY,        HEALTH_BIT_OBDH,        EV_OBDH_ACK,        false },
    /* idx 5 = TM_TASK_TRANSCEIVER */ { NULL, transceiver_task, "TRANSCEIVER", TRANSCEIVER_STACK_SIZE, TRANSCEIVER_PRIORITY, HEALTH_BIT_TRANSCEIVER, EV_TRANSCEIVER_ACK, false },
    /* idx 6 = TM_TASK_BEACON      */ { NULL, beacon_task,      "BEACON",      BEACON_STACK_SIZE,      BEACON_PRIORITY,      HEALTH_BIT_BEACON,      EV_BEACON_ACK,      false },
};

#define TASK_TABLE_SIZE (sizeof(task_table) / sizeof(task_table[0]))

static EventGroupHandle_t task_event_group_handle = NULL;

/* ---- Private helper prototypes ---- */

static BaseType_t create_task_event_group(void);
static BaseType_t create_single_task(uint32_t idx);
static void notify_and_wait(uint32_t mask, uint32_t notif_bit);
static inline uint32_t pop_lsb(uint32_t *mask);

/* ---- Public function definitions ---- */

BaseType_t tm_create_tasks(uint32_t running, uint32_t paused)
{
    BaseType_t ok = create_task_event_group();
    if (ok != pdPASS)
        return ok;

    uint32_t all = running | paused;
    while (all)
    {
        uint32_t idx = pop_lsb(&all);
        task_table[idx].paused = (paused & (1u << idx)) != 0u;
        ok = create_single_task(idx);
        if (ok != pdPASS)
        {
            printf("Error creating %s task\r\n", task_table[idx].name);
            return ok;
        }
    }

    return pdPASS;
}

void tm_pause_tasks(uint32_t mask)
{
    notify_and_wait(mask, N_TASK_PAUSE);
}

void tm_resume_tasks(uint32_t mask)
{
    notify_and_wait(mask, N_TASK_RESUME);
}

void tm_reset_task(uint32_t task_bit)
{
    uint32_t idx = __builtin_ctz(task_bit);
    if (idx >= TASK_TABLE_SIZE || task_table[idx].handle == NULL)
        return;

    taskENTER_CRITICAL();
    vTaskSuspend(task_table[idx].handle);
    vTaskDelete(task_table[idx].handle);
    task_table[idx].handle = NULL;
    taskEXIT_CRITICAL();

    BaseType_t ok = create_single_task(idx);
    if (ok != pdPASS)
    {
        printf("Error recreating %s task\r\n", task_table[idx].name);
    }
}

void tm_handle_health_faults(EventBits_t faults)
{
    for (uint32_t idx = 0; idx < TASK_TABLE_SIZE; idx++)
    {
        if ((faults & task_table[idx].health_bit) == 0u)
            continue;

        tm_reset_task(1u << idx);
        printf("%s task reset due to health check\r\n", task_table[idx].name);
    }
}

bool tm_check_pause(uint32_t notif, uint32_t *deferred)
{
    /* uxTaskGetTaskNumber(NULL) returns 0 unconditionally, not the caller's
     * number — unlike most FreeRTOS APIs where NULL means "current task". */
    uint32_t idx = uxTaskGetTaskNumber(xTaskGetCurrentTaskHandle());
    if (idx >= TASK_TABLE_SIZE)
        return false;

    TaskEntry_t *entry = &task_table[idx];
    uint32_t other_bits = notif & ~(N_TASK_PAUSE | N_TASK_RESUME);

    if (entry->paused) {
        if (deferred)
            *deferred |= other_bits;

        if (notif & N_TASK_RESUME) {
            entry->paused = false;
            /* Re-include in health monitoring now that the task will kick. */
            health_set_expected(health_get_expected() | entry->health_bit);
            xEventGroupSetBits(task_event_group_handle, entry->ack_bit);
            return false;
        }
        return true;
    }

    if (notif & N_TASK_PAUSE) {
        if (deferred)
            *deferred |= other_bits;

        entry->paused = true;
        /* Exclude from health monitoring; paused tasks won't reach health_kick. */
        health_set_expected(health_get_expected() & ~entry->health_bit);
        xEventGroupSetBits(task_event_group_handle, entry->ack_bit);
        return true;
    }

    return false;
}

TaskHandle_t tm_get_task_handle(uint32_t task_bit)
{
    uint32_t idx = __builtin_ctz(task_bit);
    if (idx >= TASK_TABLE_SIZE)
        return NULL;
    return task_table[idx].handle;
}


/* ---- Private helpers ---- */

static BaseType_t create_task_event_group(void)
{
    if (task_event_group_handle == NULL)
    {
        task_event_group_handle = xEventGroupCreate();
        if (task_event_group_handle == NULL)
        {
            printf("Error creating task event group\r\n");
            return pdFAIL;
        }
    }
    return pdPASS;
}

static BaseType_t create_single_task(uint32_t idx)
{
    TaskEntry_t *entry = &task_table[idx];
    BaseType_t ok = xTaskCreate(entry->func, entry->name, entry->stack_size,
                                NULL, entry->priority, &entry->handle);

    if (ok == pdPASS)
    {
        vTaskSetTaskNumber(entry->handle, idx);
        /* Only monitor health for running tasks. Paused tasks never reach
         * health_kick(), so including them guarantees a fault every period. */
        if (!entry->paused)
        {
            health_set_expected(health_get_expected() | entry->health_bit);
        }
    }
    return ok;
}

static void notify_and_wait(uint32_t mask, uint32_t notif_bit)
{
    /* Build the ACK bitmask */
    EventBits_t ack_mask = 0;
    uint32_t tmp = mask;
    while (tmp)
    {
        uint32_t idx = pop_lsb(&tmp);
        if (task_table[idx].handle != NULL)
            ack_mask |= task_table[idx].ack_bit;
    }

    xEventGroupClearBits(task_event_group_handle, ack_mask);

    /* Notify tasks */
    tmp = mask;
    while (tmp)
    {
        uint32_t idx = pop_lsb(&tmp);
        if (task_table[idx].handle != NULL)
            xTaskNotify(task_table[idx].handle, notif_bit, eSetBits);
    }

    EventBits_t acks = xEventGroupWaitBits(task_event_group_handle,
                                           ack_mask,
                                           pdTRUE,
                                           pdTRUE,
                                           pdMS_TO_TICKS(TM_PAUSE_ACK_TIMEOUT_MS));

    EventBits_t missing = ack_mask & ~acks;
    if (missing != 0)
    {
        const char *action = (notif_bit == N_TASK_PAUSE) ? "PAUSE" : "RESUME";
        printf("tm: %s ACK timeout, missing: 0x%08lX\r\n", action, (unsigned long)missing);
    }
}

static inline uint32_t pop_lsb(uint32_t *mask)
{
    uint32_t idx = __builtin_ctz(*mask);
    *mask &= *mask - 1;
    return idx;
}