

#ifndef INC_COMMS_H_
#define INC_COMMS_H_

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

#include <string.h>
#include <math.h>


#include "board.h"
#include "sx126x.h"
#include "radio.h"
#include "sx126x-board.h"

#include "flash.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "notifications.h"

typedef enum {
    ERR,
    PING,
    TRANSIT_TO_NM,
    TRANSIT_TO_CM,
    TRANSIT_TO_SSM,
    TRANSIT_TO_SM,
    UPLOAD_ADCS_CALIBRATION,
    UPLOAD_ADCS_TLE,
    UPLOAD_COMMS_CONFIG,
    COMMS_UPLOAD_PARAMS,
    UPLOAD_UNIX_TIME,
    UPLOAD_EPS_TH,
    UPLOAD_PL_CONFIG,
    DOWNLINK_CONFIG,
    EPS_HEATER_ENABLE,
    EPS_HEATER_DISABLE,
    POL_PAYLOAD_SHUT,
    POL_ADCS_SHUT,
    POL_BURNCOMMS_SHUT,
    POL_HEATER_SHUT,
    POL_PAYLOAD_ENABLE,
    POL_ADCS_ENABLE,
    POL_BURNCOMMS_ENABLE,
    POL_HEATER_ENABLE,
    CLEAR_PL_DATA,
    CLEAR_FLASH,
    CLEAR_HT,
    COMMS_STOP_TX,
    COMMS_RESUME_TX,
    COMMS_IT_DOWNLINK,
    COMMS_HT_DOWNLINK,
    PAYLOAD_SCHEDULE,
    PAYLOAD_DEACTIVATE,
    PAYLOAD_SEND_DATA,
    OBC_HARD_REBOOT,
    OBC_SOFT_REBOOT,
    OBC_PERIPH_REBOOT,
    OBC_DEBUG_MODE
} telecommandIDS;

extern TaskHandle_t OBC_Handle;
extern EventGroupHandle_t xEventGroup;

#define RF_ID1								128
#define RF_ID2								255

#define TX_OUTPUT_POWER                     18        // dBm

#define LORA_BANDWIDTH                      0         // [0: 125 kHz,
													  //  1: 250 kHz,
													  //  2: 500 kHz,
													  //  3: Reserved]
#define LORA_SPREADING_FACTOR               11         // [SF7..SF12]
#define LORA_CODINGRATE                     1         // [1: 4/5,
													  //  2: 4/6,
													  //  3: 4/7,
													  //  4: 4/8]
#define LORA_PREAMBLE_LENGTH                8//108    // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT                 100       // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON          false
#define LORA_IQ_INVERSION_ON                false
#define LORA_FIX_LENGTH_PAYLOAD_LEN         19
#define WINDOW_SIZE							20

#define RX_TIMEOUT_VALUE                    4000
#define BUFFER_SIZE							100

#define TLE_PACKET_SIZE						34
#define TELEMETRY_PACKET_SIZE				29
#define CALIBRATION_PACKET_SIZE				36
#define CONFIG_PACKET_SIZE					18
#define DATA_PACKET_SIZE                    39
#define TIMEOUT_PACKET_SIZE                 2

#define BEACON_OP 							1
#define ACK_OP  							2
#define DATA_OP 							3
#define DOWNLINK_CONFIG_OP 					4

#define TLCOUNTER_MAX						146
#define BEACON_PL_LEN						28

//HEADER for all packets

#define MISSION_ID   0x01   // PoCat-Lektron
#define POCAT2_ID    0x02   // PoCat-4 (????)

/*!
 *	CAD performance evaluation's parameters
 */
#define RX_FW       1
#define TX_FW       0   //TX_FW is only for test
#define FULL_DBG    1   //Active all traces

// Apps CAD timer
#define CAD_TIMER_TIMEOUT       1000        //Define de CAD timer's timeout here

TimerEvent_t RxAppTimeoutTimer;
#define RX_TIMER_TIMEOUT        4000        //Define de CAD timer's timeout here

//CAD parameters
#define CAD_SYMBOL_NUM          LORA_CAD_02_SYMBOL
#define CAD_DET_PEAK            23
#define CAD_DET_MIN             1
#define CAD_TIMEOUT_MS          2000
#define NB_TRY                  10



#define UPLINK_BUFFER_SIZE		100
#define ACK_PAYLOAD_LENGTH		5			//ACK payload data length
#define CONFIG_SIZE		        13

//OTHER
//#define ML (TELEMETRY_PACKET_SIZE + 3 + NPAR)
#define RATE_CON 				2
#define ORDER_CON 				7
#define BLOCK_ROW_INTER			4
#define BLOCK_COL_INTER			4

