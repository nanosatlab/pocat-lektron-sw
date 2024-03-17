/*
 * RFI.c
 *
 *  Created on: Dec 26, 2022
 *      Author: mxarg
 */

#include <math.h>
#include "RFI.h"
#include "main.h"

//Internal
//extern ADC_HandleTypeDef hadc1;
//
////User defined variables
//extern tRFIsweepCapture RFIsweepCapture;
//extern uint16_t dmaBuffer1[N], dmaBuffer2[N], dmaBuffer3[N], id;
//extern uint32_t calibBuffer[NUM_FREQ], cnt, ind, ind1, ind2, ind3;
//
//
//
///* Called every time the DMA buffer is filled */
//void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc){
//	uint16_t freq_cnt = 0;
//	float mn, stdv, ku, sk;
//
//	HAL_GPIO_TogglePin (GPIOA, GPIO_PIN_5); //Turn LED on/off to know it's working
//
//	freq_cnt = cnt%NUM_FREQ;	//As NUM_FREQ is not multiple from 3, two different counters have to be used.
//									//cnt takes values from 0 to NUM_FREQ*3-1
//									//freq_cnt takes values from 0 to NUM_FREQ-1
//	if(cnt%3 == 0){ //Every time this condition is fulfilled, the dmaBuffer1 has been filled and will be processed,
//	 	 	 	 	//and the dmaBuffer2 has been processed and will be filled
//
//		cnt = (cnt + 1)%(NUM_FREQ*3);
//		update_VCO();
//		if(HAL_ADC_Start_DMA(&hadc1, (uint32_t *) dmaBuffer2, N) != HAL_OK){
//			Error_Handler();
//		}
//		equalize(calibBuffer[freq_cnt], dmaBuffer1);
//		mn = calculate_mean_int(dmaBuffer1);
//		calculate_stdv_ku_sk(&stdv, &ku, &sk, dmaBuffer1, mn);
//		save_values(&RFIsweepCapture.captures[freq_cnt], mn, stdv, ku, sk);
//		statistical_alg(dmaBuffer1, &RFIsweepCapture, ku, sk, freq_cnt);
//		time_alg(dmaBuffer1, &RFIsweepCapture, mn, stdv, freq_cnt);
//		ind1++;
//	}else if(cnt%3 == 1){//Every time this condition is fulfilled, the dmaBuffer2 has been filled and will be processed,
//		 	 	 	 	 //and the dmaBuffer3 has been processed and will be filled
//		cnt = (cnt + 1)%(NUM_FREQ*3);
//		update_VCO();
//		if(HAL_ADC_Start_DMA(&hadc1, (uint32_t *)dmaBuffer3, N) != HAL_OK){
//			Error_Handler();
//		}
//		equalize(calibBuffer[freq_cnt], dmaBuffer2);
//		mn = calculate_mean_int(dmaBuffer2);
//		calculate_stdv_ku_sk(&stdv, &ku, &sk, dmaBuffer2, mn);
//		save_values(&RFIsweepCapture.captures[freq_cnt], mn, stdv, ku, sk);
//		statistical_alg(dmaBuffer2, &RFIsweepCapture, ku, sk, freq_cnt);
//		time_alg(dmaBuffer2, &RFIsweepCapture, mn, stdv, freq_cnt);
//		ind2++;
//	}else{//Every time cnt%3 == 2, the dmaBuffer3 has been filled and will be processed,
//	 	  //and the dmaBuffer1 has been processed and will be filled
//		cnt = (cnt + 1)%(NUM_FREQ*3);
//		update_VCO();
//		if(HAL_ADC_Start_DMA(&hadc1, (uint32_t *)dmaBuffer1, N) != HAL_OK){
//			Error_Handler();
//		}
//		equalize(calibBuffer[freq_cnt], dmaBuffer3);
//		mn = calculate_mean_int(dmaBuffer3);
//		calculate_stdv_ku_sk(&stdv, &ku, &sk, dmaBuffer3, mn);
//		save_values(&RFIsweepCapture.captures[freq_cnt], mn, stdv, ku, sk);
//		statistical_alg(dmaBuffer3, &RFIsweepCapture, ku, sk, freq_cnt);
//		time_alg(dmaBuffer3, &RFIsweepCapture, mn, stdv, freq_cnt);
//		ind3++;
//	}
//
//	if(freq_cnt == NUM_FREQ-1){ //After NUM_FREQ frequency bins, the frequency algorithm is run.
//		frequency_alg(&RFIsweepCapture);
//		comb_decision(&RFIsweepCapture);
//		RFIsweepCapture.id = id;
//		if(id==UINT16_MAX){//If the timestamp exceeds its maximum value
//			id = 0;
//		}else{
//			id++;
//		}
//		//Save results missing --> OBC
//		reset_decision(&RFIsweepCapture);
//	}
//	ind++;
//}




