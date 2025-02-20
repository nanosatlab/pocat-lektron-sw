
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 			                    camerav2.c                                   *
 *                                                                           *
 *  Created on: Nov 18, 2022  												 *
 *  Modified on: Jan 09, 2023                                                *
 *      Author: Xavier                                                       *
 *      Email: xavier.morales.rivero@estudiantat.upc.edu                     *
 *                                                                           *
 *  Previous Camera Work:                                                    *
 *                                                                           *
 *  Nov 25, 2021                                                             *
 *      Author: Jaume                                                        *
 *                                                                           *
 *  Mar 4, 2022                                                              *
 *  	Author: Iker              				 NANOSAT-LAB: PoketQube (c)	 *
 *  	                                                                     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 */

//LIBRERIAS
#include <camerav2.h>
//COMANDOS
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 							       COMMANDS                                  *
 *  @TX Commands: These commands are used in order to send the information.
 *  @RX Commands: These buffers are used in order to obtain the information. *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *                                                                          *
 */
//CHECKACK:This command is used to check if the data received from the camera is an ACK.

uint8_t ACK[] = {0x76, 0x00}; //All ACKS have the same structure.

//All these commands are obtained from the datasheet v1.0, which can be easily found on the web.

//DMA Callback Variables: Requiered in order to continuosly transmit data.

int HTC = 0, FTC = 0;
uint32_t indxVGA=0;

int isSizeRxed = 0;
uint32_t longitud;
int doneTransfer=0;
uint8_t auxrxdata[2048];


//RESET: This command starts the communication.
uint8_t txdataReset[4] = {0x56, 0x00, 0x26, 0x00};
uint8_t rxdataReset[60];


//SETRESOLUTION: Sets the necessary resolution. Its default value is 0x11, but it can be modified in its function.

uint8_t txdataSR[9] = {0x56, 0x00, 0x31, 0x05, 0x04, 0x01, 0x00, 0x19, 0x11};
uint8_t rxdataSR[10];

//SETCOMPRESSIBILITY: Sets the necessary compressibility. Its default value its 0x36, but it can be modified on the function.
uint8_t txdataSC[9] = {0x56, 0x00, 0x31, 0x05, 0x01, 0x01, 0x12, 0x04, 0x36};
uint8_t rxdataSC[10];

//CAPTUREIMAGE: It starts the protocol which is necessary to get an image.
uint8_t txdataCI[5] = {0x56, 0x00, 0x36, 0x01, 0x00};
uint8_t rxdataCI[5];

//DATA LENGTH: It obtains the length of the buffer.
uint8_t txdataDL[5] = {0x56, 0x00, 0x34, 0x01, 0x00};
uint8_t rxdataDL[9];

//GET VERSION: Gets the verion. It is not compulsory on any function, but it has been implemented as an extra function.
uint8_t txdataGV[4] = {0x56, 0x00, 0x11, 0x00};
uint8_t rxdataGV[20];

//READDATA: These commands enables to obtain the necessary data. There are 2 notes:
//Note1: The command 0x0A is changed. Even though it appears on the datasheet to be 0x0D, on other sites appear with this number.
//Note2: Personally, I though it is due to a change on the firmware on the newest version, but it does not appear on any web.
																							//12   //13
uint8_t txdata[16] = {0x56, 0x00, 0x32, 0x0C, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A};
uint8_t rxdataImage[4096]={0};


//StopCapture: These commands enables to stop the capture.
uint8_t txdataStop[5] = {0x56, 0x00, 0x36, 0x01, 0x03};
uint8_t rxdataStop[5];



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 	 																		 *
 * 								"STATES"         						     *
 *  @These states are useful in order to know the state of the camera.       *
 *           These are used on the the error protocol function               *
 *                                                                           *                                                                         *                                                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 */

typedef enum{
	CAM_RESET 				          = 0x01, //The reset function is working.
	CAM_SET_RESOLUTION                = 0x02, //The set compressibility function is working.
	CAM_SET_COMPRESSIBILITY 		  = 0x03, //...
	CAM_START_CAPTURE				  = 0x04, //...
	CAM_GET_DATA_LENGTH    			  = 0x05, //...
	CAM_GET_DATA                      = 0x06, //...
	CAM_STOP_CAPTURE                  = 0x07, //...
	CAM_ERROR						  = 0x08, //...
	CAM_VERSION						  = 0x09, //...
	CAM_OK							  = 0x10, //...
	CAM_START_INIT					  = 0x11, //...
	CAM_START_PHOTO					  = 0x12, //...
	CAM_END 						  = 0x13, //...
	CAM_START						  = 0x14  //...
}Camera_State;

