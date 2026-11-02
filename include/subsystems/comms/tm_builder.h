#pragma once
#include <stdint.h>

#define TM_HK_LIVE_LEN  25u   /* TM_HK_LIVE body bytes (§11.1) */

void tm_build_hk_live(uint8_t *out);
void tm_build_downlink_config(uint8_t *out);

/**
 * @brief Read the EPS telemetry mailbox and extract the housekeeping summary.
 *
 * Reads the 19-byte EPS frame back from the OBDH mailbox in one transaction
 * and decodes the single-byte fields the beacon and TM_HK_LIVE carry. Any
 * NULL pointer is skipped. On a mailbox read failure every output is left 0.
 *
 * @param vbat_mv     Battery voltage in millivolts, full precision
 *                    (vbat_raw * 4.88 mV/LSB). Kept full-width here so callers
 *                    can quantise to one byte today and move to a 2-byte BATT_V
 *                    field later with only a call-site change.
 * @param temp_c      Battery temperature, whole degrees C (signed, int8 on wire).
 * @param batt_i_raw  Instantaneous battery current, raw DS2782 CURRENT register
 *                    (signed, 0.10417 mA/LSB). Sent on the wire as int16 BE.
 * @param eps_flags   EPS status bitfield (system_status), verbatim.
 */
void tm_read_eps_summary(uint16_t *vbat_mv, uint8_t *temp_c,
                         int16_t *batt_i_raw, uint8_t *eps_flags);

/**
 * @brief Encode battery voltage into the 1-byte BATT_V wire field.
 *
 * Offset encoding over the single-cell Li-ion window: 10 mV/LSB from 2.000 V,
 * so code 0..255 spans 2.00..4.55 V. Values outside the window saturate at
 * 0 / 255. Ground decodes V = 2.000 + code * 0.010.
 */
uint8_t tm_encode_batt_v(uint16_t vbat_mv);
