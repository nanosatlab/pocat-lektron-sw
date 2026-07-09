/**
 * @file ltc4040.c
 * @brief GPIO/ADC access to the ADI LTC4040 power manager.
 */

#include "ltc4040.h"
#include "periph.h"   /* EPS_PIN_* map, adc_read_channel(), stm32l4xx_hal.h */

bool ltc4040_read_status(EPS_Status_t *pmic_out)
{
    if (pmic_out == NULL) return false;

    pmic_out->is_charging       = (HAL_GPIO_ReadPin(EPS_PIN_CHRG_PORT,  EPS_PIN_CHRG)  == GPIO_PIN_RESET); /* !CHRG  */
    pmic_out->has_fault         = (HAL_GPIO_ReadPin(EPS_PIN_FAULT_PORT, EPS_PIN_FAULT) == GPIO_PIN_RESET); /* !FAULT */
    pmic_out->is_eclipse        = (HAL_GPIO_ReadPin(EPS_PIN_PFO_PORT,   EPS_PIN_PFO)   == GPIO_PIN_RESET); /* !PFO: low = input power lost */
    pmic_out->charging_disabled = ltc4040_charger_is_disabled();

    /* CLPROG — solar input current monitor. An ADC failure does not
     * invalidate the GPIO reads, so we continue with the sentinel. */
    uint32_t adc_val = 0;
    if (adc_read_channel(EPS_ADC_CHANNEL_CLPROG, &adc_val) == HAL_OK) {
        pmic_out->raw_clprog_adc = (uint16_t)adc_val;
    } else {
        pmic_out->raw_clprog_adc = 0xFFFF;
    }

    return true;
}

void ltc4040_charger_enable(void)
{
    HAL_GPIO_WritePin(EPS_PIN_CHRGOFF_PORT, EPS_PIN_CHRGOFF, GPIO_PIN_RESET);
}

void ltc4040_charger_disable(void)
{
    HAL_GPIO_WritePin(EPS_PIN_CHRGOFF_PORT, EPS_PIN_CHRGOFF, GPIO_PIN_SET);
}

bool ltc4040_charger_is_disabled(void)
{
    return (HAL_GPIO_ReadPin(EPS_PIN_CHRGOFF_PORT, EPS_PIN_CHRGOFF) == GPIO_PIN_SET);
}

bool ltc4040_pfo_is_asserted(void)
{
    return (HAL_GPIO_ReadPin(EPS_PIN_PFO_PORT, EPS_PIN_PFO) == GPIO_PIN_RESET);
}
