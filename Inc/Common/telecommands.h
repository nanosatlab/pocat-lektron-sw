/*************************************************************************//**
 *  Header file containing the macros for the telecommands
 *  @authors    Arnau Dolz Puig (ADP), arnau.dolz@estudiantat.upc.edu
 *  @date       2024-11-22
 * 
 *  @copyright  This file is part of a project developed at Nano-Satellite and
 *              Payload Laboratory (NanoSat Lab), Universitat Politècnica de
 *              Catalunya - UPC BarcelonaTech.
 ****************************************************************************/
#ifndef INC_TELECOMMANDS_H_
#define INC_TELECOMMANDS_H_


#define RESET2      01  /* Reset the satellite. */
#define EXIT_STATE  02  /* Exit the current state. */

#define TLE         10  /* New TLE received, update the current one*/

// /*COMMS*/
#define SEND_DATA   20  /* Send stored data to GS. */

#define SEND_TELEMETRY 21   /* Send the telemetry/housekeeping data. */
// #define CHANGE_TIMEOUT 23
// #define STOP_TRANSMISIONS 24

// /*PAYLOAD*/
#define ACTIVATE_PAYLOAD 30
// // #define SET_PHOTO_RESOL	31	//Photo Resolution
// // #define PHOTO_COMPRESSION 32

// /*PAYLOAD 2: ELECTROSMOG ANTENNA*/
// // #define F_MIN					41
// // #define F_MAX					42
// // #define DELTA_F				43
// // #define INTEGRATION_TIME		44

// /*GLOBAL*/
// #define SEND_CONFIG 40       /* Send DOWNLINK configuration. */
#define UPLINK_CONFIG 41     /* Send UPLINK configuration. */ 
#define STOP_TRANSMISSION 42 /* STOP TX. */

// #define ACK1 60
// #define ERROR 65
#define BEACON 68

#endif /* INC_TELECOMMANDS_H_ */