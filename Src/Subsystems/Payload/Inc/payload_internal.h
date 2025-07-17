#include "FreeRTOS.h"
#include "main.h"

UART_HandleTypeDef huart4;

static uint32_t waitForNotification(void);
