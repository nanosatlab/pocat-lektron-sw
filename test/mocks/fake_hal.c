#include "fake_hal.h"

TIM_HandleTypeDef  htim2;
TIM_HandleTypeDef  htim5;
SPI_HandleTypeDef  hspi2;
UART_HandleTypeDef huart4;
IWDG_HandleTypeDef hiwdg;
RTC_HandleTypeDef  hrtc;
ADC_HandleTypeDef  hadc1;

static int iwdg_refresh_count;

void fake_hal_reset(void) { iwdg_refresh_count = 0; }
int  fake_hal_iwdg_refresh_count(void) { return iwdg_refresh_count; }

HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h)
{
    (void)h;
    iwdg_refresh_count++;
    return HAL_OK;
}
