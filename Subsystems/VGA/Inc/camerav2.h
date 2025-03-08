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

/*!
 * \brief Function storing the subsequential states the camera goes through
 */
void storeInfo(uint8_t info);

/*!
 * \brief Camera reset function
 * \param [in]  huart     UART Port
 */
int reset(UART_HandleTypeDef huart);

/*!
 * \brief Camera resolution setting function
 * \param [in]  huart     UART Port
 */
int setResolution(UART_HandleTypeDef huart);

/*!
 * \brief Camera compression setting function
 * \param [in]  huart     UART Port
 */
int setCompressibility(UART_HandleTypeDef huart);

/*!
 * \brief Camera image taking setting function
 * \param [in]  huart     UART Port
 */
int startCapture(UART_HandleTypeDef huart);

/*!
 * \brief Camera image lenght retrieval function
 * \param [in]  huart     UART Port
 */
int getDataLength(UART_HandleTypeDef huart);

/*!
 * \brief Camera data extraction function
 * \param [in]  huart     UART Port
 */
int getData(UART_HandleTypeDef huart);

/*!
 * \brief Camera stop function
 * \param [in]  huart     UART Port
 */
int stopCapture(UART_HandleTypeDef huart);

/*!
 * \brief ACK of camera operations
 * \param [in]  huart     UART Port
 * \param [in]  c1     Byte 1
 * \param [in]  c2     Byte 2
 * \param [in]  c3     Byte 3
 * \param [in]  c4     Byte 4
 * \param [in]  c5     Byte 5
 */
int checkACK(UART_HandleTypeDef huart, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5);

/*!
 * \brief Error protocol, executes a number of tries.
 * \param [in]  huart     UART Port
 */
int errP(UART_HandleTypeDef huart);

/*!
 * \brief Camera composite function initializing and setting resolution and compression
 * \param [in]  huart     		UART Port
 * \param [in]  res     		Resolution (0x00/0x11/0x22)
 * \param [in]  comp     		Compression (0x00-0xFF)
 * \param [in]  *infoarray      Camera Prev. States
 *
 */
int initCam(UART_HandleTypeDef huart, uint8_t res, uint8_t comp, uint8_t *infoarray);

/*!
 * \brief Camera composite function taking and storing an image
 * \param [in]  huart     UART Port
 * \param [in]  *infoarray      Camera Prev. States
 */
uint16_t getPhoto(UART_HandleTypeDef huart, uint8_t *infoarray);

/*!
 * \brief Camera get version function
 * \param [in]  huart     UART Port
 */
int getVersion(UART_HandleTypeDef huart);


#endif /* INC_CAMERAV2_H_ */
