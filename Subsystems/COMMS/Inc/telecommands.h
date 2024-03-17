/*
 * telecommands.h
 *
 *  Created on: 16 feb. 2023
 *      Author: NilRi
 */

#ifndef INC_TELECOMMANDS_H_
#define INC_TELECOMMANDS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stm32l4xx_hal.h>

/******TELECOMMANDS*****/

/*OBC*/
#define RESET2  				01	/*The PQ might take a reset*/
#define EXIT_STATE				02

//#define NACKDATA  			08	/*If it is received if the GS do not receive all the segments of the data.
 	 	 	 	 	 	 	 	 //*The PQ will send since the last segment received correctly.*/
/*ADCS*/
#define TLE  					10 /*Packet from GS with the new TLE, update it inside memory
 	 	 	 	 	 	 	  	  	  the SPG4 uses it to propagate the orbit*/
#define ADCS_CALIBRATION		11


/*COMMS*/
#define SEND_DATA  				20	/*If the acquired photo or spectogram is needed to be send to GS*/
#define SEND_TELEMETRY 			21
#define STOP_SENDING_DATA  		22
#define CHANGE_TIMEOUT			23

#define ACK_DATA  				24	/*It is received when all the data is received correctly*/

//NACK
#define NACK_TELEMETRY			25
#define NACK_CONFIG				26

/*PAYLOAD*/
#define ACTIVATE_PAYLOAD 		30	/*Might rotate the PQ into the right position +
								wait until it is in the position where the picture is wanted to be taken.*/
//#define SET_PHOTO_RESOL	31	//Photo Resolution
//#define PHOTO_COMPRESSION 32

/*PAYLOAD 2: ELECTROSMOG ANTENNA*/
//#define F_MIN					41
//#define F_MAX					42
//#define DELTA_F				43
//#define INTEGRATION_TIME		44

/*GLOBAL*/
#define SEND_CONFIG				40	//Send all configuration
#define UPLINK_CONFIG			41	//Send all configuration

#define ACK1					60
#define ERROR					65
#define BEACON					68



#endif /* INC_TELECOMMANDS_H_ */