Camera_State state = CAM_START_INIT;

//Camera_State state = CAM_CALM;

uint8_t REP_NUM = 3;

uint8_t resolution;

uint8_t compressibility;

//BUFER CON INFORMACION

uint8_t infoBuffer[50];

bool anErrorHappened = false;

int numerrorsVGA = 0;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 								"BASIC" Functions						     *
 *  @These functions are used in order to transmit and obtain the data.      *
 *   These structures, besides it is not perfect, it works and enables       *
 *   Communication. Nevertheless, it is recommended not to touch these       *
 *   functions, as they are extremely sensible to changes.                   *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 */

bool reset(UART_HandleTypeDef huart){
	state = CAM_RESET;
	storeInfo(CAM_RESET);
	memset(rxdataReset, 0, sizeof(rxdataReset));
	HAL_UART_Transmit(&huart, txdataReset, sizeof(txdataReset), 1000);
	HAL_UART_Receive(&huart, rxdataReset, sizeof(rxdataReset), 1000);
	//The c5 is not used in this case. For that reason, a 0x00 is used.
	return checkACK(huart, rxdataReset[0], rxdataReset[1], rxdataReset[2], rxdataReset[3], 0x00);
}

bool getVersion(UART_HandleTypeDef huart){
	state = CAM_VERSION;
	storeInfo(CAM_VERSION);
	memset(rxdataGV, 0, sizeof(rxdataGV));
	HAL_UART_Transmit(&huart, txdataGV, sizeof(txdataGV), 100);
	HAL_UART_Receive(&huart, rxdataGV, sizeof(rxdataGV), 100);
	return checkACK(huart, rxdataGV[0], rxdataGV[1], rxdataGV[2], rxdataGV[3], 0x00);
}

bool setResolution(UART_HandleTypeDef huart){
	state = CAM_SET_RESOLUTION;
	storeInfo(CAM_SET_RESOLUTION);
	txdataSR[8] = resolution;
	memset(rxdataSR, 0, sizeof(rxdataSR));
	HAL_UART_Transmit(&huart, txdataSR, sizeof(txdataSR), 100);
	HAL_UART_Receive(&huart, rxdataSR, sizeof(rxdataSR), 100);
	return checkACK(huart, rxdataSR[0], rxdataSR[1], rxdataSR[2], rxdataSR[3], rxdataSR[4]);
}

bool setCompressibility(UART_HandleTypeDef huart){
	state = CAM_SET_COMPRESSIBILITY;
	storeInfo(CAM_SET_COMPRESSIBILITY);
	txdataSC[8] = compressibility;
	memset(rxdataSC, 0, sizeof(rxdataSC));
	HAL_UART_Transmit(&huart, txdataSC, sizeof(txdataSC), 100);
	HAL_UART_Receive(&huart, rxdataSC, sizeof(rxdataSC) ,100);
	return checkACK(huart, rxdataSC[0], rxdataSC[1], rxdataSC[2], rxdataSC[3], rxdataImage[4]);
}

bool startCapture(UART_HandleTypeDef huart){
	state = CAM_START_CAPTURE;
	storeInfo(CAM_START_CAPTURE);
	memset(rxdataCI, 0, sizeof(rxdataCI));
	HAL_UART_Transmit(&huart, txdataCI, sizeof(txdataCI), 1000);
	HAL_UART_Receive(&huart, rxdataCI, sizeof(rxdataCI), 1000);
	return checkACK(huart, rxdataCI[0], rxdataCI[1], rxdataCI[2], rxdataCI[3], rxdataCI[4]);
}

