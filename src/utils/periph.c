/**
 * @file periph.c
 * @brief Global peripheral handle definitions.
 * @details 
 * This file defines the global STM32 HAL peripheral handles that will then 
 * be initialized during system startup in main.c.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-20
 * 
 */

#include "periph.h"

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim5;
SPI_HandleTypeDef hspi2;
UART_HandleTypeDef huart2;
IWDG_HandleTypeDef hiwdg;
RTC_HandleTypeDef hrtc;
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;