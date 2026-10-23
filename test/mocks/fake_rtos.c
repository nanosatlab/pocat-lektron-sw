#include "fake_rtos.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTIFY_SLOTS    4
#define NOTIFY_LOG_MAX  1024

struct fake_task {
    struct fake_task *next;
    char              name[32];
    TaskFunction_t    fn;
    UBaseType_t       priority;
    uint16_t          stack_depth;
    UBaseType_t       number;
    bool              alive;
    bool              suspended;
    bool              is_test;
    uint32_t          nvalue[NOTIFY_SLOTS];
    bool              npending[NOTIFY_SLOTS];
    uint32_t          bits_sent;
};

struct fake_queue {
    struct fake_queue *next;
    UBaseType_t        length, item_size, count, head;
    uint8_t           *storage;
};

struct fake_event_group {
    struct fake_event_group *next;
    EventBits_t              bits;
};

struct fake_semaphore {
    struct fake_semaphore *next;
    bool                   taken;
};

struct fake_timer {
    struct fake_timer      *next;
    TickType_t              period;
    bool                    auto_reload, active;
    void                   *id;
    TimerCallbackFunction_t cb;
};

/* All state is global and reachable, so LeakSanitizer never reports leftovers. */
static struct fake_task        *tasks;
static struct fake_queue       *queues;
static struct fake_event_group *egs;
static struct fake_semaphore   *sems;
static struct fake_timer       *timers;
static struct fake_task        *test_task;
static struct fake_task        *current;

static TickType_t now_tick;
static fake_rtos_block_hook_t block_hook;
static bool in_hook;
static int  tasks_created_count;
static int  fail_create_countdown;
static bool fail_next_queue, fail_next_eg;
static int  critical_depth, suspend_depth;
static bool expect_deadlock, deadlock_seen;

static fake_notify_rec_t notify_log[NOTIFY_LOG_MAX];
static int               notify_log_n;

/* ---------------------------------------------------------------- */

static struct fake_task *new_task_record(const char *name)
{
    struct fake_task *t = calloc(1, sizeof(*t));
    TEST_ASSERT_NOT_NULL_MESSAGE(t, "fake_rtos: out of memory");
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->alive = true;
    t->next = tasks;
    tasks = t;
    return t;
}

void fake_rtos_reset(void)
{
    while (tasks)  { struct fake_task *n = tasks->next;  free(tasks);  tasks = n; }
    while (queues) { struct fake_queue *n = queues->next; free(queues->storage); free(queues); queues = n; }
    while (egs)    { struct fake_event_group *n = egs->next; free(egs); egs = n; }
    while (sems)   { struct fake_semaphore *n = sems->next; free(sems); sems = n; }
    while (timers) { struct fake_timer *n = timers->next; free(timers); timers = n; }
    now_tick = 0;
    block_hook = NULL;
    in_hook = false;
    tasks_created_count = 0;
    fail_create_countdown = 0;
    fail_next_queue = fail_next_eg = false;
    critical_depth = suspend_depth = 0;
    expect_deadlock = deadlock_seen = false;
    notify_log_n = 0;
    test_task = new_task_record("TEST");
    test_task->is_test = true;
    current = test_task;
}

void fake_rtos_assert_failed(const char *file, int line)
{
    char msg[160];
    snprintf(msg, sizeof(msg), "configASSERT failed at %s:%d", file, line);
    TEST_FAIL_MESSAGE(msg);
}

/* ---------------------------------------------------------------- control API */

TaskHandle_t fake_rtos_test_task(void) { return test_task; }
void fake_rtos_set_current_task(TaskHandle_t t) { current = t ? t : test_task; }
TaskHandle_t fake_rtos_current_task(void) { return current; }
void fake_rtos_set_block_hook(fake_rtos_block_hook_t hook) { block_hook = hook; }
void fake_rtos_set_tick(TickType_t t) { now_tick = t; }
void fake_rtos_advance_ticks(TickType_t dt) { now_tick += dt; }
void fake_rtos_fail_task_create_at(int nth) { fail_create_countdown = nth; }
void fake_rtos_fail_next_queue_create(void) { fail_next_queue = true; }
void fake_rtos_fail_next_event_group_create(void) { fail_next_eg = true; }
void fake_rtos_expect_deadlock(bool expect) { expect_deadlock = expect; }
bool fake_rtos_deadlock_detected(void) { return deadlock_seen; }
int  fake_rtos_critical_depth(void) { return critical_depth; }
int  fake_rtos_suspend_all_depth(void) { return suspend_depth; }
int  fake_rtos_tasks_created(void) { return tasks_created_count; }

