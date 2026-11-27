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
#include "notifications.h"
#include "main.h"
#include <stdio.h>

#define TM_PAUSE_ACK_TIMEOUT_MS 10000u

/* ---- Task table ---- */

/*
 * One row per task, at its TM_IDX_* index (task_management.h). Each row's
 * designator [TM_IDX_X] places it, so the order of the lines does not matter,
 * and TM_TASK_X = (1u << TM_IDX_X) always selects the row of task X. A row
 * left out would stay all zeros: test_task_management checks every row.
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
    [TM_IDX_PAYLOAD]     = { NULL, payload_task,     "PAYLOAD",     PAYLOAD_STACK_SIZE,     PAYLOAD_PRIORITY,     HEALTH_BIT_PAYLOAD,     EV_PAYLOAD_ACK,     false },
    [TM_IDX_EPS]         = { NULL, eps_task,         "EPS",         EPS_STACK_SIZE,         EPS_PRIORITY,         HEALTH_BIT_EPS,         EV_EPS_ACK,         false },
    [TM_IDX_COMMS]       = { NULL, comms_task,       "COMMS",       COMMS_STACK_SIZE,       COMMS_PRIORITY,       HEALTH_BIT_COMMS,       EV_COMMS_ACK,       false },
    [TM_IDX_ADCS]        = { NULL, adcs_task,        "ADCS",        ADCS_STACK_SIZE,        ADCS_PRIORITY,        HEALTH_BIT_ADCS,        EV_ADCS_ACK,        false },
    [TM_IDX_OBDH]        = { NULL, obdh_task,        "OBDH",        OBDH_STACK_SIZE,        OBDH_PRIORITY,        HEALTH_BIT_OBDH,        EV_OBDH_ACK,        false },
    [TM_IDX_TRANSCEIVER] = { NULL, transceiver_task, "TRANSCEIVER", TRANSCEIVER_STACK_SIZE, TRANSCEIVER_PRIORITY, HEALTH_BIT_TRANSCEIVER, EV_TRANSCEIVER_ACK, false },
    [TM_IDX_BEACON]      = { NULL, beacon_task,      "BEACON",      BEACON_STACK_SIZE,      BEACON_PRIORITY,      HEALTH_BIT_BEACON,      EV_BEACON_ACK,      false },
};

#define TASK_TABLE_SIZE (sizeof(task_table) / sizeof(task_table[0]))

_Static_assert(TASK_TABLE_SIZE == TM_TASK_COUNT, "TM_TASK_COUNT must match the task table");
_Static_assert(TM_TASK_COUNT <= EV_RESUME_ACK_SHIFT, "pause and resume ACK bits must not overlap");
_Static_assert(EV_RESUME_ACK_SHIFT + TM_TASK_COUNT <= 24u, "resume ACK bits must fit the 24 bits of an event group");

static EventGroupHandle_t task_event_group_handle = NULL;

/* ---- Private helper prototypes ---- */

static BaseType_t create_task_event_group(void);
static BaseType_t create_single_task(uint32_t idx);
static void reset_task(uint32_t idx);
static uint32_t notify_and_wait(uint32_t mask, uint32_t notif_bit);
static EventBits_t ack_bit_for(const TaskEntry_t *entry, uint32_t notif_bit);
static inline uint32_t pop_lsb(uint32_t *mask);

/* ---- Public function definitions ---- */

BaseType_t tm_create_tasks(uint32_t running, uint32_t paused)
{
    if (!obc_is_caller()) {
        return pdFAIL;   /* another task: nothing created */
    }

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

uint32_t tm_pause_tasks(uint32_t mask)
{
    if (!obc_is_caller()) {
        return mask;   /* another task: no task notified, none acknowledged */
    }

    return notify_and_wait(mask, N_TASK_PAUSE);
}

uint32_t tm_resume_tasks(uint32_t mask)
{
    if (!obc_is_caller()) {
        return mask;   /* another task: no task notified, none acknowledged */
    }

    return notify_and_wait(mask, N_TASK_RESUME);
}

void tm_reset_task(uint32_t task_bit)
{
    if (!obc_is_caller()) {
        return;   /* another task: nothing deleted or created */
    }

    reset_task(__builtin_ctz(task_bit));
}

void tm_handle_health_faults(EventBits_t faults)
{
    if (!obc_is_caller()) {
        return;   /* another task: nothing deleted or created */
    }

    for (uint32_t idx = 0; idx < TASK_TABLE_SIZE; idx++)
    {
        if ((faults & task_table[idx].health_bit) == 0u)
            continue;

        reset_task(idx);
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

    /* A resume wins over a pause that arrives with it: the OBC sends a resume
     * only after it has stopped waiting for the pause. A running task
     * acknowledges a resume too, so the OBC is never left waiting for it.
     * The resume has its own ACK bit, so it never counts as a pause. */
    if (notif & N_TASK_RESUME) {
        if (entry->paused) {
            entry->paused = false;
            /* Re-include in health monitoring now that the task will kick. */
            health_set_expected(health_get_expected() | entry->health_bit);
        }
        xEventGroupSetBits(task_event_group_handle, ack_bit_for(entry, N_TASK_RESUME));
        return false;
    }

    if (entry->paused) {
        if (deferred)
            *deferred |= other_bits;
        return true;
    }

    if (notif & N_TASK_PAUSE) {
        if (deferred)
            *deferred |= other_bits;

        entry->paused = true;
        /* Exclude from health monitoring; paused tasks won't reach health_kick. */
        health_set_expected(health_get_expected() & ~entry->health_bit);
        xEventGroupSetBits(task_event_group_handle, ack_bit_for(entry, N_TASK_PAUSE));
        return true;
    }

    return false;
}

int tm_current_index(void)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    /* A task outside the table has number 0, like PAYLOAD: the handle tells them apart. */
    UBaseType_t idx = uxTaskGetTaskNumber(self);
    if ((idx < TASK_TABLE_SIZE) && (task_table[idx].handle == self))
        return (int)idx;
    if (self == main_get_obc_handle())
        return (int)TM_OBC_INDEX;
    return -1;
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

/* Deletes the task in row idx and creates it again; nothing if the row has no task. */
static void reset_task(uint32_t idx)
{
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

/* Returns the TM_TASK_* flags of the tasks in mask that did not acknowledge. */
static uint32_t notify_and_wait(uint32_t mask, uint32_t notif_bit)
{
    /* Build the ACK bitmask */
    EventBits_t ack_mask = 0;
    uint32_t tmp = mask;
    while (tmp)
    {
        uint32_t idx = pop_lsb(&tmp);
        if (task_table[idx].handle != NULL)
            ack_mask |= ack_bit_for(&task_table[idx], notif_bit);
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

    uint32_t missing_tasks = 0u;
    tmp = mask;
    while (tmp)
    {
        uint32_t idx = pop_lsb(&tmp);
        if ((missing & ack_bit_for(&task_table[idx], notif_bit)) != 0u)
            missing_tasks |= (1u << idx);
    }
    return missing_tasks;
}

/* The event bit a task sets to acknowledge a pause (N_TASK_PAUSE) or a resume (N_TASK_RESUME) */
static EventBits_t ack_bit_for(const TaskEntry_t *entry, uint32_t notif_bit)
{
    return (notif_bit == N_TASK_PAUSE) ? entry->ack_bit : (entry->ack_bit << EV_RESUME_ACK_SHIFT);
}

static inline uint32_t pop_lsb(uint32_t *mask)
{
    uint32_t idx = __builtin_ctz(*mask);
    *mask &= *mask - 1;
    return idx;
}