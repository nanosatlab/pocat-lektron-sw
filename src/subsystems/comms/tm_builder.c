#include "tm_builder.h"
#include "lora_cfg.h"
#include "obc.h"
#include "temperature.h"
#include "health.h"
#include "flash.h"
#include "obdh_requests.h"
#include <string.h>
#include <stdint.h>

/* TM_HK_LIVE body layout per §11.1: 24 bytes */
void tm_build_hk_live(uint8_t *out)
{
    memset(out, 0, 24);

    uint8_t obc_state = 0;
    obdh_read_request(CURRENT_STATE_ADDR, &obc_state, 1);

    out[0]  = obc_state;
    out[1]  = 0x3Fu;                          /* HEALTH_FLAGS placeholder */
    out[2]  = (uint8_t)mcu_get_temperature();
    out[3]  = 0x00u;                          /* TEMP_BATT dummy */
    out[4]  = 0x00u;                          /* TEMP_PAYLOAD dummy */
    out[5]  = 0x00u;                          /* TEMP_COMMS dummy */
    out[6]  = (uint8_t)(mcu_get_vdda_mv() / 100u);
    out[7]  = 0x00u;                          /* BATT_I dummy */
    /* out[8..17] solar voltages, EPS/OBDH/COMMS/PAYLOAD flags — all 0 */
    /* out[18..23] TC_COUNT, NACK_COUNT, RESET_COUNT — all 0 for now */
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
