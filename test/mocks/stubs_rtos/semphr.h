#ifndef SEMPHR_H
#define SEMPHR_H
#include "FreeRTOS.h"

typedef struct fake_semaphore *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t s);

#endif
