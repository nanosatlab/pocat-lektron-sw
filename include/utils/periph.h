/**
 * @file periph.h
 * @brief Peripheral handle declarations and initialization helpers.
 * @details
 * Exposes the global STM32 HAL peripheral handles used across the firmware and
 * the public entry points for peripheral initialization and clock-dependent
 * reconfiguration.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-20
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

/** @brief Global UART4 handle used for debug logging. */
extern UART_HandleTypeDef huart4;

/** @brief Global independent watchdog handle. */
extern IWDG_HandleTypeDef hiwdg;

/** @brief Global RTC handle. */
extern RTC_HandleTypeDef hrtc;

/** @brief Global ADC1 handle. Used for internal temperature sensor. */
extern ADC_HandleTypeDef hadc1;

/** @brief Global I2C1 handle. Used for DS2872 battery sensor. */
extern I2C_HandleTypeDef hi2c1;

/* ── EPS board pins (LTC4040 PMIC + battery heater) ─────────────────────────
 * Single source of truth for the EPS pin map. periph_gpio_init() puts these
 * pins in a defined state at boot, before any task runs; the EPS hardware
 * layer (eps_hw_pocat.c, ltc4040.c) reads and drives them through the same
 * macros. The LTC4040 status outputs are open-drain and active-low. */
#define EPS_PIN_CHRG_PORT       GPIOB        /**< !CHRG: charging status */
#define EPS_PIN_CHRG            GPIO_PIN_2
#define EPS_PIN_PFO_PORT        GPIOB        /**< !PFO: power-fail (input power lost) */
#define EPS_PIN_PFO             GPIO_PIN_5
#define EPS_PIN_FAULT_PORT      GPIOC        /**< !FAULT: charger fault */
#define EPS_PIN_FAULT           GPIO_PIN_4
#define EPS_PIN_CHRGOFF_PORT    GPIOA        /**< CHRGOFF: high = charging disabled */
#define EPS_PIN_CHRGOFF         GPIO_PIN_3
#define EPS_PIN_CLPROG_PORT     GPIOA        /**< CLPROG: input-current monitor (analog) */
#define EPS_PIN_CLPROG          GPIO_PIN_4
#define EPS_ADC_CHANNEL_CLPROG  ADC_CHANNEL_9
#define EPS_PIN_HEATER_PORT     GPIOB        /**< Battery heater: push-pull, active-high */
#define EPS_PIN_HEATER          GPIO_PIN_10

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

/**
 * @brief Perform a single blocking ADC1 conversion on the given channel.
 * @details Configures the channel, starts the conversion, polls for completion,
 *          reads the value, and stops the ADC. The pin backing the channel must
 *          already be configured as GPIO_MODE_ANALOG.
 * @param channel ADC channel (e.g. ADC_CHANNEL_9 for PA4, ADC_CHANNEL_VREFINT).
 * @param[out] result Raw 12-bit ADC value (0–4095).
 * @return HAL_OK on success, HAL_ERROR on channel config failure,
 *         HAL_TIMEOUT if the conversion does not complete within 100 ms.
 */
HAL_StatusTypeDef adc_read_channel(uint32_t channel, uint32_t *result);

#endif /* INC_UTILS_PERIPH_H_ */
