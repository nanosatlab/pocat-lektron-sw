/**
 * @file log.h
 * @brief Debug printf redirection through UART4.
 * @details
 * Call log_init() after UART4 initialization to create the mutex used for
 * serialized UART output once FreeRTOS is running. Before the scheduler starts,
 * printf() output is transmitted directly.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-22
 */

#include "stm32l4xx_hal.h"
#include "stm32l476xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "periph.h"


/**
 * @brief Initialize debug logging synchronization.
 * @details
 * Creates the FreeRTOS mutex used to serialize UART access once the scheduler
 * is running. Call this once after peripheral initialization and before tasks
 * start using printf().
 */
void log_init(void);

/**
 * @brief Low-level write hook for standard output.
 * @details
 * Implements the _write() system call used by printf() and related functions.
 * Data is transmitted through UART4 using huart4.
 *
 * When the FreeRTOS scheduler is running and log_init() has created the mutex,
 * the function serializes UART access with that mutex. Before the scheduler is
 * running, output is transmitted directly.
 *
 * @param file File descriptor requested by the C library; ignored.
 * @param ptr Pointer to the data buffer to transmit.
 * @param len Number of bytes to transmit.
 * @return Number of bytes accepted for transmission. This implementation
 *         returns len regardless of HAL_UART_Transmit() status.
 */
int _write(int file, char *ptr, int len);