TaskHandle_t fake_rtos_find_task(const char *name)
{
    for (struct fake_task *t = tasks; t; t = t->next) {   /* list head is the newest */
        if (!t->is_test && t->alive && strcmp(t->name, name) == 0) return t;
    }
    return NULL;
}

bool fake_rtos_task_alive(TaskHandle_t t) { return t != NULL && t->alive; }
bool fake_rtos_task_suspended(TaskHandle_t t) { return t != NULL && t->suspended; }

uint32_t fake_rtos_notify_value(TaskHandle_t t, UBaseType_t idx)
{
    configASSERT(t != NULL && idx < NOTIFY_SLOTS);
    return t->nvalue[idx];
}

bool fake_rtos_notify_pending(TaskHandle_t t, UBaseType_t idx)
{
    configASSERT(t != NULL && idx < NOTIFY_SLOTS);
    return t->npending[idx];
}

int fake_rtos_notify_log_count(void) { return notify_log_n; }

fake_notify_rec_t fake_rtos_notify_log_at(int i)
{
    TEST_ASSERT_TRUE_MESSAGE(i >= 0 && i < notify_log_n, "fake_rtos: notify log index out of range");
    return notify_log[i];
}

uint32_t fake_rtos_bits_sent_to(TaskHandle_t t) { return t ? t->bits_sent : 0u; }

UBaseType_t fake_rtos_queue_items(QueueHandle_t q)
{
    configASSERT(q != NULL);
    return q->count;
}

void fake_rtos_fire_timer(TimerHandle_t t)
{
    configASSERT(t != NULL);
    if (t->cb) t->cb(t);
}

/* ---------------------------------------------------------------- blocking rule */

typedef bool (*cond_fn)(const void *ctx);

/* Returns true when the condition was met, false on timeout. */
static bool block_until(cond_fn cond, const void *ctx, TickType_t ticks, const char *api)
{
    if (cond(ctx)) return true;
    if (ticks == 0) return false;

    if (block_hook != NULL && !in_hook) {
        in_hook = true;
        block_hook();
        in_hook = false;
        if (cond(ctx)) return true;
    }
    if (ticks == portMAX_DELAY) {
        deadlock_seen = true;
        if (!expect_deadlock) {
            char msg[96];
            snprintf(msg, sizeof(msg), "fake_rtos: deadlock in %s", api);
            TEST_FAIL_MESSAGE(msg);
        }
        return false;
    }
    now_tick += ticks;
    return false;
}

/* ---------------------------------------------------------------- tasks */

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint16_t stack_depth,
                       void *params, UBaseType_t priority, TaskHandle_t *created)
{
    (void)params;
    if (fail_create_countdown > 0 && --fail_create_countdown == 0) {
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    struct fake_task *t = new_task_record(name ? name : "");
    t->fn = fn;
    t->priority = priority;
    t->stack_depth = stack_depth;
    t->number = 0;
    tasks_created_count++;
    if (created) *created = t;
    return pdPASS;
}

static struct fake_task *resolve(TaskHandle_t t) { return t ? t : current; }

void vTaskDelete(TaskHandle_t t)
{
    struct fake_task *r = resolve(t);
    configASSERT(!r->is_test);
    r->alive = false;   /* record is kept until fake_rtos_reset() so stale handles stay valid memory */
}

void vTaskSuspend(TaskHandle_t t) { resolve(t)->suspended = true; }
void vTaskSetTaskNumber(TaskHandle_t t, UBaseType_t number) { configASSERT(t != NULL); t->number = number; }
/* Like real FreeRTOS, a NULL handle yields 0 (it does not mean "current task"). */
UBaseType_t uxTaskGetTaskNumber(TaskHandle_t t) { return t ? t->number : 0; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current; }
TickType_t xTaskGetTickCount(void) { return now_tick; }
void vTaskDelay(TickType_t ticks) { now_tick += ticks; }
void vTaskSuspendAll(void) { suspend_depth++; }
BaseType_t xTaskResumeAll(void)
{
    configASSERT(suspend_depth > 0);
    suspend_depth--;
    return pdFALSE;
}
BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }

