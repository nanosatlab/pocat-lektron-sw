/**
 * @file log.c
 * @brief Debug printf redirection through USART2.
 * @details
 * Implements the project _write() hook used by the C library for stdout.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-22
 *
 */

#include "log.h"
#include <string.h>
#include "FreeRTOS.h"
#include "semphr.h"

/** @brief Mutex used to serialize debug UART writes after the scheduler starts. */
static SemaphoreHandle_t uart_mutex = NULL;

void log_init(void)
{
    uart_mutex = xSemaphoreCreateMutex();
    configASSERT(uart_mutex);
}

int _write(int file, char *ptr, int len)
{
    if (uart_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(uart_mutex, portMAX_DELAY);
        HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, 1000);
        xSemaphoreGive(uart_mutex);
    } else {
        HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, 1000);
    }
    return len;
}
