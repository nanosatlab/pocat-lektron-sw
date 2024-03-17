/*
 * RFI_Thread.h
 *
 *  Created on: 9 mar. 2023
 *      Author: roger
 */

#ifndef INC_RFI_THREAD_H_
#define INC_RFI_THREAD_H_

#include "RFI.h"
#include "main.h"
#include "cmsis_os.h"
//#include "../../CMSIS_DSP/Include/arm_math.h"


extern ADC_HandleTypeDef hadc1;
//extern DMA_HandleTypeDef hdma_adc1;

extern DAC_HandleTypeDef hdac1;

extern TaskHandle_t RFI_Handle;

#define fLow			((float) 5600)		// VCO lower frequency [MHz]
#define fHigh			((float) 7000)		// VCO higher frequency [MHz]


void Init_RFIPayload();
void Start_RFIPayload();
void Check_RFIPayload();
void Measure_loop();
void Stop_RFIPayload();


#endif /* INC_RFI_THREAD_H_ */
