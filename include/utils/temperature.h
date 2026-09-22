/**
 * @file temperature.h
 * @brief Internal MCU temperature sensor reading.
 */

#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdint.h>

/**
 * @brief Read the internal MCU temperature.
 * @return Temperature in degrees Celsius (signed).
 */
int8_t mcu_get_temperature(void);

/**
 * @brief Read the MCU supply voltage (Vdda) using the internal reference.
 * @return Vdda in millivolts, or 0 on error.
 */
uint16_t mcu_get_vdda_mv(void);

#endif /* TEMPERATURE_H */