#define PIN1	 				200	//Pin of the GS to avoid hacking telecommands
#define PIN2					157	//Second byte of the PIN of the GS


#define MISSION_ID				0x74
#define POCKETQUBE_ID			0x72
#define POQUETQUBE_ID2			0x73




#ifndef INC_TELECOMMANDS_H_
#define INC_TELECOMMANDS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stm32l4xx_hal.h>

/******TELECOMMANDS*****/

/*OBC*/
#define RESET2  				01	/*The PQ might take a reset*/
#define EXIT_STATE				02

/*ADCS*/
#define TLE  					10 /*Packet from GS with the new TLE, update it inside memory
 	 	 	 	 	 	 	  	  	  the SPG4 uses it to propagate the orbit*/

/*COMMS*/
#define SEND_DATA  				20	/*If the acquired photo or spectogram is needed to be send to GS*/
#define SEND_TELEMETRY 			21
#define CHANGE_TIMEOUT			23
#define STOP_TRANSMISIONS		24


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
#define STOP_TRANSMISSION		42	//Send all configuration

#define ACK1					60
#define ERROR					65
#define BEACON					68



#endif /* INC_TELECOMMANDS_H_ */




/*!
 * Radio events function pointer
 */
static RadioEvents_t RadioEvents;

/*!
 * \brief Function to be executed on Radio Tx Done event
 */
void OnTxDone( void );

/*!
 * \brief Function to be executed on Radio Rx Done event
 */
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );

/*!
 * \brief Function executed on Radio Tx Timeout event
 */
void OnTxTimeout( void );

/*!
 * \brief Function executed on Radio Rx Timeout event
 */
void OnRxTimeout( void );

/*!
 * \brief Function executed on Radio Rx Error event
 */
void OnRxError( void );

/*!
 * \brief Function executed on Radio CAD Done event
 */
void OnCadDone( bool channelActivityDetected);

/*!
 * \brief Function configuring CAD parameters
 * \param [in]  cadSymbolNum   The number of symbol to use for CAD operations
 *                             [LORA_CAD_01_SYMBOL, LORA_CAD_02_SYMBOL,
 *                              LORA_CAD_04_SYMBOL, LORA_CAD_08_SYMBOL,
 *                              LORA_CAD_16_SYMBOL]
 * \param [in]  cadDetPeak     Limit for detection of SNR peak used in the CAD
 * \param [in]  cadDetMin      Set the minimum symbol recognition for CAD
 * \param [in]  cadTimeout     Defines the timeout value to abort the CAD activity
 */
void SX126xConfigureCad( RadioLoRaCadSymbols_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin , uint32_t cadTimeout);

/*!
 * \brief CAD timeout timer callback
 */
static void CADTimeoutTimeoutIrq( void );

/*!
 * \brief Sleep timer
 */
void BedIRQ(TimerHandle_t Timer);

/*!
 * \brief Average the collected RSSIs during CAD
 */
int8_t AverageCadRssi( void );

/*!
 * \brief Get the last good RSSI during CAD
 */
int8_t GetLastCadRssi( void );

/*!
 * \brief Display collected RSSIs each ms during CAD
 */




/**
 * @brief Prepares the transmission with the specified operation.
 *
 * @param operation The operation code for transmission preparation.
 */
void TxPrepare(uint8_t operation);

/**
 * @brief Communications state machine.
 */
void COMMS_StateMachine(void);

/**
 * @brief Processes an incoming telecommand.
 *
 * @param Data Array containing telecommand data.
 */
void process_telecommand(uint8_t Data[]);

/**
 * @brief Interleaves the given input array.
 *
 * @param inputarr Pointer to the input array.
 * @param size The size of the input array.
 */
void interleave(uint8_t *inputarr, int size);

/**
 * @brief Deinterleaves the given input array.
 *
 * @param inputarr Pointer to the input array.
 * @param size The size of the input array.
 */
void deinterleave(uint8_t *inputarr, int size);

/**
 * @brief Configures the SX1262 module with specified parameters.
 *
 * @param SF Spreading factor.
 * @param CR Coding rate.
 * @param RF_F RF frequency.
 */
void SX1262Config(uint8_t SF, uint8_t CR, uint32_t RF_F);

/**
 * @brief Configures the SX1262 module using telecommand configuration data.
 *
 * @param config_data Array containing configuration data.
 */
void SX1262TLCConfig(uint8_t config_data[]);

/**
 * @brief Configures the communications settings using telecommand configuration data.
 *
 * @param config_data Array containing configuration data.
 */
void COMMSTLCConfig(uint8_t config_data[]);

/**
 * @brief Beacon callback executed function.
 */
void beacon_time();

/**
 * @brief Stores telemetry data ito flash memory.
 */
void store_telemetry();


#endif /* INC_COMMS_H_ */
