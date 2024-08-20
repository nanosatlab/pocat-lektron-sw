/*
 * peripherals.h
 *
 *  Created on: Feb 28, 2023
 *      Author: NilRi
 */

#ifndef INC_PERIPH_H_
#define INC_PERIPH_H_

#include "main.h"

#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_gpio.h"
#include "stm32l4xx_hal_tim.h"
#include "stm32l4xx_hal_adc.h"
#include "stm32l4xx_hal_dac.h"
#include "stm32l4xx_hal_spi.h"
#include "stm32l4xx_hal_uart.h"
#include "stm32l4xx_hal_i2c.h"

/****PERIPHERAL VARIABLES****/
extern ADC_HandleTypeDef hadc1;
extern DAC_HandleTypeDef hdac1;
extern I2C_HandleTypeDef hi2c1;
extern RTC_HandleTypeDef hrtc;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim5;
extern TIM_HandleTypeDef htim7;
extern TIM_HandleTypeDef htim16;
extern TIM_HandleTypeDef htim17;
extern UART_HandleTypeDef huart4;

void Periph_Disable_Clock();
void Periph_Disable();
void Periph_Disable_IRQ();
void Periph_Enable_Clock();
void Periph_Enable();
void Periph_Enable_IRQ();

#endif /* INC_PERIPH_H_ */