void fake_rtos_enter_critical(void) { critical_depth++; }
void fake_rtos_exit_critical(void)
{
    configASSERT(critical_depth > 0);
    critical_depth--;
}

/* ---------------------------------------------------------------- notifications */

BaseType_t xTaskNotifyIndexed(TaskHandle_t t, UBaseType_t idx, uint32_t value, eNotifyAction action)
{
    configASSERT(t != NULL);
    configASSERT(t->alive);
    configASSERT(idx < NOTIFY_SLOTS);

    /* Like FreeRTOS, a rejected notification delivers nothing, so it is not logged either. */
    if (action == eSetValueWithoutOverwrite && t->npending[idx]) return pdFAIL;

    if (notify_log_n < NOTIFY_LOG_MAX) {
        notify_log[notify_log_n].target = t;
        notify_log[notify_log_n].index = idx;
        notify_log[notify_log_n].value = value;
        notify_log[notify_log_n].action = action;
        notify_log_n++;
    }
    if (idx == 0) t->bits_sent |= value;

    switch (action) {
    case eNoAction:                break;
    case eSetBits:                 t->nvalue[idx] |= value; break;
    case eIncrement:               t->nvalue[idx] += 1u; break;
    case eSetValueWithOverwrite:   t->nvalue[idx] = value; break;
    case eSetValueWithoutOverwrite: t->nvalue[idx] = value; break;
    }
    t->npending[idx] = true;
    return pdPASS;
}

BaseType_t xTaskNotify(TaskHandle_t t, uint32_t value, eNotifyAction action)
{
    return xTaskNotifyIndexed(t, 0, value, action);
}

typedef struct { struct fake_task *t; UBaseType_t idx; } pending_wait_t;

static bool cond_pending(const void *ctx)
{
    const pending_wait_t *c = ctx;
    return c->t->npending[c->idx];
}

BaseType_t xTaskNotifyWaitIndexed(UBaseType_t idx, uint32_t clear_on_entry, uint32_t clear_on_exit,
                                  uint32_t *value, TickType_t ticks)
{
    configASSERT(idx < NOTIFY_SLOTS);
    /* The block hook may switch `current`, so remember who is waiting. */
    struct fake_task *t = current;
    pending_wait_t c = { t, idx };

    if (!t->npending[idx]) t->nvalue[idx] &= ~clear_on_entry;   /* clear-on-entry only if nothing pending */

    bool got = block_until(cond_pending, &c, ticks, "xTaskNotifyWait");
    if (got) {
        if (value) *value = t->nvalue[idx];
        t->nvalue[idx] &= ~clear_on_exit;
        t->npending[idx] = false;
        return pdTRUE;
    }
    if (value) *value = t->nvalue[idx];
    return pdFALSE;
}

BaseType_t xTaskNotifyWait(uint32_t clear_on_entry, uint32_t clear_on_exit,
                           uint32_t *value, TickType_t ticks)
{
    return xTaskNotifyWaitIndexed(0, clear_on_entry, clear_on_exit, value, ticks);
}

BaseType_t xTaskNotifyStateClear(TaskHandle_t t)
{
    struct fake_task *r = resolve(t);
    BaseType_t was = r->npending[0] ? pdTRUE : pdFALSE;
    r->npending[0] = false;
    return was;
}

/* ---------------------------------------------------------------- queues */

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    configASSERT(length > 0);   /* as in FreeRTOS; also keeps the % length below well defined */
    if (fail_next_queue) { fail_next_queue = false; return NULL; }
    struct fake_queue *q = calloc(1, sizeof(*q));
    TEST_ASSERT_NOT_NULL_MESSAGE(q, "fake_rtos: out of memory");
    q->length = length;
    q->item_size = item_size;
    q->storage = calloc(length, item_size ? item_size : 1);
    TEST_ASSERT_NOT_NULL_MESSAGE(q->storage, "fake_rtos: out of memory");
    q->next = queues;
    queues = q;
    return q;
}

