/**
 * @file periph.h
 * @brief Global peripheral handle declarations.
 * @details 
 * This file declares global STM32 HAL peripheral handles. These handles are defined
 * in periph.c and initialized during system startup in main.c.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-20
 * 
 */

#include "stm32l4xx_hal.h"

/** @brief Global SPI handle. Used for communication with the SX1262. 
*/
extern SPI_HandleTypeDef hspi2;  

/** @brief Global TIM2 handle. Temporarily declared for tone generation with Radiolib.
 *  @todo Might not be needed for SX1262 operation, remove if not used.
 */
extern TIM_HandleTypeDef htim2;

/** @brief Global TIM5 handle. Used as a microsecond timebase. */
extern TIM_HandleTypeDef htim5;

/** @brief Global UART2 handle. Used for debug logging. */
extern UART_HandleTypeDef huart2;

/** @brief Global Independent Watchdog handle. */
extern IWDG_HandleTypeDef hiwdg;

/** @brief Global RTC handle. */
extern RTC_HandleTypeDef hrtc;

/** @brief Global ADC1 handle. Used for internal temperature sensor. */
extern ADC_HandleTypeDef hadc1;

/** @brief Global I2C1 handle. Used for DS2872 battery sensor. */
extern I2C_HandleTypeDef hi2c1;