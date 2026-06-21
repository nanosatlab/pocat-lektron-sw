/**
 * @file ds2782.c
 * @brief Low-level driver for the Maxim DS2782 stand-alone fuel gauge.
 */

#include "ds2782.h"
#include <stdio.h>

#define DS2782_I2C_TIMEOUT_MS   100u

bool ds2782_burst_read(I2C_HandleTypeDef *hi2c, uint8_t out[DS2782_BURST_LEN])
{
    if (hi2c == NULL || out == NULL) {
        return false;
    }

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        hi2c,
        DS2782_I2C_ADDR_8BIT,
        DS2782_REG_RARC,
        I2C_MEMADD_SIZE_8BIT,
        out,
        DS2782_BURST_LEN,
        DS2782_I2C_TIMEOUT_MS);

    if (status != HAL_OK) {
        printf("[DS2782] burst read fail: status=%d err=0x%lx\r\n",
               (int)status, (unsigned long)hi2c->ErrorCode);
        return false;
    }

    return true;
}

bool ds2782_read_voltage(I2C_HandleTypeDef *hi2c, uint16_t *raw_voltage_out)
{
    if (hi2c == NULL || raw_voltage_out == NULL) {
        return false;
    }

    uint8_t buf[2];
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        hi2c,
        DS2782_I2C_ADDR_8BIT,
        DS2782_REG_VOLT_M,
        I2C_MEMADD_SIZE_8BIT,
        buf,
        2,
        DS2782_I2C_TIMEOUT_MS);

    if (status != HAL_OK) {
        printf("[DS2782] voltage read fail: status=%d err=0x%lx\r\n",
               (int)status, (unsigned long)hi2c->ErrorCode);
        return false;
    }

    *raw_voltage_out = (uint16_t)((((uint16_t)buf[0] << 8) | buf[1]) >> 5);
    return true;
}

bool ds2782_probe(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL) {
        return false;
    }

    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(
        hi2c, DS2782_I2C_ADDR_8BIT, 3, DS2782_I2C_TIMEOUT_MS);

    printf("[DS2782] probe @0x%02X: %s (status=%d, err=0x%lx)\r\n",
           (unsigned)DS2782_I2C_ADDR_8BIT,
           (status == HAL_OK) ? "ACK" : "NO ACK",
           (int)status,
           (unsigned long)hi2c->ErrorCode);

    /* If the expected address NACKs, scan the bus so the failure isn't
     * ambiguous. Any address that ACKs is printed in 8-bit (HAL-shifted) form
     * so the value can be compared directly against DS2782_I2C_ADDR_8BIT. */
    if (status != HAL_OK) {
        printf("[DS2782] bus scan (8-bit addrs):");
        bool any = false;
        for (uint8_t a = 1u; a < 128u; a++) {
            uint16_t dev = (uint16_t)(a << 1);
            if (HAL_I2C_IsDeviceReady(hi2c, dev, 1, 5u) == HAL_OK) {
                printf(" 0x%02X", (unsigned)dev);
                any = true;
            }
        }
        printf("%s\r\n", any ? "" : " (no devices responding)");
    }

    return (status == HAL_OK);
}
