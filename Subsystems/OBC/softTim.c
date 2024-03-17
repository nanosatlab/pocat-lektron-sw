/*
 * software_timers.c
 *
 *  Created on: Mar 26, 2023
 *      Author: NilRi
 */
#include "softTim.h"

void ObcTimerCallback(TimerHandle_t xTimer)
{
	//xTaskNotify(sTIM_Handle,sTIM_OBC_NOTI,eSetBits);
}

void CommsTimerCallback(TimerHandle_t xTimer)
{
	//xTaskNotify(sTIM_Handle,sTIM_COMMS_NOTI,eSetBits);
}

void AdcsTimerCallback(TimerHandle_t xTimer)
{
	//xTaskNotify(sTIM_Handle,sTIM_ADCS_NOTI,eSetBits);
}

void EpsTimerCallback(TimerHandle_t xTimer)
{
	//xTaskNotify(sTIM_Handle,sTIM_EPS_NOTI,eSetBits);
}


void PayloadTimerCallback(TimerHandle_t xTimer)
{
	//xTaskNotify(sTIM_Handle,sTIM_PAYLOAD_NOTI,eSetBits);
}

void PhotoTimerCallback(TimerHandle_t xTimer)
{
	xEventGroupSetBits(xEventGroup, PAYLOAD_TIMEFORPHOTO_EVENT);
}

void RFTimerCallback(TimerHandle_t xTimer)
{
	xEventGroupSetBits(xEventGroup, PAYLOAD_TIMERF_EVENT);

}


