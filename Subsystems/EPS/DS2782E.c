/*
 * DS2782E.c
 *
 *  Created on: Dec 11, 2023
 *      Author: marccorretge
 */

#include <stdio.h>
#include <stdint.h>
#include "DS2782E.h"
#include "stm32l4xx_hal.h"
//#include "stm32l4xx_it.h"

// Datasheet: https://www.mouser.es/datasheet/2/256/DS2782-1389185.pdf
uint8_t DS2782_I2C_ADDRESS = 0x34 << 1;
uint8_t DS2782_VOLT_REG = 0x0C;
uint8_t DS2782_TEMP_REG = 0x0A;
uint8_t DS2782_AMP_REG = 0x0E;

uint8_t DS2782_AVGAMP_REG = 0x08;	// Average current
uint8_t DS2782_ACRAMP_REG = 0x10;	// Accumulated Current Register
uint8_t DS2782_ACRLAMP_REG = 0x12;	// Low Accumulated Current Register

uint8_t DS2782_RAAC_REG = 0x02;	// Remaining Active Absolute Capacity Register
uint8_t DS2782_RSAC_REG = 0x04;	// Remaining Standby Absolute Capacity Register
uint8_t DS2782_RARC_REG = 0x06;	// Remaining Active Relative Capacity Register
uint8_t DS2782_RSRC_REG = 0x07;	// Remaining Standby Relative Capacity Register

float Rsns = 15e-3;	// Resistencia (ohms) per calcular corrent

typedef struct {
	/*
	uint8_t T;
	uint8_t V;
	uint8_t I;
	uint8_t RAAC;
	uint8_t RSAC;
	uint8_t AVG_I;
	uint8_t ACR_I;
	uint8_t ACRL_I;
	uint8_t RARC;
	uint8_t RSRC;
	*/
	uint8_t Prova;
}DS2782_Data_t;

HAL_StatusTypeDef readDS2782Volt(I2C_HandleTypeDef i2c, float *v){

	HAL_StatusTypeDef ret;

	uint8_t data[2];
	uint16_t volt = 0;
	int sign;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_VOLT_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 2, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	volt = data[0]<<8 | data[1];

	if(volt >> 15 == 1){
		sign = -1;
		volt = ~volt + 1;
	}else{
		sign = 1;
	}

	volt = volt >> 5;
	*v = (float)volt * 4.88e-3 * sign; // Precisió de 4.88mV => 1V / 0.00488 = 204.918033

	printf("Voltage: %.2fV\r\n", *v);
	return ret;

}

HAL_StatusTypeDef readDS2782Temp(I2C_HandleTypeDef i2c, float *t){

	HAL_StatusTypeDef ret;

	uint8_t data[2];
	uint16_t temp = 0;
	int sign;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_TEMP_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 2, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	temp = data[0]<<8 | data[1];

	if(temp >> 15 == 1){
		sign = -1;
		temp = ~temp + 1;
	}else{
		sign = 1;
	}

	temp = temp >> 5;
	*t = (float)temp * 0.125 * sign; // Precisió de 0.125 => 1ºC / 0.125 = 8

	printf("Temperatura: %.2fC\r\n", *t);
	return ret;
}

HAL_StatusTypeDef readDS2782Amp(I2C_HandleTypeDef i2c, float *a){

	HAL_StatusTypeDef ret;

	uint8_t data[2];
	uint16_t amp = 0;
	int sign;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_AMP_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 2, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	amp = data[0]<<8 | data[1];

	if(amp >> 15 == 1){
			sign = -1;
			amp = ~amp + 1;
		}else{
			sign = 1;
		}


	*a = (float)amp * (1.5625e-6 / Rsns) * 1000 * sign;

	printf("Corrent: %.2fmA\r\n", *a);
	return ret;
}

HAL_StatusTypeDef readDS2782AVGAmp(I2C_HandleTypeDef i2c, float *avg_a){	// Average Current

	HAL_StatusTypeDef ret;

	uint8_t data[2];
	uint16_t avgamp = 0;
	int sign;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_AVGAMP_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 2, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	avgamp = data[0]<<8 | data[1];

	if(avgamp >> 15 == 1){
			sign = -1;
			avgamp = ~avgamp + 1;
		}else{
			sign = 1;
		}


	*avg_a = (float)avgamp * (1.5625e-6 / Rsns) * 1000 * sign;

	printf("AVG Corrent: %.2fmA\r\n", *avg_a);
	return ret;
}

