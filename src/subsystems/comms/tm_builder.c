#include "tm_builder.h"
#include "lora_cfg.h"
#include "obc.h"
#include "temperature.h"
#include "health.h"
#include "flash.h"
#include <string.h>
#include <stdint.h>

/* EPS telemetry mailbox layout — raw byte offsets into the 19-byte
 * EPS_Telemetry_Frame_t (little-endian, packed). Duplicated here on purpose so
 * comms stays free of EPS headers; eps_frame.c guards these offsets and the
 * length with _Static_assert, so a layout change breaks that build. */
#define EPS_BLOB_LEN            19u
#define EPS_OFF_VBAT             0u   /* uint16, 4.88 mV/LSB -> mV = raw*488/100 */
#define EPS_OFF_TEMP_RAW         2u   /* int16, 0.125 C/LSB  -> whole C = raw/8  */
#define EPS_OFF_CURRENT          5u   /* int16, instantaneous current (both bytes) */
#define EPS_OFF_AVG_CURRENT      7u   /* int16, 28 s averaged current              */
#define EPS_OFF_SYSTEM_STATUS   18u   /* status bitfield, verbatim               */

void tm_read_eps_summary(uint16_t *vbat_mv, uint8_t *temp_c,
                         int16_t *batt_i_raw, uint8_t *eps_flags)
{
    if (vbat_mv  != NULL) *vbat_mv  = 0u;
    if (temp_c   != NULL) *temp_c   = 0u;
    if (batt_i_raw != NULL) *batt_i_raw = 0;
    if (eps_flags != NULL) *eps_flags = 0u;

    uint8_t blob[EPS_BLOB_LEN];
    if (OBDH_Read_Request(OBDH_EPS_TELEMETRY_ADDR, blob, sizeof(blob)) != HAL_OK) {
        return;   /* mailbox unreadable: keep the zeroed placeholders */
    }

    /* BATT_V: full millivolts. raw is 4.88 mV/LSB (matches
     * DS2782_Compute_Voltage = raw*488/100), reconstructed little-endian.
     * The caller decides how many bytes of this to put on the wire. */
    uint16_t vbat_raw = (uint16_t)((uint16_t)blob[EPS_OFF_VBAT] |
                                   ((uint16_t)blob[EPS_OFF_VBAT + 1u] << 8));
    if (vbat_mv != NULL) *vbat_mv = (uint16_t)(((uint32_t)vbat_raw * 488u) / 100u);

    /* TEMP_BATT: whole degrees C. raw is 0.125 C/LSB (matches
     * DS2782_Compute_Temperature = raw/8), reconstructed little-endian. */
    int16_t temp_raw = (int16_t)((uint16_t)blob[EPS_OFF_TEMP_RAW] |
                                 ((uint16_t)blob[EPS_OFF_TEMP_RAW + 1u] << 8));
    if (temp_c != NULL) *temp_c = (uint8_t)(int8_t)(temp_raw / 8);

    /* BATT_I: full signed 16-bit instantaneous current, reconstructed
     * little-endian. Sent verbatim on the wire as int16 big-endian (beacon
     * and TM_HK_LIVE); the ground scales it by 1.5625 uV / Rsns per LSB
     * (0.10417 mA/LSB at the 15 mOhm R9). */
    if (batt_i_raw != NULL) {
        *batt_i_raw = (int16_t)((uint16_t)blob[EPS_OFF_CURRENT] |
                                ((uint16_t)blob[EPS_OFF_CURRENT + 1u] << 8));
    }

    if (eps_flags != NULL) *eps_flags = blob[EPS_OFF_SYSTEM_STATUS];
}

#define BATT_V_OFFSET_MV   2000u  /* code 0 = 2.000 V              */
#define BATT_V_LSB_MV        10u  /* 10 mV/LSB -> 2.00..4.55 V      */

uint8_t tm_encode_batt_v(uint16_t vbat_mv)
{
    if (vbat_mv <= BATT_V_OFFSET_MV) return 0u;
    /* Round to nearest instead of truncating. */
    uint32_t code = ((uint32_t)(vbat_mv - BATT_V_OFFSET_MV) + BATT_V_LSB_MV / 2u)
                    / BATT_V_LSB_MV;
    return (code > 255u) ? 255u : (uint8_t)code;
}

/* TM_HK_LIVE body layout per §11.1: 25 bytes (BATT_I is 2 bytes) */
void tm_build_hk_live(uint8_t *out)
{
    memset(out, 0, TM_HK_LIVE_LEN);

    uint8_t obc_state = 0;
    OBDH_Read_Request(CURRENT_STATE_ADDR, &obc_state, 1);

    uint16_t batt_mv = 0;
    uint8_t batt_temp = 0, eps_flags = 0;
    int16_t batt_i_raw = 0;
    tm_read_eps_summary(&batt_mv, &batt_temp, &batt_i_raw, &eps_flags);

    out[0]  = obc_state;
    out[1]  = 0x3Fu;                          /* HEALTH_FLAGS placeholder */
    out[2]  = (uint8_t)mcu_get_temperature();
    out[3]  = batt_temp;                       /* TEMP_BATT (whole C, signed) */
    out[4]  = 0x00u;                          /* TEMP_PAYLOAD dummy */
    out[5]  = 0x00u;                          /* TEMP_COMMS dummy */
    out[6]  = tm_encode_batt_v(batt_mv);       /* BATT_V: 2.000 V + code*10 mV */
    out[7]  = (uint8_t)((uint16_t)batt_i_raw >> 8); /* BATT_I: raw int16 BE */
    out[8]  = (uint8_t)((uint16_t)batt_i_raw);
    /* out[9..18] solar voltages + EPS/OBDH/COMMS/PAYLOAD flags. The EPS flag
     * byte (system_status, in eps_flags) still needs its §11.1 offset assigned
     * before it can be placed here; left 0 until the spec pins it. */
    /* out[19..24] TC_COUNT, NACK_COUNT, RESET_COUNT — all 0 for now */
    (void)eps_flags;
}

/* TM_DOWNLINK_CONFIG body layout (§11, reply to TC 0x0E):
 *   [0]     BODY_VER = 0x01
 *   [1]     CFG_ID
 *   [2..5]  FREQUENCY_HZ (BE)
 *   [6]     SF
 *   [7]     BW
 *   [8]     CR
 *   [9]     TX_POWER_DBM (int8)
 *   [10..11] PREAMBLE_SYMBOLS (BE)
 *   [12]    CRC_ON
 *   [13]    IQ_INVERTED
 *   [14]    SYNC_WORD
 *   [15]    reserved (0)
 */
void tm_build_downlink_config(uint8_t *out)
{
    LoraConfig_t c;
    lora_cfg_active_snapshot(&c);

    out[0]  = 0x01u;
    out[1]  = c.cfg_id;
    out[2]  = (uint8_t)(c.frequency_hz >> 24);
    out[3]  = (uint8_t)(c.frequency_hz >> 16);
    out[4]  = (uint8_t)(c.frequency_hz >> 8);
    out[5]  = (uint8_t)(c.frequency_hz);
    out[6]  = c.sf;
    out[7]  = c.bw;
    out[8]  = c.cr;
    out[9]  = (uint8_t)c.tx_power_dbm;
    out[10] = (uint8_t)(c.preamble_symbols >> 8);
    out[11] = (uint8_t)(c.preamble_symbols);
    out[12] = c.crc_on;
    out[13] = c.iq_inverted;
    out[14] = c.sync_word;
    out[15] = 0u;
}
