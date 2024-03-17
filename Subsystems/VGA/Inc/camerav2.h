/*
 * camerav2.h
 *
 *  Created on: Nov 18, 2022
 *      Author: Xavie
 */

#ifndef INC_CAMERAV2_H_
#define INC_CAMERAV2_H_

#include "stm32l4xx_hal.h"
#include <string.h> // Usado para la funcion memcmp
#include <stdio.h>
#include <stdbool.h> //PARA EL BOOL
#include <stdlib.h> //PARA GUARDAR DATOS

bool runCommand(UART_HandleTypeDef huart, uint8_t commandTX, uint8_t commandRX);

bool reset(UART_HandleTypeDef huart);

bool setResolution(UART_HandleTypeDef huart);

bool setCompressibility(UART_HandleTypeDef huart);

bool startCapture(UART_HandleTypeDef huart);

bool getDataLength(UART_HandleTypeDef huart);

bool getData(UART_HandleTypeDef huart);

bool stopCapture(UART_HandleTypeDef huart);

bool checkACK(UART_HandleTypeDef huart, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5);

bool errP(UART_HandleTypeDef huart);

bool initCam(UART_HandleTypeDef huart, uint8_t res, uint8_t comp, uint8_t *infoarray);

uint16_t getPhoto(UART_HandleTypeDef huart, uint8_t *infoarray);

uint16_t storeDataFlash();

bool getVersion(UART_HandleTypeDef huart);

#endif /* INC_CAMERAV2_H_ */
