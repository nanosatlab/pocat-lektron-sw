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
 *
 * Byte offsets (little-endian, packed, total 19 bytes):
 *    0  vbat_raw        u16   4.88 mV/LSB   (mV = raw * 488 / 100)
 *    2  temp_raw        i16   0.125 C/LSB   (whole C = raw / 8)
 *    4  rel_cap_raw     u8    RARC, 1 %/LSB
 *    5  current_raw     i16   0.10417 mA/LSB (15 mOhm R9)
 *    7  avg_current_raw i16   0.10417 mA/LSB, 28 s window
 *    9  acr_raw         i16   0.4167 mAh/LSB (6.25 uVh / 15 mOhm)
 *   11  aac_raw         u16   RAAC, 1.6 mAh/LSB
 *   13  sac_raw         u16   RSAC, 1.6 mAh/LSB
 *   15  rsrc_raw        u8    RSRC, 1 %/LSB
 *   16  clprog_adc      u16   12-bit ADC counts (0..4095), 0xFFFF = read failed
 *   18  system_status   u8    status bitfield (see below)
 *
 * The comms housekeeping builders (beacon, TM_HK_LIVE) read this blob back
 * from the OBDH mailbox and decode offsets 0 (BATT_V), 2 (TEMP_BATT), 5
 * (BATT_I) and 18 (EPS flags) by hand, without including this header.
 * eps_frame.c pins those offsets and the 19-byte size with _Static_assert so
 * any layout change breaks the build here rather than silently corrupting the
 * downlink.
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
    uint16_t aac_raw;          // RAAC (active absolute capacity, 1.6 mAh/LSB)
    uint16_t sac_raw;          // RSAC (standby absolute capacity, 1.6 mAh/LSB)
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
