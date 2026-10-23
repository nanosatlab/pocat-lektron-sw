#ifndef STM32L4XX_HAL_H
#define STM32L4XX_HAL_H

#include <stdint.h>

typedef enum {
    HAL_OK = 0x00U,
    HAL_ERROR = 0x01U,
    HAL_BUSY = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

#define FLASH_PAGE_SIZE 2048u

/* Opaque peripheral handle types. The real layouts are irrelevant on the host;
 * the instances live in fake_hal.c so periph.h's `extern` declarations link. */
typedef struct { int dummy; } TIM_HandleTypeDef;
typedef struct { int dummy; } SPI_HandleTypeDef;
typedef struct { int dummy; } UART_HandleTypeDef;
typedef struct { int dummy; } IWDG_HandleTypeDef;
typedef struct { int dummy; } RTC_HandleTypeDef;
typedef struct { int dummy; } ADC_HandleTypeDef;

/* Counted by fake_hal.c (see fake_hal_iwdg_refresh_count()). */
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *hiwdg);

#endif
