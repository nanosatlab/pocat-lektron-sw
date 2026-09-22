/*
 * obc_task.h
 *
 *  Created on: Apr 5, 2023
 *      Author: NilRi
 */

#ifndef INC_OBC_TASK_H_
#define INC_OBC_TASK_H_

#include <stdbool.h>
#include <stdlib.h>
#include "obc.h"

#include "definitions.h"
#include "flash.h"
#include "notifications.h"

#include "softTim.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

/****PERIPHERAL VARIABLES****/
extern RTC_HandleTypeDef hrtc;

/****TASK HANDLE VARIABLES****/
extern TaskHandle_t FLASH_Handle;
extern TaskHandle_t COMMS_Handle;
extern TaskHandle_t ADCS_Handle;
extern TaskHandle_t PAYLOAD_Handle;
extern TaskHandle_t EPS_Handle;

/****FREERTOS VARIABLES****/
extern EventGroupHandle_t xEventGroup;

extern TimerHandle_t xTimerPhoto;

void OBC_COMMS_RXFlags();

#endif /* INC_OBC_TASK_H_ */
