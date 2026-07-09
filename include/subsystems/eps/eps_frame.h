/**
 * @file eps_frame.h
 * @brief EPS ground-telemetry frame: the wire/storage layout contract.
 *
 * This is the EPS-to-ground interface definition (ICD as code). The frame is
 * stored by the OBDH task as an opaque byte blob and decoded on the ground,
 * so its layout must stay bit-for-bit stable; any change here is a change of
 * the ground decode tables. Values are downlinked RAW (fuel-gauge native
 * scale factors, see Battery_Telemetry_t): conversion happens on the ground,
 * keeping the on-board math integer-only and the frame at 19 bytes.
 *
 * Neither the EPS task logic nor the OBDH task interprets these bytes; the
 * EPS task only decides WHEN to emit a frame (sampling period or significant
 * change) and fills it via eps_frame_pack().
 */

#ifndef INC_SUBSYSTEMS_EPS_EPS_FRAME_H_
#define INC_SUBSYSTEMS_EPS_EPS_FRAME_H_

#include <stdint.h>
#include "eps_hw.h"   /* Battery_Telemetry_t, EPS_Status_t */

// Force the compiler to pack this struct with ZERO empty padding bytes
#pragma pack(push, 1)

typedef struct {
    // 1. Digital Battery Sensor Data (DS2782) - 16 Bytes
    uint16_t vbat_raw;
    int16_t  temp_raw;
    uint8_t  rel_cap_raw;      // RARC (active relative capacity, %)
    int16_t  current_raw;      // instantaneous current (signed)
    int16_t  avg_current_raw;  // IAVG (28 s averaged current, signed)
    int16_t  acr_raw;          // ACR (accumulated charge, signed)
    uint16_t aac_raw;          // RAAC (active absolute capacity, mAh)
    uint16_t sac_raw;          // RSAC (standby absolute capacity, mAh)
    uint8_t  rsrc_raw;         // RSRC (standby relative capacity, %)

    // 2. Analog Power Manager Data (LTC4040 ADC) - 2 Bytes
    uint16_t clprog_adc;

    // 3. Compressed Status Word - 1 Byte
    // bit 0 is_charging (!CHRG)      bit 4 auto_heater_enabled
    // bit 1 has_fault   (!FAULT)     bit 5 heater_state
    // bit 2 is_eclipse  (!PFO)       bit 6 battery_read_failure
    // bit 3 charging_disabled        bit 7 pmic_read_failure
    uint8_t  system_status;

} EPS_Telemetry_Frame_t;

// Restore normal compiler padding for the rest of the code
#pragma pack(pop)

/**
 * @brief Pack a battery snapshot and status into the 19-byte frame.
 *
 * Pure function: no hardware access, no task state. NULL-safe (returns
 * without writing if any argument is NULL).
 */
void eps_frame_pack(const Battery_Telemetry_t *batt, const EPS_Status_t *pmic,
                    EPS_Telemetry_Frame_t *frame_out);

#endif /* INC_SUBSYSTEMS_EPS_EPS_FRAME_H_ */
