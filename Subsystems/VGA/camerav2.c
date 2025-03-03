


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 			                    camerav2.c                                   *
 *      Current responsible: Óscar P.                                        *
 *                          												 *
 *  Previous Camera Work:                                                    *
 *                                                                           *
 *  Jan 09, 2023:															 *
 *  	Author: Xavier Morales												 *
 *  Nov 25, 2021                                                             *
 *      Author: Jaume                                                        *
 *                                                                           *
 *  Mar 4, 2022                                                              *
 *  	Author: Iker              				 NANOSAT-LAB: PoketQube (c)	 *
 *  	                                                                     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "main.h"
#include <camerav2.h>


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
	CAM_SET_RESOLUTION                = 0x02, //The set compression function is working.
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


//DMA Variables

int HTC = 0, FTC = 0;
int isSizeRxed = 0;
int doneTransfer=0;

uint32_t indxVGA=0;
uint32_t imagelength;
uint8_t auxrxdata[2048];

/* Tx and Rx Buffers */

//RESET: This command starts the communication.
uint8_t txdataReset[4] = {0x56, 0x00, 0x26, 0x00};
uint8_t rxdataReset[60];

//SETRESOLUTION: Sets the necessary resolution. Its default value is 0x11, but it can be modified in its function.

uint8_t txdataSR[9] = {0x56, 0x00, 0x31, 0x05, 0x04, 0x01, 0x00, 0x19, 0x11};
uint8_t rxdataSR[10];

//SETCOMPRESSIBILITY: Sets the necessary compression. Its default value its 0x36, but it can be modified on the function.
uint8_t txdataSC[9] = {0x56, 0x00, 0x31, 0x05, 0x01, 0x01, 0x12, 0x04, 0x36};
uint8_t rxdataSC[10];

//CAPTUREIMAGE: It starts the protocol which is necessary to get an image.
uint8_t txdataCI[5] = {0x56, 0x00, 0x36, 0x01, 0x00};
uint8_t rxdataCI[5];

//DATA LENGTH: It obtains the length of the buffer.
uint8_t txdataDL[5] = {0x56, 0x00, 0x34, 0x01, 0x00};
uint8_t rxdataDL[9];

//GET VERSION: Gets the camera version.
uint8_t txdataGV[4] = {0x56, 0x00, 0x11, 0x00};
uint8_t rxdataGV[20];

//READDATA: These commands enables to obtain the necessary data. There are 2 notes:
																							//12   //13
uint8_t txdataIM[16] = {0x56, 0x00, 0x32, 0x0C, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A};
uint8_t rxdataIM[4096]={0};

//StopCapture: These commands enables to stop the capture.
uint8_t txdataStop[5] = {0x56, 0x00, 0x36, 0x01, 0x03};
uint8_t rxdataStop[5];

uint8_t ACK[] = {0x76, 0x00}; //All ACKS have the same structure.
uint8_t REP_NUM = 3;

uint8_t resolution;
uint8_t compressibility;

uint8_t infoBuffer[50];

int anErrorHappened = 0;

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

int reset(UART_HandleTypeDef huart){
	state = CAM_RESET;
	storeInfo(CAM_RESET);
	memset(rxdataReset, 0, sizeof(rxdataReset));
	HAL_UART_Transmit(&huart, txdataReset, sizeof(txdataReset), 1000);
	HAL_UART_Receive(&huart, rxdataReset, sizeof(rxdataReset), 1000);
	//The c5 is not used in this case. For that reason, a 0x00 is used.
	return checkACK(huart, rxdataReset[0], rxdataReset[1], rxdataReset[2], rxdataReset[3], 0x00);
}

int getVersion(UART_HandleTypeDef huart){
	state = CAM_VERSION;
	storeInfo(CAM_VERSION);
	memset(rxdataGV, 0, sizeof(rxdataGV));
	HAL_UART_Transmit(&huart, txdataGV, sizeof(txdataGV), 100);
	HAL_UART_Receive(&huart, rxdataGV, sizeof(rxdataGV), 100);
	return checkACK(huart, rxdataGV[0], rxdataGV[1], rxdataGV[2], rxdataGV[3], 0x00);
}

int setResolution(UART_HandleTypeDef huart){
	state = CAM_SET_RESOLUTION;
	storeInfo(CAM_SET_RESOLUTION);
	txdataSR[8] = resolution;
	memset(rxdataSR, 0, sizeof(rxdataSR));
	HAL_UART_Transmit(&huart, txdataSR, sizeof(txdataSR), 100);
	HAL_UART_Receive(&huart, rxdataSR, sizeof(rxdataSR), 100);
	return checkACK(huart, rxdataSR[0], rxdataSR[1], rxdataSR[2], rxdataSR[3], rxdataSR[4]);
}

