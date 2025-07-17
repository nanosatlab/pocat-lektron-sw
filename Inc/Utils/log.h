#include "stm32l4xx_hal.h"
#include "stm32l476xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

int _write(int file, char *ptr, int len);

void MX_USART2_UART_Init(void);
