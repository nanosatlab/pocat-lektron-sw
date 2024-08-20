/*
 * software_timers.h
 *
 *  Created on: Mar 26, 2023
 *      Author: NilRi
 */

#ifndef INC_SOFTTIM_H_
#define INC_SOFTTIM_H_

#include "main.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "timers.h"

#include "Subsystems/obc.h"
#include <stdbool.h>

extern TaskHandle_t ADCS_Handle;
extern TaskHandle_t OBC_Handle;
extern TaskHandle_t COMMS_Handle;
extern TaskHandle_t PAYLOAD_Handle;
extern TaskHandle_t EPS_Handle;
extern TaskHandle_t sTIM_Handle;

extern EventGroupHandle_t xEventGroup;

extern TimerHandle_t xTimerObc;
extern TimerHandle_t xTimerComms;
extern TimerHandle_t xTimerAdcs;
extern TimerHandle_t xTimerEps;
extern TimerHandle_t xTimerPayload;
extern TimerHandle_t xTimerPhoto;

void ObcTimerCallback(TimerHandle_t xTimer);
void CommsTimerCallback(TimerHandle_t xTimer);
void AdcsTimerCallback(TimerHandle_t xTimer);
void EpsTimerCallback(TimerHandle_t xTimer);
void PayloadTimerCallback(TimerHandle_t xTimer);
void PhotoTimerCallback(TimerHandle_t xTimer);
void RFTimerCallback(TimerHandle_t xTimer);

#endif /* INC_SOFTTIM_H_ */
