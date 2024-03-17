/*
 * definitions.h
 *
 *  Created on: 12 may. 2022
 *      Author: Daniel Herencia Ruiz
 */

#ifndef INC_DEFINITIONS_H_
#define INC_DEFINITIONS_H_


#include <stdbool.h>
#include <stdint.h>
#include <stm32l4xx_hal.h>

/*Temperature range operation of STM32L162VE*/
#define TEMP_MIN -40
#define TEMP_MAX 105
#define TEMP_MIN_BATT 2
#define TRUE 	0x01
#define FALSE 	0x00

#define DELAY_CAMERA 2500 /*Initial operation process*/

#define THRESHOLD 3


uint8_t currentState;
uint8_t previousState;

/*Total of 8bytes -> 8bytes·1uit64_t/8bytes = 1 uit64_t*/
typedef union __attribute__ ((__packed__)) Temperatures {
    uint64_t raw[1];
    struct __attribute__((__packed__)) {
    	 int8_t tempbatt;
         int8_t temp1;
         int8_t temp2;
         int8_t temp3;
         int8_t temp4;
         int8_t temp5;
         int8_t temp6;
    }fields;
} Temperatures;

/*Total of 12bytes -> rounded to 16bytes -> 2 uit64_t*/
typedef union __attribute__ ((__packed__)) Voltages {
    uint64_t raw[1];
    struct __attribute__((__packed__)) {
    	uint8_t voltage1;
//    	uint8_t voltage2;
//    	uint8_t voltage3;
//    	uint8_t voltage4;
//    	uint8_t voltage5;
//    	uint8_t voltage6;
//    	uint8_t voltage7;
//    	uint8_t voltage8;
//    	uint8_t voltage9;
//    	uint8_t voltage10;
//    	uint8_t voltage11;
//    	uint8_t voltage12;
    }fields;
} Voltages;


/*Total of 5bytes -> 8 bytes -> 1 uint64_t*/
typedef union __attribute__ ((__packed__)) BatteryLevels {
    uint64_t raw[1];
    struct __attribute__((__packed__)) {
    	uint8_t totalbattery; /*Stores the total percentage of battery*/
    }fields;
} BatteryLevels;

/*Total of 7bytes -> 8bytes -> 1 uint64_t*/
typedef union __attribute__ ((__packed__)) Currents {
    uint64_t raw[1];
    struct __attribute__((__packed__)) {
    	uint8_t current1;
    	uint8_t current2;
    	uint8_t current3;
    	uint8_t current4;
    	uint8_t current5;
    	uint8_t current6;
    	uint8_t current7;
    }fields;
} Currents;

/*Total of 69bytes·2lines = 138bytes -> 144bytes -> 18 uit64_t*/
typedef union __attribute__ ((__packed__)) TLEUpdate {
    uint64_t raw[18];
    struct __attribute__((__packed__)) {
    	char tle1[69]; 						/*First line of TLE, 69 chars, 1byte each char*/
    	char tle2[69]; 						/*Second line of TLE, 69 chars, 1byte each char*/
    }fields;
} TLEUpdate;

/*Total of 20002bytes (20000+1+1) -> 20002/8 = 2500.25 rounded to 2501 uint64_t*/
typedef union __attribute__ ((__packed__)) Image {	/*const variable is stored in FLASH memory*/
    uint64_t raw[2501];
    struct __attribute__ ((__packed__)) {
    	uint8_t date;						/*When the image was acquired*/
    	uint8_t coordinates;				/*Where the image was acquired*/
    	uint8_t bufferImage[20000];			/*20000bytes worst case*/
    	uint8_t size;
    }fields;
} Image;

/*Total of 55002bytes -> 55002/8 = 6875.25 rounded to 6876 uint64_t*/
typedef union __attribute__ ((__packed__)) RadioFrequency {
    uint64_t raw[6876];
    struct __attribute__((__packed__)) {
    	uint8_t date;						/*When the image was acquired*/
    	uint8_t coordinates;				/*Where the image was acquired*/
    	uint8_t bufferRF[55000];				/*The size depends on the time acquiring, at the most about 55kB (whole orbit)
    	 	 	 	 	 	 	 	 	 	  size(bytes) = 73bits/s·(time acquiring)·1byte/8bits */
    }fields;
} RadioFrequency;

/****STRUCTS****/
enum Subsystem {OBC, COMMS, ADCS, PAYLOAD, EPS};

enum MachineState {_INIT, _NOMINAL, _CONTINGENCY, _SUNSAFE, _SURVIVAL};

typedef enum
{
	OBCsender,
	COMMSsender,
	ADCSsender,
	PAYLOADsender,
	EPSsender
} DataSource_t;

typedef struct
{
	uint8_t* pointer;
	uint32_t arrayLength;
	uint32_t addr;
	DataSource_t DataSource;
} QueueData_t;

#endif /* INC_DEFINITIONS_H_ */
