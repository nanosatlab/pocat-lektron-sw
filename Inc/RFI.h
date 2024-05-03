/*
 * RFI.h
 *
 *  Created on: Dec 26, 2022
 *      Author: mxarg
 */

#ifndef INC_RFI_H_
#define INC_RFI_H_

#include "main.h"



//CONSTANTS
//General
#define N_SAMPLES	1000	//Number of samples to capture in one frequency bin
#define NUM_FREQ 62	//Must be an even value!
					//NUM_FREQ = 200 for L-band
					//NUM_FREQ = 50 for K-band
#define R	10		//Number of repetitions for the calibration step

//For statistical domain algorithm
#define KU_UPPER_THRES	3.3
#define KU_LOWER_THRES	2.7
#define SK_UPPER_THRES	200 	//Skewness multiplied by 1000
#define SK_LOWER_THRES	-200 //Skewness multiplied by 1000

//For time domain algorithm
#define AGGRESSIVENESS_TIME	1.5
#define PERCENTAGE_TH	0.01
#define K	1.3339

//For frequency domain algorithm
#define AGGRESSIVENESS_FREQ 1.5



//STRUCTURES DEFINITION
typedef struct{
	uint16_t mean;
	uint16_t stdv;
	uint16_t ku;
	int16_t sk;
	uint16_t Ndet;
	uint8_t decision;
} tRFIbinCapture;

typedef struct{
	uint16_t id;
	tRFIbinCapture captures[NUM_FREQ];
} tRFIsweepCapture;



//FUNCTION HEADERS
void equalize(float frequency, uint16_t samples[N_SAMPLES]);
float calculate_mean_int(uint16_t samples[N_SAMPLES]);
float calculate_mean_float(tRFIbinCapture captures[NUM_FREQ]);
float calculate_median(uint16_t samples[N_SAMPLES]);
float calculate_mad(uint16_t samples[N_SAMPLES]);
void calculate_stdv_ku_sk(float *stdeviation, float *kurt, float *skew, uint16_t samples[N_SAMPLES], float mean);
float calculate_stdv(tRFIbinCapture captures[NUM_FREQ], float mn);
void save_values(tRFIbinCapture *RFIbinCapture, float mn, float stdv, float ku, float sk);
void update_VCO();
void statistical_alg(uint16_t samples[N_SAMPLES], tRFIsweepCapture *RFIsweepCapture, float ku, float sk, uint16_t freq_cnt);
void time_alg(uint16_t samples[N_SAMPLES], tRFIsweepCapture *RFIsweepCapture, float mn, float stdv, uint16_t freq_cnt);
void frequency_alg(tRFIsweepCapture *RFIsweepCapture);
void comb_decision(tRFIsweepCapture *RFIsweepCapture);
void reset_decision(tRFIsweepCapture *RFIsweepCapture);

#endif /* INC_RFI_H_ */
