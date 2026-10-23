#ifndef TIMERS_H
#define TIMERS_H
#include "FreeRTOS.h"

typedef struct fake_timer *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t timer);

TimerHandle_t xTimerCreate(const char *name, TickType_t period, UBaseType_t auto_reload,
                           void *timer_id, TimerCallbackFunction_t callback);
BaseType_t    xTimerStart(TimerHandle_t t, TickType_t ticks);
BaseType_t    xTimerChangePeriod(TimerHandle_t t, TickType_t new_period, TickType_t ticks);
void         *pvTimerGetTimerID(TimerHandle_t t);

#endif
