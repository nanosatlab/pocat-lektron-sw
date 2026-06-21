/**
 * @file ds2782.h
 * @brief Low-level driver for the Maxim DS2782 stand-alone fuel gauge.
 *
 * The DS2782 register file is auto-incrementing; a single I2C transaction
 * starting at RARC (0x06) and continuing through RSAC LSB (0x15) yields a
 * temporally consistent snapshot of every register the EPS task consumes.
 * Higher-level decoding (sign extension, unit conversion) lives in eps.c.
 */

#ifndef INC_SUBSYSTEMS_EPS_DS2782_H_
#define INC_SUBSYSTEMS_EPS_DS2782_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32l4xx_hal.h"

/* I2C slave address — DS2782 fixed 7-bit address is 0110100b = 0x34
 * (confirmed by on-bench HAL_I2C_IsDeviceReady scan). The STM32 HAL I2C API
 * expects the address pre-shifted by one bit, giving 0x68 for write
 * transactions (0x69 for read). */
#define DS2782_I2C_ADDR_7BIT    0x34u
#define DS2782_I2C_ADDR_8BIT    (DS2782_I2C_ADDR_7BIT << 1)

/* Register addresses (datasheet section "Memory Map"). */
#define DS2782_REG_RARC         0x06u   /* active relative capacity, %        */
#define DS2782_REG_RSRC         0x07u   /* standby relative capacity, %       */
#define DS2782_REG_IAVG_M       0x08u   /* 28 s averaged current, MSB         */
#define DS2782_REG_IAVG_L       0x09u
#define DS2782_REG_TEMP_M       0x0Au   /* temperature, MSB (11-bit signed)   */
#define DS2782_REG_TEMP_L       0x0Bu
#define DS2782_REG_VOLT_M       0x0Cu   /* voltage, MSB (11-bit unsigned)     */
#define DS2782_REG_VOLT_L       0x0Du
#define DS2782_REG_CURR_M       0x0Eu   /* instantaneous current, MSB (s16)   */
#define DS2782_REG_CURR_L       0x0Fu
#define DS2782_REG_ACR_M        0x10u   /* accumulated charge, MSB (signed)   */
#define DS2782_REG_ACR_L        0x11u
#define DS2782_REG_RAAC_M       0x12u   /* active absolute capacity, MSB      */
#define DS2782_REG_RAAC_L       0x13u
#define DS2782_REG_RSAC_M       0x14u   /* standby absolute capacity, MSB     */
#define DS2782_REG_RSAC_L       0x15u

/* Number of bytes in the burst-read window (RARC .. RSAC_L inclusive). */
#define DS2782_BURST_LEN        16u

/**
 * @brief Read the DS2782 telemetry register window in a single I2C transaction.
 * @param hi2c I2C handle (expected: &hi2c1).
 * @param[out] out Buffer of DS2782_BURST_LEN bytes. out[0] holds the value of
 *                 register DS2782_REG_RARC, out[15] holds DS2782_REG_RSAC_L.
 * @return true on HAL_OK, false on any I2C failure or NULL argument.
 *
 * On failure, logs the HAL status and the I2C ErrorCode register to USART2.
 */
bool ds2782_burst_read(I2C_HandleTypeDef *hi2c, uint8_t out[DS2782_BURST_LEN]);

/**
 * @brief Ping the DS2782 with HAL_I2C_IsDeviceReady and log the result.
 *
 * Intended for one-shot use at task start to confirm wiring/address. Non-fatal:
 * the EPS task should keep running even if the probe fails so the failure mode
 * is observable through telemetry instead of locking the system.
 *
 * @param hi2c I2C handle (expected: &hi2c1).
 * @return true if the slave acknowledged at DS2782_I2C_ADDR_8BIT.
 */
bool ds2782_probe(I2C_HandleTypeDef *hi2c);

/**
 * @brief Read only the voltage registers (VOLT_M, VOLT_L) as a minimal sanity
 *        test. Independent of the full burst-read path.
 *
 * @param hi2c I2C handle (expected: &hi2c1).
 * @param[out] raw_voltage_out Right-shifted 11-bit unsigned value (4.88 mV/LSB).
 *             Decode to millivolts with DS2782_Compute_Voltage(), or directly:
 *             mV = raw * 488 / 100.
 * @return true on HAL_OK, false on any I2C failure. Logs error on failure.
 */
bool ds2782_read_voltage(I2C_HandleTypeDef *hi2c, uint16_t *raw_voltage_out);

#endif /* INC_SUBSYSTEMS_EPS_DS2782_H_ */
