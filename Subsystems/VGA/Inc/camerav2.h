/*
 * camerav2.h
 *
 *  Created on: Nov 18, 2022
 *      Author: Xavier
 */

#ifndef INC_CAMERAV2_H_
#define INC_CAMERAV2_H_

#include "stm32l4xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

void storeInfo(uint8_t info);

int runCommand(UART_HandleTypeDef huart, uint8_t commandTX, uint8_t commandRX);

int reset(UART_HandleTypeDef huart);

int setResolution(UART_HandleTypeDef huart);

int setCompressibility(UART_HandleTypeDef huart);

int startCapture(UART_HandleTypeDef huart);

int getDataLength(UART_HandleTypeDef huart);

int getData(UART_HandleTypeDef huart);

int stopCapture(UART_HandleTypeDef huart);

int checkACK(UART_HandleTypeDef huart, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5);

int errP(UART_HandleTypeDef huart);

int initCam(UART_HandleTypeDef huart, uint8_t res, uint8_t comp, uint8_t *infoarray);

uint16_t getPhoto(UART_HandleTypeDef huart, uint8_t *infoarray);

int getVersion(UART_HandleTypeDef huart);

void WFSkip(uint8_t* pointer,uint32_t arrayLength,uint32_t addr);

#endif /* INC_CAMERAV2_H_ */
