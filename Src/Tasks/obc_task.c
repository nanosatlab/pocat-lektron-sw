/*
 * obc_task.c
 *
 *  Created on: Apr 5, 2023
 *      Author: NilRi
 */

#include "obc_task.h"

void OBC_COMMS_RXFlags()
{
	//uint8_t currentState, previousState, nominal, low, critical;
	uint32_t RX_OBC_NOTIS;

	 if (xTaskNotifyWait(0, 0xFFFFFFFF, &RX_OBC_NOTIS, 0)==pdPASS)
		 {
			if((RX_OBC_NOTIS & EXIT_CONTINGENCY_NOTI)==EXIT_CONTINGENCY_NOTI)
				{
					*currentState = _NOMINAL;
					Send_to_WFQueue((uint8_t *)&currentState,sizeof(currentState),CURRENT_STATE_ADDR,OBCsender);
				}

			if((RX_OBC_NOTIS & EXIT_SUNSAFE_NOTI)==EXIT_SUNSAFE_NOTI)
				{
					*currentState = _CONTINGENCY;
					Send_to_WFQueue((uint8_t *)&currentState,sizeof(currentState),CURRENT_STATE_ADDR,OBCsender);
				}

			if((RX_OBC_NOTIS & EXIT_SURVIVAL_NOTI)==EXIT_SURVIVAL_NOTI)
				{
					*currentState = _SURVIVAL;
					Send_to_WFQueue((uint8_t *)&currentState,sizeof(currentState),CURRENT_STATE_ADDR,OBCsender);
				}
		 }
}
