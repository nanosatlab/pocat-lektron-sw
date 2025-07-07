/*
 * DS2782E.h
 *
 *  Created on: Dec 11, 2023
 *      Author: marccorretge
 */

#ifndef DS2782E_H_
#define DS2782E_H_

#include <stdio.h>
#include <stdint.h>
#include "stm32l4xx_hal.h"
#include "flash.h"
//#include "stm32l4xx_it.h"

HAL_StatusTypeDef readDS2782Volt(I2C_HandleTypeDef i2c, float *v);
HAL_StatusTypeDef readDS2782Temp(I2C_HandleTypeDef i2c, float *t);
HAL_StatusTypeDef readDS2782Amp(I2C_HandleTypeDef i2c, float *a);
HAL_StatusTypeDef readDS2782AVGAmp(I2C_HandleTypeDef i2c, float *avg_a);
HAL_StatusTypeDef readDS2782ACRAmp(I2C_HandleTypeDef i2c, float *acr_a);
HAL_StatusTypeDef readDS2782ACRLAmp(I2C_HandleTypeDef i2c, float *acrl_a);
HAL_StatusTypeDef readDS2782RAAC(I2C_HandleTypeDef i2c, float *raac);
HAL_StatusTypeDef readDS2782RSAC(I2C_HandleTypeDef i2c, float *rsac);
HAL_StatusTypeDef readDS2782RARC(I2C_HandleTypeDef i2c, int *rarc);
HAL_StatusTypeDef readDS2782RSRC(I2C_HandleTypeDef i2c, int *rsrc);
void setRSNSP(I2C_HandleTypeDef i2c);

#endif /* DS2782E_H_ */