bool getDataLength(UART_HandleTypeDef huart){
	state = CAM_GET_DATA_LENGTH;
	storeInfo(CAM_GET_DATA_LENGTH);
	memset(rxdataDL, 0, sizeof(rxdataDL));
	HAL_UART_Transmit(&huart, txdataDL, sizeof(txdataDL), 100);
	HAL_UART_Receive(&huart, rxdataDL, sizeof(rxdataDL), 100);
	//Put the necessary data from the DataLength command
	if(checkACK(huart, rxdataDL[0], rxdataDL[1], rxdataDL[2], rxdataDL[3], rxdataDL[4])){
		txdata[12] = rxdataDL[7];
		txdata[13] = rxdataDL[8];
		return true;
	}
}

bool getData(UART_HandleTypeDef huart){
	state = CAM_GET_DATA;
	storeInfo(CAM_GET_DATA);
	longitud=256U*rxdataDL[7]+rxdataDL[8]+8;
	memset(rxdataImage, 0, sizeof(rxdataImage));
	HAL_UART_Transmit(&huart, txdata, sizeof(txdata), 30000);
	HAL_UART_Receive_DMA(&huart, rxdataImage,4096); //1000 bytes is around the smallest photo to be received
	while (!doneTransfer)
	{
	  if (((longitud-indxVGA)>0) && ((longitud-indxVGA)<2048))
	  {
	    if (HTC==1)
	    {
	      memcpy(auxrxdata,rxdataImage+2048,2048);
	      vTaskDelay(200);
	      store_flash_memory(PHOTO_ADDR+indxVGA, &auxrxdata, (longitud-indxVGA));
	      //Send_to_WFQueue(&auxrxdata, (longitud-indxVGA), PHOTO_ADDR+indxVGA, PAYLOADsender); //memcpy (FinalBuf+indxVGA, rxdataImage+500, (longitud-indxVGA));
	      indxVGA = longitud;
	      memset(auxrxdata, '\0', 2048);
	      isSizeRxed = 0;
	      HTC = 0;
	      HAL_UART_DMAStop(&huart);
	      doneTransfer=1;
	      //HAL_UART_Receive_DMA(&huart, rxdataImage, 1000);
	    }

	  else if (FTC==1)
	  {
		  vTaskDelay(50);
	     //Write_Flash(PHOTO_ADDR+indxVGA, &rxdataImage,(longitud-indxVGA));
	     store_flash_memory(PHOTO_ADDR+indxVGA, &rxdataImage, (longitud-indxVGA));
		 //Send_to_WFQueue(&rxdataImage, 500, PHOTO_ADDR+indxVGA, PAYLOADsender);//memcpy (FinalBuf+indxVGA, rxdataImage, (longitud-indxVGA));
	     indxVGA = longitud;
	     isSizeRxed = 0;
	     FTC = 0;
	     HAL_UART_DMAStop(&huart);
	     doneTransfer=1;
	     //HAL_UART_Receive_DMA(&huart, rxdataImage, 1000);
	  }
	  }
	  else if ((indxVGA == longitud) && ((HTC==1)||(FTC==1)))
	  {
		  isSizeRxed = 0;
		  HTC = 0;
		  FTC = 0;
		  HAL_UART_DMAStop(&huart);
	      doneTransfer=1;
		  //HAL_UART_Receive_DMA(&huart, rxdataImage, 1000);
	  }
	}
	return 1;//checkACK(huart, rxdataImage[0], rxdataImage[1], rxdataImage[2], rxdataImage[3], 0x00); //return 0s with the checkACK() //cant check due to dma
}


