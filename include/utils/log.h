/**
 * @file log.h
 * @brief Debug logging support. 
 * @details It currently simply provides a redirection of standard output to a serial interface.
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
 * @brief Write function for standard output.
 * @details 
 * Implements the _write() system call used by printf() and related functions.
 * Data is transmitted over USART3 using huart3.
 *
 * The function enters a FreeRTOS critical section to prevent concurrent access
 * to the UART during transmission. This implementation is intended for
 * debugging purposes only.
 * @param file File descriptor (unused).
 * @param ptr Pointer to the data buffer to transmit
 * @param len Number of bytes to transmit.
 * @return    Number of bytes transmitted.
 */
void log_init(void);
int _write(int file, char *ptr, int len);
