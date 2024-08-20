/*
 * utils.h
 *
 *  Created on: 23 nov. 2022
 *      Author: NilRi
 */

#ifndef INC_NOTIFICATIONS_H_
#define INC_NOTIFICATIONS_H_

/******************NOTIFICATIONS******************/

/*********Sent by COMMS*********/
// To : OBC
#define RESET_NOTI				1<<0
#define GS_NOTI					1<<1
#define EXIT_CONTINGENCY_NOTI	1<<2
#define EXIT_SUNSAFE_NOTI       1<<3
#define EXIT_SURVIVAL_NOTI      1<<4
#define SET_TIME_NOTI 			1<<5
#define NOMINAL_NOTI 			1<<6
#define LOW_NOTI				1<<7
#define CRITICAL_NOTI 			1<<8

// To : OBC -> ADCS
#define CTEKP_NOTI				1<<9
#define TLE_NOTI				1<<10
#define GYRORES_NOTI			1<<11
#define CALIBRATION_NOTI		1<<12
#define POINTING_NOTI			1<<13
#define DETUMBLING_NOTI			1<<14
#define CHECKROTATE_NOTI		1<<15


// To : OBC -> PAYLOAD
#define TAKEPHOTO_NOTI			1<<16


/*********Sent by OBC*********/
// To : ADCS
#define STOP_POINTING_NOTI		1<<17
#define ADCS_TELEMETRY_NOTI     1<<18

// To : COMMS, ADCS, PAYLOAD
#define CONTINGENCY_NOTI		1<<19
#define SUNSAFE_NOTI			1<<20
#define WAKEUP_NOTI				1<<21

// To : COMMS
#define SURVIVAL_NOTI       	1<<22

// To: COMMS, PAYLOAD
#define ANTENNA_DEPLOYMENT_NOTI 1<<23

// To: EPS
#define EPS_BATTERY_NOTI        1<<24
#define EPS_HEATER_NOTI         1<<25
#define EPS_TELEMETRY_NOTI      1<<26

/*********Sent by ADCS*********/
// To : OBC -> PAYLOAD
#define POINTING_DONE_NOTI		1<<27


/*********Sent by PAYLOAD*********/
// To : OBC -> COMMS
#define DONEPHOTO_NOTI			1<<28
#define PAYLOAD_ERROR_NOTI  	1<<29

#endif /* INC_NOTIFICATIONS_H_ */
