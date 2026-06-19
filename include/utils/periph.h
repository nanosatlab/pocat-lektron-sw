/**
 * @file periph.h
 * @brief Peripheral handle declarations and initialization helpers.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-20
 * TODO:
 * Exposes the global STM32 HAL peripheral handles used across the firmware and
 * the public entry points for peripheral initialization and clock-dependent
 * reconfiguration.
 */

#ifndef INC_UTILS_PERIPH_H_
#define INC_UTILS_PERIPH_H_

#include "clock.h"
#include "stm32l4xx_hal.h"

/** @brief Global TIM2 handle. Temporarily declared for tone generation with Radiolib.
 *  @todo Might not be needed for SX1262 operation, remove if not used.
 */
extern TIM_HandleTypeDef htim2;

/** @brief Global TIM5 handle used as the microsecond timebase. */
extern TIM_HandleTypeDef htim5;

/** @brief Global SPI2 handle used for SX1262 communication. */
extern SPI_HandleTypeDef hspi2;

/** @brief Global USART3 handle used for debug logging. */
extern UART_HandleTypeDef huart3;

/** @brief Global independent watchdog handle. */
extern IWDG_HandleTypeDef hiwdg;

/** @brief Global RTC handle. */
extern RTC_HandleTypeDef hrtc;

/** @brief Global ADC1 handle. Used for internal temperature sensor. */
extern ADC_HandleTypeDef hadc1;

/**
 * @brief Initialize all board peripherals for a system clock frequency.
 * @param freq System clock selection.
 */
void periph_init_for_freq(ClockFreq_t freq);

/**
 * @brief Reconfigure peripherals that depend on the system clock frequency.
 * @param freq New system clock selection.
 */
void periph_reconfigure_for_freq(ClockFreq_t freq);

#endif /* INC_UTILS_PERIPH_H_ */
