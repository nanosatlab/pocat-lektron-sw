#include "beacon.h"
#include "beacon_period.h"
#include "frame.h"
#include "comms.h"
#include "lora_cfg.h"
#include "notifications.h"
#include "obc.h"
#include "time.h"
#include "temperature.h"
#include "state_machine.h"
#include "uptime.h"
#include "ht_store.h"
#include "health.h"
#include "task_management.h"
#include "types.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

_Static_assert(HT_BEACON_SIZE == 19u, "the telemetry history stores the 19-byte beacon body built below");

uint8_t  g_last_tc_id      = 0xFFu;
uint8_t  g_last_tc_rc      = 0x00u;

static uint8_t build_beacon_body(uint8_t *body)
{
    memset(body, 0, 19);

    uint32_t epoch = time_get_unix();
    uint8_t obc_state = (uint8_t)state_machine_current_state();
    uint32_t uptime_s = uptime_get_s();         /* §6.6 UPTIME_S: seconds since boot, from the tick counter */

    body[0]  = AIR_BODY_VER;
    body[1]  = (uint8_t)(epoch >> 24);
    body[2]  = (uint8_t)(epoch >> 16);
    body[3]  = (uint8_t)(epoch >> 8);
    body[4]  = (uint8_t)(epoch);
    body[5]  = obc_state;
    body[6]  = 0x3Fu;                           /* HEALTH_FLAGS placeholder */
    body[7]  = (uint8_t)mcu_get_temperature();
    body[8]  = 0x00u;                           /* TEMP_BATT dummy */
    body[9]  = (uint8_t)(mcu_get_vdda_mv() / 100u);
    body[10] = 0x00u;                           /* BATT_I dummy */
    body[11] = 0x00u;                           /* DEPLOY */
    body[12] = g_last_tc_id;
    body[13] = g_last_tc_rc;
    body[14] = lora_cfg_current_id();           /* §9 active CFG_ID */
    body[15] = (uint8_t)(uptime_s >> 24);
    body[16] = (uint8_t)(uptime_s >> 16);
    body[17] = (uint8_t)(uptime_s >> 8);
    body[18] = (uint8_t)(uptime_s);

    return 19u;
}

static void transmit_beacon(void)
{
    uint8_t body[19];
    build_beacon_body(body);

    ht_beacon_post(body);   /* never waits: OBDH stores the beacon, at most one per 30 s on average */

    uint8_t air_buf[AIR_FRAME_MAX];
    uint8_t frame_len = air_encode(air_buf, AIR_BEACON, 0x00u,
                                   comms_next_seq(), body, 19u);

    TxQueueEntry_t entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.frame, air_buf, frame_len);
    entry.frame_len = frame_len;
    entry.needs_ack = 0;

    QueueHandle_t tx_q = comms_get_tx_queue();
    if (tx_q != NULL && xQueueSend(tx_q, &entry, pdMS_TO_TICKS(100)) == pdTRUE) {
        TaskHandle_t trx = tm_get_task_handle(TM_TASK_TRANSCEIVER);
        if (trx != NULL) {
            xTaskNotify(trx, N_TRANSCEIVER_TX_READY_BIT, eSetBits);
        }
    }
}

void beacon_task(void *pv_parameters)
{
    (void)pv_parameters;
    uint32_t deferred = 0u;   /* notifications that came while paused */

    beacon_period_apply();   /* the stored period, also after a restart by the health monitor */

    for (;;) {
        uint32_t notif = 0;
        xTaskNotifyWait(0, N_TASK_PAUSE | N_TASK_RESUME | N_COMMS_TRANSMIT_BEACON | N_COMMS_NEW_BEACON_PERIOD,
                        &notif, pdMS_TO_TICKS(BEACON_HEALTH_KICK_MS));

        health_kick(HEALTH_BIT_BEACON);

        if (tm_check_pause(notif, &deferred)) {
            continue;
        }
        /* A period stored while paused (SURVIVAL pauses this task) applies now;
           a beacon that fell due while paused is not sent late */
        notif |= deferred & N_COMMS_NEW_BEACON_PERIOD;
        deferred = 0u;

        if (notif & N_COMMS_NEW_BEACON_PERIOD) {
            beacon_period_apply();
        }
        if (notif & N_COMMS_TRANSMIT_BEACON) {
            transmit_beacon();
        }
    }
}
