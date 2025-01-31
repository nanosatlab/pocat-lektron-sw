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

#define PHOTO_ADDR 					0x08040000
#define COMMS_CONFIG_ADDR
#define COMMS_CONFIG_ADDR
#define COMMS_CONFIG_ADDR

//#define PAYLOAD_STATE_ADDR 		0x08008000
//#define COMMS_STATE_ADDR 			0x08008001
//#define DETUMBLE_STATE_ADDR 		0x08008004
#define CURRENT_STATE_ADDR			0x08030000 //OBC STATE MACHINE CURRENT STATE
#define PREVIOUS_STATE_ADDR			0x08030001 //OBC STATE MACHINE PREVIOUS STATE
#define DEPLOYMENT_STATE_ADDR 		0x08030002 //DEPLOYMENT STATE
#define DEPLOYMENTRF_STATE_ADDR 	0x08030003 //ANTENNA DEPLYMENT STATE

#define EXIT_LOW_ADDR 				0x08030007
#define SET_TIME_ADDR				0x08030008 	//4 bytes (GS -> RTC)
#define RTC_TIME_ADDR				0x0803000C	//4 bytes (RTC -> Unix) Get from the RTC

//CONFIGURATION ADDRESSES
#define CONFIG_ADDR 				0x08030010
#define KP_ADDR 					0x08030010
#define GYRO_RES_ADDR 				0x08030011
#define SF_ADDR 					0x08030012
#define CRC_ADDR 					0x08030013
#define PHOTO_RESOL_ADDR 			0x08030014
#define PHOTO_COMPRESSION_ADDR 		0x08030015
#define F_MIN_ADDR 					0x08030016
#define F_MAX_ADDR 					0x08030018
#define DELTA_F_ADDR 				0x0803001A
#define INTEGRATION_TIME_ADDR 		0x0803001C

#define TLE_ADDR 					0x08030020 // 138 bytes
//#define EXIT_LOW_POWER_FLAG_ADDR 	0x080080AA

//CALIBRATION ADDRESSES
#define CALIBRATION_ADDR			0x080300AB
#define MAGNETO_MATRIX_ADDR			0x080300AB // 35 bytes
#define MAGNETO_OFFSET_ADDR			0x080300CF // 11 bytes
#define GYRO_POLYN_ADDR 			0x080300DB // 23 bytes
#define PHOTODIODES_OFFSET_ADDR 	0x080300F3 // 12 bytes

//TELEMETRY ADDRESSES
#define TELEMETRY_ADDR				0x08030100
#define TEMPLAT_ADDR 				0x08030100		// 6
#define BATT_TEMP_ADDR 			    0x08030106		// 1
#define MCU_TEMP_ADDR 			    0x08030107		// 1
#define BATT_CAP_ADDR 			    0x08030108		// 3
//      CURRENT_STATE_ADDR                             1
#define GYRO_ADDR 			        0x08030109		// 6
#define MAGNETOMETER_ADDR 			0x0803010F      // 8
#define PHOTODIODES_ADDR 			0x08030117      // 8

#define TELEMETRY_LEGACY_ADDR		0x080FEFFF // 4096kb dedicated enabling stores of 89 old data.


//TIME ADDR
#define PL_TIME_ADDR 				0x08030111 	//4 bytes (GS -> PAYLOAD CAMERA)
#define PL_RF_TIME_ADDR				0x0803011C	//8 bytes (GS -> PAYLOAD RF)

//COMMS CONFIGURATION ADDRESSES
#define COUNT_PACKET_ADDR 			0x08030200
#define COUNT_WINDOW_ADDR 			0x08030201
#define COUNT_RTX_ADDR 				0x08030202

#define TIMEOUT_ADDR 				0x08030204 // 2 bytes

#define ANTENNA_DEPLOYED_ADDR       0x08030205


/**********OTHER ADDR****************************/
#define DATA_ADDR					0x08030000
#define TLE_ADDR1 					0x08038020 // 138 bytes
#define TLE_ADDR2 					0x08038065 // 138 bytes
#define COMMS_TIME_ADDR				0x0803E860 // Time between packets
#define PHOTOTIME_ADDR				0x08038008 	//4 bytes (GS -> RTC)
#define COMMS_STATE_ADDR			0x08038012
#define COMMS_BOOL_ADDR				0x08038013
/************************************************/



//EPS
#define NOMINAL_TH_ADDR             0x08030800  // 1
#define CONTINGENCY_TH_ADDR         0x08030801  // 1
#define SUNSAFE_TH_ADDR             0x08030802  // 1
#define SURVIVAL_TH_ADDR            0x08030803  // 1

#define RFI_CONFIG_ADDR             0x08031000  // 8


//COMMS
#define COMMS_CONFIG_ADDR           0x08031800  // 7







extern EventGroupHandle_t xEventGroup;
extern QueueHandle_t FLASH_Queue;
extern SemaphoreHandle_t xMutex;

void Write_Flash(uint32_t data_addr, uint8_t *data,uint16_t n_bytes);
void Read_Flash(uint32_t data_addr, uint8_t *RxBuf, uint16_t n_bytes);
void Send_to_WFQueue(uint8_t* pointer, uint32_t arrayLength, uint32_t addr, DataSource_t DataSource);

void erase_page(uint32_t data_addr);


#endif /* INC_FLASH_H_ */
