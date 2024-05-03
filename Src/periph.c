/*
 * peripherals.c
 *
 *  Created on: Feb 28, 2023
 *      Author: NilRi
 */

#include "periph.h"

/*
 * Following the structure of the rest of peripherals,
 * include in every function the new peripheral included.
 */

void Periph_Disable_Clock()
{
    __HAL_RCC_GPIOA_CLK_DISABLE();
    __HAL_RCC_GPIOB_CLK_DISABLE();
    __HAL_RCC_GPIOC_CLK_DISABLE();
    __HAL_RCC_GPIOH_CLK_DISABLE();

    __HAL_RCC_I2C1_CLK_DISABLE();
    __HAL_RCC_RTC_DISABLE();
    __HAL_RCC_SPI2_CLK_DISABLE();
    __HAL_RCC_UART4_CLK_DISABLE();

    __HAL_RCC_TIM16_CLK_DISABLE();
    __HAL_RCC_TIM17_CLK_DISABLE();
    __HAL_RCC_TIM7_CLK_DISABLE();
    __HAL_RCC_TIM5_CLK_DISABLE();

    __HAL_RCC_ADC_CLK_DISABLE();
    __HAL_RCC_DAC1_CLK_DISABLE();

}

void Periph_Disable()
{
	HAL_GPIO_DeInit(GPIOA, GPIO_PIN_All);
	HAL_GPIO_DeInit(GPIOB, GPIO_PIN_All);
	HAL_GPIO_DeInit(GPIOC, GPIO_PIN_All);
	HAL_GPIO_DeInit(GPIOH, GPIO_PIN_All);

	HAL_I2C_DeInit(&hi2c1);
	HAL_RTC_DeInit(&hrtc);
	HAL_SPI_DeInit(&hspi2);
	HAL_UART_DeInit(&huart4);

	HAL_ADC_DeInit(&hadc1);
	HAL_DAC_DeInit(&hdac1);

	HAL_TIM_Base_DeInit(&htim5);
	HAL_TIM_Base_DeInit(&htim7);
	HAL_TIM_Base_DeInit(&htim16);
	HAL_TIM_Base_DeInit(&htim17);
}

void Periph_Disable_IRQ()
{
	HAL_NVIC_DisableIRQ(TIM5_IRQn);
	HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
	HAL_NVIC_DisableIRQ(TIM7_IRQn);
	HAL_NVIC_DisableIRQ(TIM1_UP_TIM16_IRQn);
	HAL_NVIC_DisableIRQ(TIM1_TRG_COM_TIM17_IRQn);
	HAL_NVIC_DisableIRQ(SPI2_IRQn);
	HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
}

void Periph_Enable_Clock()
{
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();

	__HAL_RCC_I2C1_CLK_ENABLE();
	__HAL_RCC_RTC_ENABLE();
	__HAL_RCC_SPI2_CLK_ENABLE();
	__HAL_RCC_UART4_CLK_ENABLE();

	__HAL_RCC_TIM16_CLK_ENABLE();
	__HAL_RCC_TIM17_CLK_ENABLE();
	__HAL_RCC_TIM7_CLK_ENABLE();
	__HAL_RCC_TIM5_CLK_ENABLE();

	__HAL_RCC_ADC_CLK_ENABLE();
	__HAL_RCC_DAC1_CLK_ENABLE();
}

void Periph_Enable()
{
	Init_Peripherals();
}

void Periph_Enable_IRQ()
{
	HAL_NVIC_EnableIRQ(TIM5_IRQn);
	HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
	HAL_NVIC_EnableIRQ(TIM7_IRQn);
	HAL_NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);
	HAL_NVIC_EnableIRQ(TIM1_TRG_COM_TIM17_IRQn);
	HAL_NVIC_EnableIRQ(SPI2_IRQn);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

