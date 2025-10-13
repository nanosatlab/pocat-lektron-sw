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
#define OBC_PHOTO_CAPTURE (1 << 0) // bit 0

// PAYLOAD event group bits
#define PAYLOAD_PHOTO_CAPTURE (1 << 0) // bit 0


// Peripheral handles
extern SPI_HandleTypeDef hspi2;


void Error_Handler(void); // s'ha d'implementar

#endif /* __MAIN_H */