void equalize(float frequency, uint16_t samples[N_SAMPLES]){
	for(uint16_t i=0; i<N_SAMPLES; i++){
		float freq = frequency/1000;
		float calib = 0;
//		float calib = -547.79*freq*freq*freq*freq + 13833*freq*freq*freq - 130667*freq*freq + 547358*freq - 858035;
		if(freq>=6.3){
			calib = 210*freq - 1317.7;
		}else{
			calib = 13*freq - 75;
		}

		samples[i] = samples[i] - calib;
	}
}




/* Calculates the mean value of the samples */
float calculate_mean_int(uint16_t samples[N_SAMPLES]){
    float sum = 0;
    for(uint16_t i=0; i<N_SAMPLES; i++){
        sum = sum + samples[i];
    }
    return sum/N_SAMPLES;
}




float calculate_mean_float(tRFIbinCapture captures[NUM_FREQ]){
    float sum = 0.0;
    for(uint16_t i=0; i<NUM_FREQ; i++){
        sum = sum + captures[i].mean;
    }
    return sum/NUM_FREQ;
}




/* Calculates the median value of the samples */
float calculate_median(uint16_t samples[N_SAMPLES]){
	uint16_t aux = 0;
	for (uint16_t i=1; i<=N_SAMPLES-1; i++){
		for (uint16_t j = 1 ; j <= N_SAMPLES-i ; j++) {
			if (samples[j] <= samples[j+1]){
				aux = samples[j];
				samples[j] = samples[j+1];
				samples[j+1] = aux;
			}
		}
	}
	if(N_SAMPLES%2 ==0){
		return (samples[N_SAMPLES/2] + samples[N_SAMPLES/2+1])/2.0 ;
	}else{
		return samples[N_SAMPLES/2 + 1];
	}
}




/* Calculates the MAD value of the samples */
float calculate_mad(uint16_t samples[N_SAMPLES]){
	float median = calculate_median(samples);
	uint16_t substraction[N_SAMPLES];
	for(int i=0;i<N_SAMPLES;i++){
		substraction[i] = samples[i]-median; //samples[i]-median will be a float. It is however approximated
	}
	return calculate_median(substraction);
}




/* Calculates the standard deviation, kurtosis and skewness value of the samples */
void calculate_stdv_ku_sk(float *stdeviation, float *kurt, float *skew, uint16_t samples[N_SAMPLES], float mean){
    double err = 0.0, err2 = 0.0, numer_ku = 0.0, numer_sk = 0.0;
    for(uint16_t i=0; i<N_SAMPLES; i++){
        err = samples[i]-mean;
        err2 = err2 + pow(err,2);
        numer_ku = numer_ku + pow(err,4);
        numer_sk = numer_sk + pow(err,3);
    }
    *stdeviation = sqrt(err2/(N_SAMPLES-1));
    if(err2==0){
    	*kurt = 0;
		*skew = 0;
    }else{
    	*kurt = N_SAMPLES*numer_ku/pow(err2,2);
		*skew = 1000*sqrt(N_SAMPLES)*numer_sk/pow(err2,1.5); //Multiplied by 1000 to better manage small numbers
    }
}