static bool cond_queue_has_room(const void *ctx) { const struct fake_queue *q = ctx; return q->count < q->length; }
static bool cond_queue_has_item(const void *ctx) { const struct fake_queue *q = ctx; return q->count > 0; }

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
    configASSERT(q != NULL);
    if (!block_until(cond_queue_has_room, q, ticks, "xQueueSend")) return errQUEUE_FULL;
    UBaseType_t tail = (q->head + q->count) % q->length;
    memcpy(&q->storage[tail * q->item_size], item, q->item_size);
    q->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *buffer, TickType_t ticks)
{
    configASSERT(q != NULL);
    if (!block_until(cond_queue_has_item, q, ticks, "xQueueReceive")) return pdFALSE;
    memcpy(buffer, &q->storage[q->head * q->item_size], q->item_size);
    q->head = (q->head + 1u) % q->length;
    q->count--;
    return pdTRUE;
}

/* ---------------------------------------------------------------- event groups */

EventGroupHandle_t xEventGroupCreate(void)
{
    if (fail_next_eg) { fail_next_eg = false; return NULL; }
    struct fake_event_group *e = calloc(1, sizeof(*e));
    TEST_ASSERT_NOT_NULL_MESSAGE(e, "fake_rtos: out of memory");
    e->next = egs;
    egs = e;
    return e;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t eg, EventBits_t bits)
{
    configASSERT(eg != NULL);
    eg->bits |= bits;
    return eg->bits;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t eg, EventBits_t bits)
{
    configASSERT(eg != NULL);
    EventBits_t before = eg->bits;
    eg->bits &= ~bits;
    return before;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t eg)
{
    configASSERT(eg != NULL);
    return eg->bits;
}

typedef struct { struct fake_event_group *eg; EventBits_t want; bool all; } eg_wait_t;

static bool cond_eg(const void *ctx)
{
    const eg_wait_t *w = ctx;
    return w->all ? ((w->eg->bits & w->want) == w->want) : ((w->eg->bits & w->want) != 0u);
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg, EventBits_t bits_to_wait_for,
                                BaseType_t clear_on_exit, BaseType_t wait_for_all, TickType_t ticks)
{
    configASSERT(eg != NULL);
    configASSERT(bits_to_wait_for != 0u);   /* real FreeRTOS asserts this too */
    eg_wait_t w = { eg, bits_to_wait_for, wait_for_all != pdFALSE };
    bool ok = block_until(cond_eg, &w, ticks, "xEventGroupWaitBits");
    EventBits_t value = eg->bits;            /* value before clearing */
    if (ok && clear_on_exit) eg->bits &= ~bits_to_wait_for;
    return value;
}

/* ---------------------------------------------------------------- semaphores */

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    struct fake_semaphore *s = calloc(1, sizeof(*s));
    TEST_ASSERT_NOT_NULL_MESSAGE(s, "fake_rtos: out of memory");
    s->next = sems;
    sems = s;
    return s;
}

static bool cond_sem_free(const void *ctx) { const struct fake_semaphore *s = ctx; return !s->taken; }

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    configASSERT(s != NULL);
    if (!block_until(cond_sem_free, s, ticks, "xSemaphoreTake")) return pdFALSE;
    s->taken = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    configASSERT(s != NULL);
    if (!s->taken) return pdFALSE;
    s->taken = false;
    return pdTRUE;
}

/* ---------------------------------------------------------------- timers */

TimerHandle_t xTimerCreate(const char *name, TickType_t period, UBaseType_t auto_reload,
                           void *timer_id, TimerCallbackFunction_t callback)
{
    (void)name;
    struct fake_timer *t = calloc(1, sizeof(*t));
    TEST_ASSERT_NOT_NULL_MESSAGE(t, "fake_rtos: out of memory");
    t->period = period;
    t->auto_reload = auto_reload != 0;
    t->id = timer_id;
    t->cb = callback;
    t->next = timers;
    timers = t;
    return t;
}

BaseType_t xTimerStart(TimerHandle_t t, TickType_t ticks)
{
    (void)ticks;
    configASSERT(t != NULL);
    t->active = true;
    return pdPASS;
}

BaseType_t xTimerChangePeriod(TimerHandle_t t, TickType_t new_period, TickType_t ticks)
{
    (void)ticks;
    configASSERT(t != NULL);
    t->period = new_period;
    t->active = true;
    return pdPASS;
}

void *pvTimerGetTimerID(TimerHandle_t t) { configASSERT(t != NULL); return t->id; }
