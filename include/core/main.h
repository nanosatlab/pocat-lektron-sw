/**
 * @file main.h
 * @brief Application entry point definitions.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-20
 * @todo Update FreeRTOSConfig.h with v11.2 macros. This is not critical.
 */

#ifndef __MAIN_H
#define __MAIN_H

#include "stm32l4xx_hal.h"
#include "stm32l476xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
#include "periph.h"


/** @brief MSP post-initialization callback for TIM peripheral GPIO configuration. */
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/** @brief Return the OBC task handle (created in main). */
TaskHandle_t main_get_obc_handle(void);

/**
 * @brief STM32 HAL fatal-error hook (defined in main.c).
 * @details Called by the HAL/MSP and peripheral layers on unrecoverable init
 *          failures; forwards to error_fatal() (record + clean reset).
 */
void Error_Handler(void);

#endif /* __MAIN_H */
