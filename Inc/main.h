/**
 * *****************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file.
 *                   This file contains the common defines of the application.
 ******************************************************************************
 */

#ifndef __MAIN_H
#define __MAIN_H

#include "stm32l4xx_hal.h"
#include "stm32l476xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
   

// OBC event group bits

#define OBC_EVENT_OBDH_DONE (1UL << 0) //Bit 0, obdh ha acabat
#define OBC_EVENT_PAYLOAD_Experiments (1UL<<1)//Bit 1, enviem dades experiment
#define OBC_EVENT_EPS_Measurements (1UL<<2) //Bit 2, enviem mesures EPS
#define OBC_PHOTO_CAPTURE (1UL << 3) // bit 4
// PAYLOAD event group bits
#define PAYLOAD_PHOTO_CAPTURE (1<<0) 
// Event bits definitions, max 32 bits






// Peripheral handles
extern SPI_HandleTypeDef hspi2;


void Error_Handler(void); // s'ha d'implementar

#endif /* __MAIN_H */