float calculate_stdv(tRFIbinCapture captures[NUM_FREQ], float mn){
	double err2 = 0.0;
    for(uint16_t i=0; i<NUM_FREQ; i++){
        err2 = err2 + pow(captures[i].mean-mn,2);
    }
	return sqrt(err2/(NUM_FREQ-1));
}




void save_values(tRFIbinCapture *RFIbinCapture, float mn, float stdv, float ku, float sk){
	(*RFIbinCapture).mean = (uint16_t) mn;
	(*RFIbinCapture).stdv = (uint16_t) 10*stdv;
	(*RFIbinCapture).ku = (uint16_t) 10*ku;
	(*RFIbinCapture).sk = (int16_t) sk; //It has already been multiplied by 1000
}




void update_VCO(){

}




/* STATISTICAL DOMAIN ALGORITHM */
void statistical_alg(uint16_t samples[N_SAMPLES], tRFIsweepCapture *RFIsweepCapture, float ku, float sk, uint16_t freq_cnt){
    if( (ku<KU_LOWER_THRES) || (ku>KU_UPPER_THRES) || (sk<SK_LOWER_THRES) || (sk>SK_UPPER_THRES) ){
    	(*RFIsweepCapture).captures[freq_cnt].decision = (*RFIsweepCapture).captures[freq_cnt].decision + 0b00000011;
    }
}




/* TIME DOMAIN ALGORITHM */
void time_alg(uint16_t samples[N_SAMPLES], tRFIsweepCapture *RFIsweepCapture, float mn, float stdv, uint16_t freq_cnt){
    (*RFIsweepCapture).captures[freq_cnt].Ndet = 0;
    //If the stdv is used:
    float th = AGGRESSIVENESS_TIME*stdv + mn;
    //If MAD is used:
    //float th = K*AGGRESSIVENESS*calculate_mad(samples) + mn;
    for(uint16_t i=0; i<N_SAMPLES; i++){
    	if(samples[i]>th){
    		(*RFIsweepCapture).captures[freq_cnt].Ndet = (*RFIsweepCapture).captures[freq_cnt].Ndet + 1;//Guardar: ku, sk, Ndet,
    	}
    }
    if((float)(*RFIsweepCapture).captures[freq_cnt].Ndet/N_SAMPLES>PERCENTAGE_TH){
    	(*RFIsweepCapture).captures[freq_cnt].decision = (*RFIsweepCapture).captures[freq_cnt].decision + 0b00001100;
    }
}




/* FREQUENCY DOMAIN ALGORITHM */
void frequency_alg(tRFIsweepCapture *RFIsweepCapture){
    float mn = calculate_mean_float((*RFIsweepCapture).captures);
    float stdv = calculate_stdv((*RFIsweepCapture).captures, mn);
    float th = AGGRESSIVENESS_FREQ*stdv + mn;
    for(uint16_t i=0; i<NUM_FREQ; i++){
    	if((*RFIsweepCapture).captures[i].mean > th){
			(*RFIsweepCapture).captures[i].decision = (*RFIsweepCapture).captures[i].decision + 0b00110000;
    	}
    }
}



/* Combines all decisions */
void comb_decision(tRFIsweepCapture *RFIsweepCapture){
    for(uint16_t i=0; i<NUM_FREQ; i++){
		if((*RFIsweepCapture).captures[i].decision != 0){
			(*RFIsweepCapture).captures[i].decision = (*RFIsweepCapture).captures[i].decision + 0b11000000;
		}
    }
}




void reset_decision(tRFIsweepCapture *RFIsweepCapture){
	for(uint16_t i=0; i<NUM_FREQ; i++){
		(*RFIsweepCapture).captures[i].decision = 0;
	}
}



