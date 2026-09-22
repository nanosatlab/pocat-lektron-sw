/*
 * RFI_Thread.c
 *
 *  Created on: 9 mar. 2023
 *      Author: roger
 */

#include "RFI_Thread.h"

tRFIsweepCapture RFIsweepCapture;
uint32_t dmaBuffer1[N_SAMPLES], id;
uint32_t calibBuffer[NUM_FREQ], cnt, ind, ind1;
uint16_t freq_cnt = 0;
uint16_t freqStep = 16;
float frequency = 0;
float mn, stdv, ku, sk;

//#define SAMPLES                    512             /* 256 real party and 256 imaginary parts */
//#define FFT_SIZE                SAMPLES / 2        /* FFT size is always the same size as we have samples, so 256 in our case */
//float32_t Output[FFT_SIZE];

void Init_RFIPayload() {
	for (uint16_t i = 0; i < NUM_FREQ; i++) {
		calibBuffer[i] = 0;
	}
	id = 0;
	cnt = 0;
	ind = 0;
	ind1 = 0;
}

void prepare_variables() {
	frequency = 6670 - 869 + freqStep * freq_cnt;
//	frequency = 6670 - 869 + 100*freq_cnt;
}

void configure_VCO() {
	float voltage;
	double freq;
	long double c5_1, c4_1, c3_1, c2_1, c1_1, c0_1;
	long double c5_2, c4_2, c3_2, c2_2, c1_2, c0_2;
	long double c3_3, c2_3, c1_3, c0_3;
	if (frequency >= fLow && frequency < 6550) {
		freq = (double) frequency / 1000;
		c5_1 = 0.57331364985 * freq * freq * freq * freq * freq; //5e grau
		c4_1 = 16.3022043769 * freq * freq * freq * freq; //4t grau
		c3_1 = 183.334092862 * freq * freq * freq; //3r grau
		c2_1 = 1017.62338716 * freq * freq; //2n grau
		c1_1 = 2780.8933734 * freq; //1r grau
		c0_1 = 2981.93597967; //constant
		voltage = -c5_1 + c4_1 - c3_1 + c2_1 - c1_1 + c0_1;
	} else if (frequency >= 6550 && frequency < 6732) {
		freq = (double) frequency / 1000;
		c5_2 = 1407.159538 * freq * freq * freq * freq * freq; //5e grau
		c4_2 = 46509.43328 * freq * freq * freq * freq; //4t grau
		c3_2 = 614835.7857 * freq * freq * freq; //3r grau
		c2_2 = 4063560.641 * freq * freq; //2n grau
		c1_2 = 13427126.52 * freq; //1r grau
		c0_2 = 17745056.92; //constant
		voltage = c5_2 - c4_2 + c3_2 - c2_2 + c1_2 - c0_2;
	} else if (frequency >= 6732 && frequency <= fHigh) {
		freq = (double) frequency / 1000;
		c3_3 = 21.0023922573571 * freq * freq * freq; //3r grau
		c2_3 = 434.483189612138 * freq * freq; //2n grau
		c1_3 = 2989.05529791004 * freq; //1r grau
		c0_3 = 6840.88295494965; //constant
		voltage = -c3_3 + c2_3 - c1_3 + c0_3;
	} else
		voltage = -1;

	int32_t DACvalue = voltage * 4096.00000000 / 3.3000000;
	HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, DACvalue);
	vTaskDelay(pdMS_TO_TICKS(5));
}

void read_RSSI() {
	float mn, stdv, ku, sk;

//	HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5); //Turn LED on/off to know it's working

//	freq_cnt = cnt % NUM_FREQ; //As NUM_FREQ is not multiple from 3, two different counters have to be used.
							   //cnt takes values from 0 to NUM_FREQ*3-1
							   //freq_cnt takes values from 0 to NUM_FREQ-1

	//Every time this condition is fulfilled, the dmaBuffer1 has been filled and will be processed,
	//and the dmaBuffer2 has been processed and will be filled
//	if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*) dmaBuffer1, N) != HAL_OK) {
//		Error_Handler();
//	}
	//HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);

//	for (int i = 0; i < N_SAMPLES; i++) {
//		HAL_ADC_Start(&hadc1);
//		HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
//		dmaBuffer1[i] = HAL_ADC_GetValue(&hadc1);
//	}

//	HAL_ADC_Start(&hadc1);
//	for (int i = 0; i < N_SAMPLES; i++) {
//		HAL_ADC_PollForConversion(&hadc1,HAL_MAX_DELAY);
//		dmaBuffer1[i] = HAL_ADC_GetValue(&hadc1);
////		__HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOS);
//	}

	//uint32_t prova =0;
	HAL_ADC_Start(&hadc1);
	for (int i = 0; i < N_SAMPLES; i++) {
		dmaBuffer1[i] = HAL_ADC_GetValue(&hadc1);
	}
	HAL_ADC_Stop(&hadc1);

	//HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
	equalize(frequency, dmaBuffer1);
	mn = calculate_mean_int(dmaBuffer1);
	calculate_stdv_ku_sk(&stdv, &ku, &sk, dmaBuffer1, mn);
	save_values(&RFIsweepCapture.captures[freq_cnt], mn, stdv, ku, sk);
	statistical_alg(dmaBuffer1, &RFIsweepCapture, ku, sk, freq_cnt);
	time_alg(dmaBuffer1, &RFIsweepCapture, mn, stdv, freq_cnt);
//	calc_fft(dmaBuffer1);
	ind1++;
//	vTaskSuspend(RFI_Handle);
}

//void calc_fft(uint32_t Input[N_SAMPLES]) {
//	arm_cfft_radix4_instance_f32 S; /* ARM CFFT module */
//	float32_t maxValue; /* Max FFT value is stored here */
//	uint32_t maxIndex; /* Index in Output array where max value is */
//	uint16_t i;
//
//	/* Initialize the CFFT/CIFFT module, intFlag = 0, doBitReverse = 1 */
//	arm_cfft_radix4_init_f32(&S, FFT_SIZE, 0, 1);
//	/* Process the data through the CFFT/CIFFT module */
//	arm_cfft_radix4_f32(&S, (float32_t*)Input);
//	/* Process the data through the Complex Magniture Module for calculating the magnitude at each bin */
//	arm_cmplx_mag_f32((float32_t*)Input, &Output, FFT_SIZE);
//	/* Calculates maxValue and returns corresponding value */
//	arm_max_f32(&Output, FFT_SIZE, &maxValue, &maxIndex);
//}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
	int __attribute__((unused)) i = 0;
	int __attribute__((unused)) a = 3 + i;
}

void check_notis() {

}

void check_final() {
	if (freq_cnt == NUM_FREQ - 1) { //After NUM_FREQ frequency bins, the frequency algorithm is run.
		frequency_alg(&RFIsweepCapture);
		comb_decision(&RFIsweepCapture);
		RFIsweepCapture.id = id;
		if (id == UINT16_MAX) { //If the timestamp exceeds its maximum value
			id = 0;
		} else {
			id++;
		}
		//Save results missing --> OBC
		reset_decision(&RFIsweepCapture);
	}
}

void Start_RFIPayload() {

}

void Check_RFIPayload() {

}

void Stop_RFIPayload() {

}

void Measure_loop() {
	for (freq_cnt = 0; freq_cnt < NUM_FREQ; freq_cnt++) {
		prepare_variables();
		configure_VCO();
		read_RSSI(); //provar fer continuousConvMode perque aixi el sampling time es molt menor i igualaria un dma (hauria de dividir el loop perque el contConvMode va amb interrupts)
		check_final();
		ind++;
		check_notis();
	}
}
