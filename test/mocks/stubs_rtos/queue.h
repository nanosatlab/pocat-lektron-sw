#ifndef QUEUE_H
#define QUEUE_H
#include "FreeRTOS.h"

typedef struct fake_queue *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t    xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t    xQueueReceive(QueueHandle_t q, void *buffer, TickType_t ticks);

#endif
