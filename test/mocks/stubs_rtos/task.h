#ifndef TASK_H
#define TASK_H
/* Host stub of task.h. Plain functions instead of FreeRTOS's macros. */
#include "FreeRTOS.h"

typedef struct fake_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

typedef enum {
    eNoAction = 0,
    eSetBits,
    eIncrement,
    eSetValueWithOverwrite,
    eSetValueWithoutOverwrite
} eNotifyAction;

#define taskSCHEDULER_SUSPENDED   ((BaseType_t)0)
#define taskSCHEDULER_NOT_STARTED ((BaseType_t)1)
#define taskSCHEDULER_RUNNING     ((BaseType_t)2)

/** @brief Start of a wait, for xTaskCheckForTimeOut(); the same fields as FreeRTOS's, which uptime.c reads. */
typedef struct {
    BaseType_t xOverflowCount;    /**< Tick overflows when the wait started, or when it was last checked. */
    TickType_t xTimeOnEntering;   /**< Tick count at that moment. */
} TimeOut_t;

void fake_rtos_enter_critical(void);
void fake_rtos_exit_critical(void);
#define taskENTER_CRITICAL() fake_rtos_enter_critical()
#define taskEXIT_CRITICAL()  fake_rtos_exit_critical()

BaseType_t   xTaskCreate(TaskFunction_t fn, const char *name, uint16_t stack_depth,
                         void *params, UBaseType_t priority, TaskHandle_t *created);
void         vTaskDelete(TaskHandle_t t);
void         vTaskSuspend(TaskHandle_t t);
void         vTaskSetTaskNumber(TaskHandle_t t, UBaseType_t number);
UBaseType_t  uxTaskGetTaskNumber(TaskHandle_t t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t   xTaskGetTickCount(void);
void         vTaskDelay(TickType_t ticks);
void         vTaskSuspendAll(void);
BaseType_t   xTaskResumeAll(void);
BaseType_t   xTaskGetSchedulerState(void);
void         vTaskSetTimeOutState(TimeOut_t *timeout);
BaseType_t   xTaskCheckForTimeOut(TimeOut_t *timeout, TickType_t *ticks_to_wait);

BaseType_t xTaskNotify(TaskHandle_t t, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyIndexed(TaskHandle_t t, UBaseType_t idx, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyWait(uint32_t clear_on_entry, uint32_t clear_on_exit,
                           uint32_t *value, TickType_t ticks);
BaseType_t xTaskNotifyWaitIndexed(UBaseType_t idx, uint32_t clear_on_entry, uint32_t clear_on_exit,
                                  uint32_t *value, TickType_t ticks);
BaseType_t xTaskNotifyStateClear(TaskHandle_t t);

#endif
