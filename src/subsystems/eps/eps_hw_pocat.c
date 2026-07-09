/**
 * @file eps_hw_pocat.c
 * @brief POCAT-Lektron binding of the EPS hardware interface (eps_hw.h).
 *
 * Battery telemetry comes from the DS2782 fuel gauge on I2C1 (ds2782.c),
 * PMIC status from the LTC4040 GPIO/ADC lines (ltc4040.c), and the heater is
 * a direct GPIO (pin map in periph.h). This file configures NO hardware:
 * pins and buses are set up centrally by periph_init_for_freq() at boot,
 * before any task runs.
 *
 * In the UNIT_TEST build the two sensor reads dispatch at runtime to the
 * injectable mocks (eps_mock.c) when mock mode is active; the actuator
 * functions always drive the real pins so on-target tests can assert the
 * actual GPIO state.
 */

#include "eps_hw.h"
#include "ds2782.h"
#include "ltc4040.h"
#include "periph.h"
#include <stdio.h>
#include <stddef.h>

#ifdef UNIT_TEST
/* Provided by eps_mock.c (linked only into the test image). */
bool EPS_Mock_Mode_Active(void);
bool DS2782_Read_Mock(Battery_Telemetry_t *out);
bool LTC4040_Read_Mock(EPS_Status_t *out);
#endif

void eps_hw_probe(void)
{
    /* Confirm the DS2782 is on the bus at startup. Non-fatal — if the probe
     * fails the EPS task still runs and reports battery_read_failure each
     * cycle, but the boot log makes the wiring/address issue obvious. */
    (void)ds2782_probe(&hi2c1);

    /* Standalone voltage read as a minimum-viable sanity test, independent of
     * the burst read. At a healthy supply you should see mV ~= measured VIN. */
    uint16_t v_raw;
    if (ds2782_read_voltage(&hi2c1, &v_raw)) {
        uint16_t v_mv = (uint16_t)(((uint32_t)v_raw * 488u) / 100u);
        printf("[DS2782] voltage test: raw=%u mV=%u\r\n",
               (unsigned)v_raw, (unsigned)v_mv);
    }
}

bool eps_hw_read_battery(Battery_Telemetry_t *telemetry_out)
{
    if (telemetry_out == NULL) return false;
#ifdef UNIT_TEST
    if (EPS_Mock_Mode_Active()) return DS2782_Read_Mock(telemetry_out);
#endif
    return ds2782_read_telemetry(&hi2c1, telemetry_out);
}

bool eps_hw_read_pmic(EPS_Status_t *pmic_out)
{
    if (pmic_out == NULL) return false;
#ifdef UNIT_TEST
    if (EPS_Mock_Mode_Active()) return LTC4040_Read_Mock(pmic_out);
#endif
    return ltc4040_read_status(pmic_out);
}

void eps_hw_charger_set(bool enable)
{
    if (enable) ltc4040_charger_enable();
    else        ltc4040_charger_disable();
}

void eps_hw_heater_set(bool on)
{
    HAL_GPIO_WritePin(EPS_PIN_HEATER_PORT, EPS_PIN_HEATER,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool eps_hw_heater_get(void)
{
    return (HAL_GPIO_ReadPin(EPS_PIN_HEATER_PORT, EPS_PIN_HEATER) == GPIO_PIN_SET);
}

bool eps_hw_pfo_is_asserted(void)
{
    return ltc4040_pfo_is_asserted();
}

uint16_t eps_hw_battery_voltage_mv(const Battery_Telemetry_t *telemetry)
{
    return DS2782_Compute_Voltage(telemetry);
}

int16_t eps_hw_battery_current_ma(const Battery_Telemetry_t *telemetry)
{
    return DS2782_Compute_Current(telemetry);
}

int16_t eps_hw_battery_temp_c(const Battery_Telemetry_t *telemetry)
{
    return DS2782_Compute_Temperature(telemetry);
}
