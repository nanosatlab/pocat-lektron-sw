/*
 * state_functions.h
 *
 *  Created on: Nov 9, 2022
 *      Author: NilRi
 */

#ifndef INC_CLOCK_H_
#define INC_CLOCK_H_

#include "definitions.h"
#include "flash.h"
#include "main.h"

#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_pwr_ex.h"
#include "stm32l4xx_hal_rcc.h"

void Clock_80MHz();
void Clock_26MHz();
void Clock_2MHz();
void Clock_8MHz();

#endif /* INC_CLOCK_H_ */
