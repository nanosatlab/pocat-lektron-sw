#include "tm_builder.h"
#include "obc.h"
#include "temperature.h"
#include "health.h"
#include "flash.h"
#include <string.h>
#include <stdint.h>

/* TM_HK_LIVE body layout per §11.1: 24 bytes */
void tm_build_hk_live(uint8_t *out)
{
    memset(out, 0, 24);

    uint8_t obc_state = 0;
    OBDH_Read_Request(CURRENT_STATE_ADDR, &obc_state, 1);

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

/* TODO Phase 4: populate with live LoRa config when lora_cfg is available */
void tm_build_downlink_config(uint8_t *out)
{
    memset(out, 0, 16);
}
