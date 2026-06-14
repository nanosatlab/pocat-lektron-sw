/**
 * @file eps_mock.c
 * @brief Injectable mock sensor data for on-target EPS testing.
 *
 * Compiled only by the EPS_TESTS build target (./build.sh --tests).
 * Flight builds never link this file — all mock state is absent from flash.
 */

#include "eps.h"
#include <stdbool.h>

static bool g_mock_mode_enabled = false;

static Battery_Telemetry_t mock_battery = {
    .raw_voltage      = 782,
    // .raw_current   = -1442,  // enable when hardware team selects current register
    .raw_temperature  = 196,
    .raw_relative_cap = 78,
    // .raw_accumulated_cap = 2400,  // enable if energy accounting needed
};
static EPS_Status_t mock_pmic = {
    .is_charging = true, .has_fault = false, .is_eclipse = false,
    .charging_disabled = false, .raw_clprog_adc = 2048,
};
static bool mock_battery_fail = false;
static bool mock_pmic_fail    = false;

/* ── Internal helpers called from DS2782_Read_Hardware / LTC4040_Read_Hardware ── */

bool EPS_Mock_Mode_Active(void) { return g_mock_mode_enabled; }

bool DS2782_Read_Mock(Battery_Telemetry_t *out)
{
    if (mock_battery_fail) return false;
    *out = mock_battery;
    return true;
}

bool LTC4040_Read_Mock(EPS_Status_t *out)
{
    if (mock_pmic_fail) return false;
    *out = mock_pmic;
    return true;
}

/* ── Public injection API (declared in eps.h) ───────────────────────────────── */

void DS2782_Set_Mock_Mode(bool enable)                    { g_mock_mode_enabled = enable; }
void DS2782_Set_Mock_Values(const Battery_Telemetry_t *v) { if (v) mock_battery = *v; }
void DS2782_Set_Mock_Fail(bool fail)                      { mock_battery_fail = fail; }
void LTC4040_Set_Mock_Values(const EPS_Status_t *v)       { if (v) mock_pmic = *v; }
void LTC4040_Set_Mock_Fail(bool fail)                     { mock_pmic_fail = fail; }