HAL_StatusTypeDef readDS2782ACRAmp(I2C_HandleTypeDef i2c, float *acr_a){	// Accumulated Current Register

	HAL_StatusTypeDef ret;

	uint8_t data[2];
	uint16_t acramp = 0;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_ACRAMP_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 2, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	acramp = data[0]<<8 | data[1];


	*acr_a = (float)acramp * (6.25e-6 / Rsns) * 1000;

	printf("ACR Corrent: %.2fmAH\r\n", *acr_a);
	return ret;
}

HAL_StatusTypeDef readDS2782ACRLAmp(I2C_HandleTypeDef i2c, float *acrl_a){	// Low Accumulated Current Register

	HAL_StatusTypeDef ret;

	uint8_t data[2];
	uint16_t acrlamp = 0;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_ACRLAMP_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 2, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	acrlamp = data[0]<<8 | data[1];
	acrlamp = acrlamp >> 4;

	*acrl_a = (float)acrlamp * (1.526e-9 / Rsns) * 1000; // ?? No se si esta be

	printf("ACRL Corrent: %.2fmAH\r\n", *acrl_a);
	return ret;
}

HAL_StatusTypeDef readDS2782RAAC(I2C_HandleTypeDef i2c, float *raac){	// Remaining Active Absolute Capacity Register

	HAL_StatusTypeDef ret;

	uint8_t data[2];
	uint16_t raac_val = 0;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_RAAC_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 2, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	raac_val = data[0]<<8 | data[1];

	*raac = (float)raac_val * 1.6e-3 * 1000;

	printf("RAAC: %.2fmAH\r\n", *raac);
	return ret;
}

HAL_StatusTypeDef readDS2782RSAC(I2C_HandleTypeDef i2c, float *rsac){	// Remaining Standby Absolute Capacity Register

	HAL_StatusTypeDef ret;

	uint8_t data[2];
	uint16_t rsac_val = 0;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_RSAC_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 2, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	rsac_val = data[0]<<8 | data[1];

	*rsac = (float)rsac_val * 1.6e-3 * 1000;

	printf("RSAC: %.2fmAH\r\n", *rsac);
	return ret;
}

HAL_StatusTypeDef readDS2782RARC(I2C_HandleTypeDef i2c, int *rarc){	// Remaining Active Relative Capacity Register

	HAL_StatusTypeDef ret;

	uint8_t data[1];
	uint16_t rarc_val = 0;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_RARC_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	rarc_val = data[0];

	*rarc = rarc_val; // %

	printf("RARC: %d%\r\n", *rarc);
	return ret;
}

HAL_StatusTypeDef readDS2782RSRC(I2C_HandleTypeDef i2c, int *rsrc){	// Remaining Standby Relative Capacity Register

	HAL_StatusTypeDef ret;

	uint8_t data[1];
	uint16_t rsrc_val = 0;

	ret = HAL_I2C_Master_Transmit(&i2c, DS2782_I2C_ADDRESS, &DS2782_RARC_REG, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	ret = HAL_I2C_Master_Receive(&i2c, DS2782_I2C_ADDRESS|0x01, data, 1, HAL_MAX_DELAY);
	if(ret != HAL_OK){
		return ret;
	}

	rsrc_val = data[0];

	*rsrc = rsrc_val; // %

	printf("RSRC: %d%\r\n", *rsrc);
	return ret;
}


void getAllResultData(){

	uint16_t RAAC, RSAC, AVG_I, T, V, I, ACR_I, ACRL_I;
	uint8_t RARC, RSRC;
	HAL_StatusTypeDef ret;
	I2C_HandleTypeDef hi2c1;

	ret = readDS2782Temp(hi2c1, &T);
	ret = readDS2782Volt(hi2c1, &V);
	ret = readDS2782Amp(hi2c1, &I);
	ret = readDS2782AVGAmp(hi2c1, &AVG_I);
	ret = readDS2782ACRAmp(hi2c1, &ACR_I);
	ret = readDS2782ACRLAmp(hi2c1, &ACRL_I);
	ret = readDS2782RAAC(hi2c1, &RAAC);
	ret = readDS2782RSAC(hi2c1, &RSAC);
	ret = readDS2782RARC(hi2c1, &RARC);
	ret = readDS2782RSRC(hi2c1, &RSRC);

	DS2782_Data_t data;

	data.Prova = 0x00;

	store_flash_memory(EPS_DATA_ADDR, (uint8_t *)&data, sizeof(DS2782_Data_t));




}