int setCompressibility(UART_HandleTypeDef huart){
	state = CAM_SET_COMPRESSIBILITY;
	storeInfo(CAM_SET_COMPRESSIBILITY);
	txdataSC[8] = compressibility;
	memset(rxdataSC, 0, sizeof(rxdataSC));
	HAL_UART_Transmit(&huart, txdataSC, sizeof(txdataSC), 100);
	HAL_UART_Receive(&huart, rxdataSC, sizeof(rxdataSC) ,100);
	return checkACK(huart, rxdataSC[0], rxdataSC[1], rxdataSC[2], rxdataSC[3], rxdataIM[4]);
}

int startCapture(UART_HandleTypeDef huart){
	state = CAM_START_CAPTURE;
	storeInfo(CAM_START_CAPTURE);
	memset(rxdataCI, 0, sizeof(rxdataCI));
	HAL_UART_Transmit(&huart, txdataCI, sizeof(txdataCI), 1000);
	HAL_UART_Receive(&huart, rxdataCI, sizeof(rxdataCI), 1000);
	return checkACK(huart, rxdataCI[0], rxdataCI[1], rxdataCI[2], rxdataCI[3], rxdataCI[4]);
}

int getDataLength(UART_HandleTypeDef huart){
	state = CAM_GET_DATA_LENGTH;
	storeInfo(CAM_GET_DATA_LENGTH);
	memset(rxdataDL, 0, sizeof(rxdataDL));
	HAL_UART_Transmit(&huart, txdataDL, sizeof(txdataDL), 100);
	HAL_UART_Receive(&huart, rxdataDL, sizeof(rxdataDL), 100);
	//Put the necessary data from the DataLength command
	return checkACK(huart, rxdataDL[0], rxdataDL[1], rxdataDL[2], rxdataDL[3], rxdataDL[4]);
}

int getData(UART_HandleTypeDef huart){
	state = CAM_GET_DATA;
	storeInfo(CAM_GET_DATA);
	imagelength=256U*rxdataDL[7]+rxdataDL[8]+8;
	memset(rxdataIM, 0, sizeof(rxdataIM));
	HAL_UART_Transmit(&huart, txdataIM, sizeof(txdataIM), 30000);
	HAL_UART_Receive_DMA(&huart, rxdataIM,4096); //1000 bytes is around the smallest photo to be received
	while (!doneTransfer)
	{
	  if (((imagelength-indxVGA)>0) && ((imagelength-indxVGA)<2048))
	  {
	    if (HTC==1)
	    {
	      memcpy(auxrxdata,rxdataIM+2048,2048);
	      vTaskDelay(200);
	      store_flash_memory(PHOTO_ADDR+indxVGA, (uint8_t *) auxrxdata, (imagelength-indxVGA));
	      indxVGA = imagelength;
	      memset(auxrxdata, '\0', 2048);
	      isSizeRxed = 0;
	      HTC = 0;
	      HAL_UART_DMAStop(&huart);
	      doneTransfer=1;
	    }

	  else if (FTC==1)
	  {
		 vTaskDelay(50);
	     store_flash_memory(PHOTO_ADDR+indxVGA, (uint8_t *) rxdataIM, (imagelength-indxVGA));
	     indxVGA = imagelength;
	     isSizeRxed = 0;
	     FTC = 0;
	     HAL_UART_DMAStop(&huart);
	     doneTransfer=1;

	  }
	  }
	  else if ((indxVGA == imagelength) && ((HTC==1)||(FTC==1)))
	  {
		  isSizeRxed = 0;
		  HTC = 0;
		  FTC = 0;
		  HAL_UART_DMAStop(&huart);
	      doneTransfer=1;
	  }
	}
	return 1;
}