bool stopCapture(UART_HandleTypeDef huart){
	state = CAM_STOP_CAPTURE;
	storeInfo(CAM_STOP_CAPTURE);
	memset(rxdataStop, 0, sizeof(rxdataStop));
	HAL_UART_Transmit(&huart, txdataStop, sizeof(txdataStop), 100);
	HAL_UART_Receive(&huart, rxdataStop, sizeof(rxdataStop), 100);
	return checkACK(huart, rxdataStop[0], rxdataStop[1], rxdataStop[2], rxdataStop[3], rxdataStop[4]);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 							"Composed" Functions						     *
 *  @These functions are used in order to obtain the data from the sensor.   *
 *   These functions are called compose, as they are done with the basic f   *
 *   Note that initCam does not have reset. It is because it is not n        *
 *   necessary and it interrupts the com with the sensor.                    *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 */
//FINALIZADA, FUNCIONA OK.
bool initCam(UART_HandleTypeDef huart, uint8_t res, uint8_t comp, uint8_t *array){

	storeInfo(CAM_START);

	HAL_Delay(3000);

	storeInfo(res);
	storeInfo(comp);

	resolution = res;
	compressibility = comp;

	if(!reset(huart)){
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return false;
	}

	if(!getVersion(huart)){
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return false;
	}

	if(!setResolution(huart)){		//res is the Resolution used.
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return false;
	}

	if(!setCompressibility(huart)){	//comp is the compression used.
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return false;
	}

	storeInfo(CAM_END);
	//This part returns the array to the Buffer
	for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
}
//FINALIZADA, FUNCIONA OK.
uint16_t getPhoto(UART_HandleTypeDef huart, uint8_t *array){
	bool ok = true;

	storeInfo(CAM_START);

	if(!startCapture(huart)){
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return 0x0000;
	}

	if(!getDataLength(huart)){
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return 0x0000;
	}

	if(!getData(huart)){
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return 0x0000;
	}

	if(!stopCapture(huart)){
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return 0x0000;
	}
	storeInfo(CAM_END);
	for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
	//return storeDataFlash();
}

//Comprobado

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 																			 *
 * 							"Checking"&"Extra" Functions				     *
 *  @These functions are used in order to check the data from the sensor.    *
 *   Check ack analyses if the ack is not the correct one. If it does not    *
 *   work (return false)m it applied the error protocol. If it does not work *
 *   it ends returning false.                                                *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 */

uint16_t storeDataFlash(){
	memmove(rxdataImage, rxdataImage + 5, sizeof(rxdataImage));
	uint32_t photosize=sizeof(rxdataImage);
	// Flash_Write_Data(PHOTO_ADDR, rxdataImage, (uint16_t) sizeof(rxdataImage)/8 + 1);
	//Write_Flash(PHOTO_ADDR, &rxdataImage, (uint16_t) sizeof(rxdataImage)/8 + 1);
	//Send_to_WFQueue(&rxdataImage, (uint16_t) sizeof(rxdataImage) + 1, PHOTO_ADDR, PAYLOADsender);
	return (256U*txdata[12]+txdata[13]); 	//longitud
}

void storeInfo(uint8_t info){
	infoBuffer[numerrorsVGA] = info;
	if(info == CAM_START){
		memset(infoBuffer, 0, sizeof(infoBuffer));
		infoBuffer[numerrorsVGA] = info;
		numerrorsVGA = 0;
	}
	numerrorsVGA++;
}


bool checkACK(UART_HandleTypeDef huart, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5){
	//All the ACKs are formed by these structure. If this does not work, it interrupts
	if(c1 != 0x76 || c2 != 0x00){ if(anErrorHappened == true){ return false; } return errP(huart);}
	//Now, it is determined below in each case:

	//RESET: 0x76 0x00 0x26 0x00
	if(state == CAM_RESET && (c3 != 0x26 || c4 != 0x00)){ if(anErrorHappened == true){ return false; } return errP(huart); }

	//GET VERSION: 0x76 0x00 0x11 0x00
	if(state == CAM_VERSION && (c3 != 0x11 || c4 != 0x00)){ if(anErrorHappened == true){ return false; } return errP(huart); }

	//SET RESOLUTION: 0x76 0x00 0x31 0x00 0x00
	if(state == CAM_SET_RESOLUTION && (c3 != 0x31 || c4 != 0x00 || c5 != 0x00)){ if(anErrorHappened == true){ return false; } return errP(huart); }

	//SET COMPRESSIBILITY: 0x76 0x00 0x31 0x00 0x00
	if(state == CAM_SET_COMPRESSIBILITY && (c3 != 0x31 || c4 != 0x00 || c5 != 0x00)){ if(anErrorHappened == true){ return false; } return errP(huart); }

	//SET START CAPTURE: 0x76 0x00 0x36 0x00 0x00
	if(state == CAM_START_CAPTURE && ( c3 != 0x36 || c4 != 0x00 || c5 != 0x00) ){ if(anErrorHappened == true){ return false; } return errP(huart); }

	//GET DATA LENGTH: 0x76 0x00 0x34 0x00 0x00
	if(state == CAM_GET_DATA_LENGTH && (c3 != 0x34 || c4 != 0x00 || c5 != 0x04)){ if(anErrorHappened == true){ return false; } return errP(huart); }

	//GET DATA: 0x76 0x00 0x32 0x00
	if(state == CAM_GET_DATA && (c3 != 0x32 || c4 != 0x00)){ if(anErrorHappened == true){ return false; } return errP(huart); }

	//STOP CAPTURE 0x76 0x00 0x36 0x00 0x00
	if(state == CAM_STOP_CAPTURE && (c3 != 0x36 || c4 != 0x00 || c5 != 0x00)){ if(anErrorHappened == true){ return false; } return errP(huart); }

	return true;

}

bool errP(UART_HandleTypeDef huart){

	uint8_t attempts = 0;
	bool err;
	vTaskDelay(2500);
	anErrorHappened = true;

	while(attempts <= REP_NUM){
			//RESET ERROR.
			if(state == CAM_RESET){ err = reset(huart);}

			//SET RESOLUTION ERROR.
			if(state == CAM_SET_RESOLUTION){ err = setResolution(huart);}

			if(state == CAM_VERSION){err = getVersion(huart);}
			//SET COMPRESSIBILITY ERROR.
			if(state == CAM_SET_COMPRESSIBILITY ){ err = setCompressibility(huart);}

			//SET START CAPTURE ERROR.
			if(state == CAM_START_CAPTURE){ err = startCapture(huart); }

			//GET DATA LENGTH ERROR.
			if(state == CAM_GET_DATA_LENGTH){ err = getDataLength(huart); }

			//GET DATA ERROR.
			if(state == CAM_GET_DATA){ err = getData(huart); }

			//STOP CAPTURE ERROR.
			if(state == CAM_STOP_CAPTURE){ err = stopCapture(huart);}

			if(err == true){
				anErrorHappened = false;
				return true;
			}
			attempts++;
	}
	//It is restored, besides the error persist.
	anErrorHappened = false;
	return false;
}



void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
	if (isSizeRxed == 0)
	{
		indxVGA = 0;
		memcpy(auxrxdata,rxdataImage+5,2048);
		//HAL_Delay(50);
		store_flash_memory(PHOTO_ADDR+indxVGA,  &auxrxdata, 2048);
		//Write_Flash(PHOTO_ADDR+indxVGA, &auxrxdata,500);
		//Send_to_WFQueue(&auxrxdata, 500, PHOTO_ADDR+indxVGA, PAYLOADsender);//memcpy(FinalBuf+indxVGA, rxdataImage+5, 500);  // copy the data into the main buffer/file
		memset(rxdataImage, '\0', 2048);  // clear the RxData buffer
		memset(auxrxdata, '\0', 2048);  // clear the auxrxdata buffer
		indxVGA += 2048;  // update the indxVGA variable
		isSizeRxed = 1;  // set the variable to 1 so that this loop does not enter again
	}
	else
	{
		store_flash_memory(PHOTO_ADDR+indxVGA, &rxdataImage, 2048);
		//Send_to_WFQueue(&rxdataImage, 500, PHOTO_ADDR+indxVGA, PAYLOADsender);//memcpy(FinalBuf+indxVGA, rxdataImage, 500);
		memset(rxdataImage, '\0', 2048);
		indxVGA += 2048;
	}
	HTC=1;  // half transfer complete callback was called
	FTC=0;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	  memcpy(auxrxdata,rxdataImage+2048,2048);
	  store_flash_memory(PHOTO_ADDR+indxVGA, &auxrxdata, 2048);
	  //Send_to_WFQueue(&auxrxdata, 500, PHOTO_ADDR+indxVGA, PAYLOADsender);//memcpy(FinalBuf+indxVGA, rxdataImage+500, 500);
	  memset(rxdataImage+2048, '\0', 2048);
	  memset(auxrxdata, '\0', 2048);
	  indxVGA+=2048;
	  HTC=0;
	  FTC=1;
}

void WFSkip(uint8_t* pointer,uint32_t arrayLength,uint32_t addr)
{
	QueueData_t RxQueueSkip = {pointer,arrayLength,addr};
	Write_Flash(RxQueueSkip.addr,RxQueueSkip.pointer,RxQueueSkip.arrayLength);
}
