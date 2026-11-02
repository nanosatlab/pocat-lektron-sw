/**
 * @file eps_frame.c
 * @brief Packing of the 19-byte EPS ground-telemetry frame.
 */

#include "eps_frame.h"
#include <stddef.h>

/* The comms housekeeping builders decode this blob by raw byte offset without
 * including eps_frame.h. Pin the offsets and size they depend on so a layout
 * change breaks the build here, at the source of truth, instead of silently
 * shifting TEMP_BATT / BATT_I / EPS-flags on the downlink. */
_Static_assert(sizeof(EPS_Telemetry_Frame_t) == 19,
               "EPS ground frame must stay 19 bytes (ground decode tables)");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, vbat_raw) == 0,
               "BATT_V decode offset drifted (comms tm_builder.c)");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, temp_raw) == 2,
               "TEMP_BATT decode offset drifted (comms tm_builder.c)");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, current_raw) == 5,
               "BATT_I decode offset drifted (comms tm_builder.c)");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, system_status) == 18,
               "EPS-flags decode offset drifted (comms tm_builder.c)");

void eps_frame_pack(const Battery_Telemetry_t *batt, const EPS_Status_t *pmic,
                    EPS_Telemetry_Frame_t *frame_out)
{
    // Safety check: Ensure pointers are not null before accessing memory
    if (batt == NULL || pmic == NULL || frame_out == NULL) {
        return;
    }

    // --- raw I2C Battery Data ---
    frame_out->vbat_raw        = batt->raw_voltage;
    frame_out->temp_raw        = batt->raw_temperature;
    frame_out->rel_cap_raw     = batt->raw_relative_cap;
    frame_out->current_raw     = batt->raw_current;
    frame_out->avg_current_raw = batt->raw_avg_current;
    frame_out->acr_raw         = batt->raw_acr;
    frame_out->aac_raw         = batt->raw_active_abs_cap;
    frame_out->sac_raw         = batt->raw_standby_abs_cap;
    frame_out->rsrc_raw        = batt->raw_standby_rel_cap;

    // --- Analog PMIC Data ---
    frame_out->clprog_adc = pmic->raw_clprog_adc;

    // --- Boolean Logic ---
    // (0000 0000)
    frame_out->system_status = 0x00;
    if (pmic->is_charging) {
        frame_out->system_status |= (1 << 0);
    }
    if (pmic->has_fault) {
        frame_out->system_status |= (1 << 1);
    }
    if (pmic->is_eclipse) {  // bit2: !PFO power-fail (input power lost; commonly eclipse)
        frame_out->system_status |= (1 << 2);
    }
    if (pmic->charging_disabled) {
        frame_out->system_status |= (1 << 3);
    }
    if (pmic->auto_heater_enabled) {
        frame_out->system_status |= (1 << 4);
    }
    if (pmic->heater_state) {
        frame_out->system_status |= (1 << 5);
    }
    if (pmic->battery_read_failure) {
        frame_out->system_status |= (1 << 6);
    }
    if (pmic->pmic_read_failure) {
        frame_out->system_status |= (1 << 7);
    }
}
