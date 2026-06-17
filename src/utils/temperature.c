/**
 * @file temperature.c
 * @brief Internal MCU temperature sensor reading using ADC1.
 */

#include "temperature.h"
#include "periph.h"
#include "stm32l4xx_hal_adc.h"
#include "stm32l4xx_ll_adc.h"

/* adc_read_channel() is now provided by periph.c via periph.h */

int8_t mcu_get_temperature(void)
{
    uint32_t vrefint_raw, ts_raw;

    /* Read internal voltage reference to determine actual Vdda */
    if (adc_read_channel(ADC_CHANNEL_VREFINT, &vrefint_raw) != HAL_OK)
    {
        return INT8_MIN;
    }

    /* Read temperature sensor */
    if (adc_read_channel(ADC_CHANNEL_TEMPSENSOR, &ts_raw) != HAL_OK)
    {
        return INT8_MIN;
    }

    /* Compensate TS_DATA for actual Vdda vs calibration Vref (3.0V).
     * TS_DATA_AT_3V = TS_DATA * VREFINT_CAL / VREFINT_DATA */
    int32_t ts_compensated = ((int32_t)ts_raw * (int32_t)*VREFINT_CAL_ADDR) / (int32_t)vrefint_raw;

    int32_t cal1 = (int32_t)*TEMPSENSOR_CAL1_ADDR;
    int32_t cal2 = (int32_t)*TEMPSENSOR_CAL2_ADDR;

    /* Temperature formula from RM0351:
     * T = 30 + (TS_DATA_AT_3V - TS_CAL1) * (110 - 30) / (TS_CAL2 - TS_CAL1) */
    int32_t temperature = TEMPSENSOR_CAL1_TEMP
        + ((ts_compensated - cal1) * (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP))
          / (cal2 - cal1);

    if (temperature > INT8_MAX) temperature = INT8_MAX;
    if (temperature < INT8_MIN) temperature = INT8_MIN;

    return (int8_t)temperature;
}

uint16_t mcu_get_vdda_mv(void)
{
    uint32_t vrefint_raw;

    if (adc_read_channel(ADC_CHANNEL_VREFINT, &vrefint_raw) != HAL_OK)
    {
        return 0;
    }

    /* Vdda = VREFINT_CAL_VREF * VREFINT_CAL / VREFINT_DATA */
    return (uint16_t)((VREFINT_CAL_VREF * (uint32_t)*VREFINT_CAL_ADDR) / vrefint_raw);
}
