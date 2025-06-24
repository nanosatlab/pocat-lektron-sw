/*
 * flash2.h
 *
 *  Created on: 17 ene. 2023
 *      Author: NilRi
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

#include <stdint.h>
#include <stdbool.h>

#include "obc.h"
#include "definitions.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"

/**********Flash characteristics*********************/
#define FLASH_BASE         			0x08000000
#define FLASH_END_ADDR        		0x080FFFFF
#define FLASH_BANK_SIZE   			0x80000   // 512 KB
#define FLASH_PAGE_SIZE    			0x800     // 2 KB
/****************************************************/

#define PHOTO_ADDR 					0x08040000

//#define PAYLOAD_STATE_ADDR 		0x08008000
//#define COMMS_STATE_ADDR 			0x08008001
//#define DETUMBLE_STATE_ADDR 		0x08008004

/****************ADCS CALIBRATION****************/
//#define CALIBRATION_ADDR			0x080300AB
#define MAGNETO_MATRIX_ADDR			0x080300AB // 36 bytes
#define MAGNETO_OFFSET_ADDR			0x080300CF // 12 bytes
#define GYRO_POLYN_ADDR 			0x080300DB // 24 bytes
#define PHOTODIODES_OFFSET_ADDR 	0x080300F3 // 24 bytes
/************************************************/

/********************ADCS TLE********************/
#define TLE_ADDR1 					0x08038020 // 138 bytes
#define TLE_ADDR2 					0x08038065 // 138 bytes
/************************************************/

/******************COMMS CONF********************/
#define OUTPUT_POWER_ADDR			0x08030209
#define FRF_ADDRR					0x08030208 	//frequency
#define SF_ADDR 					0x08030012
#define CRC_ADDR 					0x08030013
/************************************************/

/******************COMMS PARAM*******************/
#define TIMEOUT_ADDR 				0x08030204 	// 2 bytes reception timeout and sleep time
#define CADMODE_ADDR				0x08030206
/************************************************/

/*******************UNIX TIME********************/
#define SET_TIME_ADDR				0x08030008 	//4 bytes (GS -> RTC)
/************************************************/

/*******************EPS THR********************/
#define EPS_TH_ADDR             	0x08030800  // 3 bytes: Nominal, low & critical
/************************************************/

/*******************PL CONFIG********************/
#define RFI_CONFIG_ADDR				0x08030210	//12 bytes
/************************************************/

/*******************UPLINK CONFIG****************/
#define UPLINK_ADDR 				0x08037000 	// 18 Bytes
#define KP_ADDR 					0x08030010 	//9º
#define GYRO_RES_ADDR 				0x08030011 	//10º
/************************************************/

/*************PAYLOAD DATA ADDR******************/
#define PL_TIME_ADDR				0x0802F800	//TIME ADDR 4 bytes (GS -> PAYLOAD CAMERA)
#define PHOTO_COMPRESSION_ADDR 		0x0802F804
#define PHOTO_RESOL_ADDR 			0x0802F805
#define PL_RF_TIME_ADDR				0x0802F806	//TIME ADDR 8 bytes (GS -> PAYLOAD RF)
#define F_MIN_ADDR 					0x0802F80D
#define F_MAX_ADDR 					0x0802F80E
#define	RF_RESOL_ADDR				0x0802F80F
#define INTEGRATION_TIME_ADDR 		0x0802F810
/************************************************/

//COMMS CONFIG PARAM ADDR
#define PREVIOUS_STATE_ADDR			0x08030001 //OBC STATE MACHINE PREVIOUS STATE
#define DEPLOYMENT_STATE_ADDR 		0x08030002 //DEPLOYMENT STATE
#define DEPLOYMENTRF_STATE_ADDR 	0x08030003 //ANTENNA DEPLYMENT STATE

#define RTC_TIME_ADDR				0x0803000C	//4 bytes (RTC -> Unix) Get from the RTC
#define EXIT_LOW_ADDR 				0x08030007

//CONFIGURATION ADDRESSES
#define DELTA_F_ADDR 				0x0803001A
//#define EXIT_LOW_POWER_FLAG_ADDR 	0x080080AA

//COMMS CONFIGURATION ADDRESSES
#define COUNT_PACKET_ADDR 			0x08030200
#define COUNT_WINDOW_ADDR 			0x08030201
#define COUNT_RTX_ADDR 				0x08030202
#define ANTENNA_DEPLOYED_ADDR       0x08030207

/**********OTHER ADDR****************************/
#define DATA_ADDR					0x08030000
#define COMMS_TIME_ADDR				0x0803E860 // Time between packets
#define PHOTOTIME_ADDR				0x08038008 	//4 bytes (GS -> RTC)
#define COMMS_STATE_ADDR			0x08038012
#define COMMS_BOOL_ADDR				0x08038013
/************************************************/

/*****************TELEMETRY INFO ADDR*******************/ //0x080D3800 - 0x080FFFFF: 4096kb (512 kbytes) dedicated enabling stores of 89 old data.
#define TELEMETRY_LEGACY_ADDR		0x08080800 //Page 257

/*#define PQ_TEMP_ADDR*/						// 7 bytes
#define TEMPLAT_ADDR 				0x08080801	// 6
#define BATT_TEMP_ADDR 			    0x08080807	// 1

/*#define EPS_ADDR*/ //4 bytes
#define VOLTAGE_ADDR				0x08080808	// 1
#define CURRENT_STATE_ADDR			0x08080809  // 1 EPS STATE MACHINE CURRENT STATE
#define BATT_CAP_ADDR 			    0x0808080A	// 1
#define EPS_TEMP_ADDR				0x0808080B	// 1

/*#define OBC_ADDR*/ //3 bytes
#define OBC_PQ_ADDR					0x0808080C	// 1
#define OBC_ERRORS_ADDR				0x0808080D	// 1
#define MCU_TEMP_ADDR 			    0x0808080E	// 1

/*#define ADCS_ADDR*/ //14 bytes
#define GYRO_ADDR 			        0x08030115	// 6
#define MAGNETOMETER_ADDR 			0x08030116  // 8
#define PHOTODIODES_ADDR 			0x0803011D  // 8 (?)
/*******************************************************/








extern EventGroupHandle_t xEventGroup;
extern QueueHandle_t FLASH_Queue;
extern SemaphoreHandle_t xMutex;

void Write_Flash(uint32_t data_addr, uint8_t *data,uint16_t n_bytes);
void Read_Flash(uint32_t data_addr, uint8_t *RxBuf, uint16_t n_bytes);
void Send_to_WFQueue(uint8_t* pointer, uint32_t arrayLength, uint32_t addr, DataSource_t DataSource);

void erase_page(uint32_t data_addr);
void store_flash_memory(uint32_t memory_address, uint8_t *data, uint16_t data_length);


#endif /* INC_FLASH_H_ */
