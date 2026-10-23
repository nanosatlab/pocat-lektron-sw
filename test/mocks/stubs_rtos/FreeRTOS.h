#ifndef FREERTOS_H
#define FREERTOS_H
/* Host stub of FreeRTOS.h. Declarations only; behaviour lives in mocks/fake_rtos.c. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint32_t      TickType_t;
typedef long          BaseType_t;
typedef unsigned long UBaseType_t;

#define pdFALSE  ((BaseType_t)0)
#define pdTRUE   ((BaseType_t)1)
#define pdPASS   (pdTRUE)
#define pdFAIL   (pdFALSE)
#define errQUEUE_FULL                            ((BaseType_t)0)
#define errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY    ((BaseType_t)-1)

#define portMAX_DELAY 0xFFFFFFFFu

/* The target runs a 1 kHz tick, so 1 ms == 1 tick. */
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

void fake_rtos_assert_failed(const char *file, int line);
#define configASSERT(x) do { if (!(x)) { fake_rtos_assert_failed(__FILE__, __LINE__); } } while (0)

#endif
