/*
 * obc.h
 *
 *  Created on: Nov 9, 2022
 *      Author: NilRi
 */

#ifndef INC_OBC_H_
#define INC_OBC_H_

#include "clock.h"
#include "main.h"
#include "periph.h"
#include <softTim.h>
#include <stdbool.h>

#include "obc_task.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#include "notifications.h"

#include "definitions.h"
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_adc.h"
#include "stm32l4xx_hal_dac.h"
#include "stm32l4xx_hal_i2c.h"
#include "stm32l4xx_hal_spi.h"
#include "stm32l4xx_hal_tim.h"
#include "stm32l4xx_hal_uart.h"

#include "board.h"
#include "gpio-board.h"

#include <stdbool.h>
#include <stdint.h>
#include <stm32l4xx_hal.h>

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

/****TASK HANDLE VARIABLES****/
extern TaskHandle_t FLASH_Handle;
extern TaskHandle_t COMMS_Handle;
extern TaskHandle_t ADCS_Handle;
extern TaskHandle_t PAYLOAD_Handle;
extern TaskHandle_t EPS_Handle;
extern TaskHandle_t RFI_Handle;

/****EVENT GROUPS****/
extern EventGroupHandle_t xEventGroup;

/*******SOFTWARE TIMERS********/
extern TimerHandle_t xTimerObc;
extern TimerHandle_t xTimerComms;
extern TimerHandle_t xTimerAdcs;
extern TimerHandle_t xTimerEps;
extern TimerHandle_t xTimerPayload;
extern TimerHandle_t xTimerPhoto;
extern TimerHandle_t xTimerRF;

/****SOFTWARE TIMERS NOTIS****/
#define sTIM_OBC_NOTI 1 << 0
#define sTIM_COMMS_NOTI 1 << 1
#define sTIM_ADCS_NOTI 1 << 2
#define sTIM_PAYLOAD_NOTI 1 << 3
#define sTIM_EPS_NOTI 1 << 4

/****SOFTWARE TIMERS DEFINITIONS****/

/***ACTIVE PERIODS IN ms***/
#define OBC_ACTIVE_PERIOD 3000
#define COMMS_ACTIVE_PERIOD 3000
#define ADCS_ACTIVE_PERIOD 3000
#define PAYLOAD_ACTIVE_PERIOD 3000
#define EPS_ACTIVE_PERIOD 3000

/***SUSPENDED PERIODS IN ms***/
#define OBC_SUSPENDED_PERIOD 3000
#define COMMS_SUSPENDED_PERIOD 3000
#define ADCS_SUSPENDED_PERIOD 3000
#define PAYLOAD_SUSPENDED_PERIOD 3000
#define EPS_SUSPENDED_PERIOD 3000

/********TASK STACK SIZES********/
#define sTIM_STACK_SIZE 1000
//      DAEMON_STACK_SIZE       500    Defined at CubeMX
#define FLASH_STACK_SIZE 9000
#define PAYLOAD_STACK_SIZE 4000
#define OBC_STACK_SIZE 1000
#define COMMS_STACK_SIZE 3000
#define EPS_STACK_SIZE 250
#define ADCS_STACK_SIZE 250

/********TASK PRIORITIES********/
#define sTIM_PRIORITY 8
//      DAEMON_PRIORITY         7      Defined at CubeMX
#define FLASH_PRIORITY 6
#define PAYLOAD_PRIORITY 5
#define ADCS_PRIORITY 4
#define EPS_PRIORITY 3
#define OBC_PRIORITY 2
#define COMMS_PRIORITY 1

/****EVENT FLAGS****/

/**COMMS EVENTS**/
#define COMMS_RXIRQFlag_EVENT 1 << 0 // IRQ flag pending on the transceiver
#define COMMS_RXNOTI_EVENT 1 << 1    // Task notifications pending on COMMS

#define COMMS_TELECOMMAND_EVENT                                                \
  1 << 2 // Telecommand has been processed succesfully
#define COMMS_WRONGPACKET_EVENT 1 << 3 // The packet received is not ours

/******ADCS EVENTS******/
#define ADCS_POINTINGDONE_EVENT                                                \
  1 << 4 /*                                                                    \
          * When ADCS has finished pointing to the                             \
          * given location sends this event to payload.                        \
          */

/******PAYLOAD EVENTS******/
#define PAYLOAD_TIMEFORPHOTO_EVENT                                             \
  1 << 5 /* When the timer controlling the time for photo                      \
          * is triggered it notifies the payload task.                         \
          */
#define PAYLOAD_TIMERF_EVENT 1 << 6

void OBC_Init();
void OBC_Nominal();
void OBC_Contingency();
void OBC_Sunsafe();
void OBC_Survival();

void OBC_COMMSFlags();

#endif /* INC_OBC_H_ */