int stopCapture(UART_HandleTypeDef huart){
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

int initCam(UART_HandleTypeDef huart, uint8_t res, uint8_t comp, uint8_t *array){

	storeInfo(CAM_START);

	vTaskDelay(3000);

	storeInfo(res);
	storeInfo(comp);

	resolution = res;
	compressibility = comp;

	if(!reset(huart)){
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return 0;
	}

	if(!getVersion(huart)){
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return 0;
	}

	if(!setResolution(huart)){		//res is the Resolution used.
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return 0;
	}

	if(!setCompressibility(huart)){	//comp is the compression used.
		storeInfo(CAM_ERROR);
		for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
		return 0;
	}

	storeInfo(CAM_END);
	//This part returns the array to the Buffer
	for ( uint8_t a = 0; a < numerrorsVGA; a++ ) { array[a] = infoBuffer[a]; }
	return 1;
}

uint16_t getPhoto(UART_HandleTypeDef huart, uint8_t *array){

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
	return imagelength;
}



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 																			 *
 * 							"Checking"&"Extra" Functions				     *
 *  @These functions are used in order to check the data from the sensor.    *
 *   Check ack analyses if the ack is not the correct one. If it does not    *
 *   work (return 0)m it applied the error protocol. If it does not work *
 *   it ends returning 0.                                                *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 */


void storeInfo(uint8_t info){
	infoBuffer[numerrorsVGA] = info;
	if(info == CAM_START){
		memset(infoBuffer, 0, sizeof(infoBuffer));
		infoBuffer[numerrorsVGA] = info;
		numerrorsVGA = 0;
	}
	numerrorsVGA++;
}


int checkACK(UART_HandleTypeDef huart, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5){
	//All the ACKs are formed by these structure. If this does not work, it interrupts
	if(c1 != 0x76 || c2 != 0x00){ if(anErrorHappened == 1){ return 0; } return errP(huart);}
	//Now, it is determined below in each case:

	//RESET: 0x76 0x00 0x26 0x00
	if(state == CAM_RESET && (c3 != 0x26 || c4 != 0x00)){ if(anErrorHappened == 1){ return 0; } return errP(huart); }

	//GET VERSION: 0x76 0x00 0x11 0x00
	if(state == CAM_VERSION && (c3 != 0x11 || c4 != 0x00)){ if(anErrorHappened == 1){ return 0; } return errP(huart); }

	//SET RESOLUTION: 0x76 0x00 0x31 0x00 0x00
	if(state == CAM_SET_RESOLUTION && (c3 != 0x31 || c4 != 0x00 || c5 != 0x00)){ if(anErrorHappened == 1){ return 0; } return errP(huart); }

	//SET COMPRESSIBILITY: 0x76 0x00 0x31 0x00 0x00
	if(state == CAM_SET_COMPRESSIBILITY && (c3 != 0x31 || c4 != 0x00 || c5 != 0x00)){ if(anErrorHappened == 1){ return 0; } return errP(huart); }

	//SET START CAPTURE: 0x76 0x00 0x36 0x00 0x00
	if(state == CAM_START_CAPTURE && ( c3 != 0x36 || c4 != 0x00 || c5 != 0x00) ){ if(anErrorHappened == 1){ return 0; } return errP(huart); }

	//GET DATA LENGTH: 0x76 0x00 0x34 0x00 0x00
	if(state == CAM_GET_DATA_LENGTH && (c3 != 0x34 || c4 != 0x00 || c5 != 0x04)){ if(anErrorHappened == 1){ return 0; } return errP(huart); }

	//GET DATA: 0x76 0x00 0x32 0x00
	if(state == CAM_GET_DATA && (c3 != 0x32 || c4 != 0x00)){ if(anErrorHappened == 1){ return 0; } return errP(huart); }

	//STOP CAPTURE 0x76 0x00 0x36 0x00 0x00
	if(state == CAM_STOP_CAPTURE && (c3 != 0x36 || c4 != 0x00 || c5 != 0x00)){ if(anErrorHappened == 1){ return 0; } return errP(huart); }

	return 1;

}

int errP(UART_HandleTypeDef huart){

	uint8_t attempts = 0;
	int err;
	vTaskDelay(250);
	anErrorHappened = 1;

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

			if(err == 1){
				anErrorHappened = 0;
				return 1;
			}
			attempts++;
	}
	//It is restored, besides the error persist.
	anErrorHappened = 0;
	return 0;
}


void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
	if (isSizeRxed == 0)
	{
		indxVGA = 0;
		memcpy(auxrxdata,rxdataIM+5,2048);
		store_flash_memory(PHOTO_ADDR+indxVGA,  (uint8_t *) auxrxdata, 2048);
		memset(rxdataIM, '\0', 2048);  // clear the RxData buffer
		memset(auxrxdata, '\0', 2048);  // clear the auxrxdata buffer
		indxVGA += 2048;  // update the indxVGA variable
		isSizeRxed = 1;  // set the variable to 1 so that this loop does not enter again
	}
	else
	{
		store_flash_memory(PHOTO_ADDR+indxVGA, (uint8_t *) rxdataIM, 2048);
		memset(rxdataIM, '\0', 2048);
		indxVGA += 2048;
	}
	HTC=1;  // Half transfer complete callback was called
	FTC=0;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	  memcpy(auxrxdata,rxdataIM+2048,2048);
	  store_flash_memory(PHOTO_ADDR+indxVGA, (uint8_t *) auxrxdata, 2048);
	  memset(rxdataIM+2048, '\0', 2048);
	  memset(auxrxdata, '\0', 2048);
	  indxVGA+=2048;
	  HTC=0;
	  FTC=1;
}
