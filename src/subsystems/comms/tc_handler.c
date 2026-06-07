/**
 * @file tc_handler.c
 * @brief Telecommand dispatch and processing.
 *
 * Each case in the switch corresponds to a telecommand defined in
 * tc_handler.h. Processing that was already implemented in the reference
 * codebase (reference/COMMS/comms.c) is included as comments for
 * reference during reimplementation.
 */

/* ---- Includes ---- */

#include "FreeRTOS.h"
#include "task.h"
#include "tc_handler.h"
#include "notifications.h"
#include "task_management.h"
#include "main.h"
#include "time.h"
#include "flash.h"

/* ---- Private helpers ---- */

/**
 * @brief Send a notification to a target task (NULL-safe).
 */
static inline void notify(TaskHandle_t handle, uint32_t bits)
{
    if (handle != NULL) {
        xTaskNotify(handle, bits, eSetBits);
    }
}

/* ---- Public functions ---- */

void tc_process(const uint8_t *rx_data)
{
    tc_id_t tc_id = (tc_id_t)rx_data[2];

    switch (tc_id) {

    /* ── S/C Ping ───────────────────────────────────────────────────────── */

    case TC_PING:
        break;

    /* ── Mode Transits ──────────────────────────────────────────────────── */

    case TC_TRANSIT_TO_NM:

        notify(main_get_obc_handle(), N_OBC_EXIT_STATE_TO_NOMINAL);
        break;

    case TC_TRANSIT_TO_CM:

        notify(main_get_obc_handle(), N_OBC_EXIT_STATE_TO_CONTINGENCY);
        break;

    case TC_TRANSIT_TO_SSM:

        notify(main_get_obc_handle(), N_OBC_EXIT_STATE_TO_SUNSAFE);
        break;

    case TC_TRANSIT_TO_SM:

        notify(main_get_obc_handle(), N_OBC_EXIT_STATE_TO_SURVIVAL);
        break;

    /* ── SS Configuration ───────────────────────────────────────────────── */

    case TC_UPLOAD_ADCS_CAL:

        /* Reference processing (multi-packet upload):
         * if (ADCS_counter == 1 && tlc_data[2]==86) {
         *     Send_to_WFQueue(&tlc_data[3], CALIBRATION_PACKET_SIZE,
         *                     MAGNETO_MATRIX_ADDR, COMMSsender);
         *     ADCS_counter++;
         *     Wait_ACK_Flag = 1;
         * } else if (ADCS_counter == 2 && tlc_data[2]==164) {
         *     Send_to_WFQueue(&tlc_data[3], 3, MAGNETO_MATRIX_ADDR + 3, COMMSsender);
         *     Send_to_WFQueue(&tlc_data[6], 12, MAGNETO_OFFSET_ADDR, COMMSsender);
         *     Send_to_WFQueue(&tlc_data[18], CALIBRATION_PACKET_SIZE-3-12,
         *                     GYRO_POLYN_ADDR, COMMSsender);
         *     ADCS_counter++;
         *     Wait_ACK_Flag = 1;
         * } else if (ADCS_counter == 3 && tlc_data[2]==255) {
         *     Send_to_WFQueue(&tlc_data[3], 6, GYRO_POLYN_ADDR + 6, COMMSsender);
         *     Send_to_WFQueue(&tlc_data[9], CALIBRATION_PACKET_SIZE-6-3,
         *                     PHOTODIODES_OFFSET_ADDR, COMMSsender);
         *     ADCS_counter = 1;
         * }
         */
        // TODO: save calibration in OBDH (ADCS_CONFIG_ADDR)
        // TODO: notify ADCS task once it exists
        break;

    case TC_UPLOAD_ADCS_TLE:

        /* Reference processing (multi-packet upload):
         * if ((TLE_counter==1 && tlc_data[2]==86) ||
         *     (TLE_counter==2 && tlc_data[2]==164)) {
         *     Send_to_WFQueue(&tlc_data[3], TLE_PACKET_SIZE,
         *                     TLE_ADDR1 + (TLE_counter-1)*TLE_PACKET_SIZE,
         *                     COMMSsender);
         *     TLE_counter++;
         *     Wait_ACK_Flag = 1;
         * } else if (TLE_counter==3 && tlc_data[2]==255) {
         *     Send_to_WFQueue(&tlc_data[3], 1,
         *                     TLE_ADDR1 + 2*TLE_PACKET_SIZE, COMMSsender);
         *     Send_to_WFQueue(&tlc_data[4], TLE_PACKET_SIZE-1,
         *                     TLE_ADDR2, COMMSsender);
         *     TLE_counter = 1;
         * }
         */
        // TODO: save TLE in OBDH
        // TODO: notify ADCS task once it exists
        break;

    case TC_UPLOAD_COMMS_CONFIG:

        /* Reference processing:
         * Radio.Standby();
         * SX1262TLCConfig(RxData);  // reconfigures SF, CR, RF_F
         * COMMS_State = TX;
         * Beacon_Flag = 1;
         */
        // TODO: save config in OBDH (COMMS_CONFIG_ADDR)
        notify(tm_get_task_handle(TM_TASK_COMMS), N_COMMS_NEW_CONFIG);
        break;

    case TC_UPLOAD_COMMS_PARAMS:

        /* Reference processing:
         * Radio.Standby();
         * COMMSTLCConfig(RxData);  // updates rxTime, sleepTime, CAD mode, window
         * COMMS_State = TX;
         * Beacon_Flag = 1;
         */
        // TODO: save config in OBDH (COMMS_CONFIG_ADDR)
        notify(tm_get_task_handle(TM_TASK_COMMS), N_COMMS_NEW_PARAMS);
        break;

    case TC_UPLOAD_UNIX_TIME:

        time_set_unix((rx_data[3] << 24) | (rx_data[4] << 16) | (rx_data[5] << 8) | rx_data[6]);
        // OBC task is notified that the time has been updated, in case it needs to trigger time-dependent actions
        notify(main_get_obc_handle(), N_OBC_UPDATE_TIME);  
        break;

    case TC_UPLOAD_EPS_TH:

        OBDH_Write_Request(EPS_THRESHOLDS_ADDR, &rx_data[3], 3); // write all 3 thresholds at once
        notify(tm_get_task_handle(TM_TASK_EPS), N_EPS_NEW_THRESHOLDS);
        break;

    case TC_UPLOAD_PL_CONFIG:

        /* Reference processing:
         * Send_to_WFQueue((uint8_t*) tlc_data[3], 8,
         *                 RFI_CONFIG_ADDR, COMMSsender);
         */
        // TODO: save configuration in OBDH
        break;

    case TC_DOWNLINK_CONFIG:
        /* Reference processing:
         * plsize = 19;
         * GoTX_Flag = 1;
         * TxConfig_Data_Flag = 1;
         */
        // TODO: read config data from OBDH, enqueue downlink config telemetry
        break;

    /* ── EPS Heater ─────────────────────────────────────────────────────── */

    case TC_EPS_HEATER_ENABLE:

        notify(tm_get_task_handle(TM_TASK_EPS), N_EPS_ENABLE_AUTO_HEAT);
        break;

    case TC_EPS_HEATER_DISABLE:

        notify(tm_get_task_handle(TM_TASK_EPS), N_EPS_DISABLE_AUTO_HEAT);
        break;

    /* ── PoL up/down ────────────────────────────────────────────────────── */

    case TC_POL_PAYLOAD_SHUT:
        // TODO: TBD — PoL control
        break;

    case TC_POL_ADCS_SHUT:
        // TODO: TBD — PoL control
        break;

    case TC_POL_BURNCOMMS_SHUT:
        // TODO: TBD — PoL control
        break;

    case TC_POL_HEATER_SHUT:
        // TODO: TBD — PoL control
        break;

    case TC_POL_PAYLOAD_ENABLE:
        // TODO: TBD — PoL control
        break;

    case TC_POL_ADCS_ENABLE:
        // TODO: TBD — PoL control
        break;

    case TC_POL_BURNCOMMS_ENABLE:
        // TODO: TBD — PoL control
        break;

    case TC_POL_HEATER_ENABLE:
        // TODO: TBD — PoL control
        break;

    /* ── Flash Memory ───────────────────────────────────────────────────── */

    case TC_CLEAR_PL_DATA:

        notify(tm_get_task_handle(TM_TASK_OBDH), N_OBDH_CLEAR_PAYLOAD);
        break;

    case TC_CLEAR_FLASH:

        notify(tm_get_task_handle(TM_TASK_OBDH), N_OBDH_CLEAR_FLASH);
        break;

    case TC_CLEAR_HT:

        notify(tm_get_task_handle(TM_TASK_OBDH), N_OBDH_CLEAR_HT);
        break;

    /* ── COMMS ──────────────────────────────────────────────────────────── */

    case TC_COMMS_STOP_TX:

        /* Reference processing:
         * xTimerStop(xTimerBeacon, 0);
         * TXStopped_Flag = 1;
         */
        notify(tm_get_task_handle(TM_TASK_COMMS), N_COMMS_STOP_RF);
        break;

    case TC_COMMS_RESUME_TX:

        /* Reference processing:
         * xTimerStart(xTimerBeacon, 0);
         * TXStopped_Flag = 0;
         */
        notify(tm_get_task_handle(TM_TASK_COMMS), N_COMMS_RESUME_RF);
        break;

    case TC_COMMS_IT_DOWNLINK:
        /* Reference processing:
         * Beacon_Flag = 1;
         * GoTX_Flag = 1;
         */
        // TODO: enqueue beacon for transmission
        break;

    case TC_COMMS_HT_DOWNLINK:

        // TODO: enqueue historic telemetry from OBDH
        break;

    /* ── Payload ────────────────────────────────────────────────────────── */

    case TC_PAYLOAD_SCHEDULE:
        /* Reference processing:
         * Send_to_WFQueue(&tlc_data[3], 4, PL_TIME_ADDR, COMMSsender);
         * Send_to_WFQueue(&tlc_data[7], 1, PHOTO_RESOL_ADDR, COMMSsender);
         * Send_to_WFQueue(&tlc_data[8], 1, PHOTO_COMPRESSION_ADDR, COMMSsender);
         * xTaskNotify(OBC_Handle, TAKEPHOTO_NOTI, eSetBits);
         * Send_to_WFQueue(&tlc_data[9], 8, PL_RF_TIME_ADDR, COMMSsender);
         * Send_to_WFQueue(&tlc_data[17], 1, F_MIN_ADDR, COMMSsender);
         * Send_to_WFQueue(&tlc_data[18], 1, F_MAX_ADDR, COMMSsender);
         * Send_to_WFQueue(&tlc_data[19], 1, DELTA_F_ADDR, COMMSsender);
         * Send_to_WFQueue(&tlc_data[20], 1, INTEGRATION_TIME_ADDR, COMMSsender);
         */
        // TODO: TBD — save config to OBDH, then activate
        notify(tm_get_task_handle(TM_TASK_PAYLOAD), N_PAYLOAD_ACTIVATE);
        break;

    case TC_PAYLOAD_DEACTIVATE:

        /* Reference processing:
         * Beacon_Flag = 1;
         * GoTX_Flag = 1;
         */
        notify(tm_get_task_handle(TM_TASK_PAYLOAD), N_PAYLOAD_DEACTIVATE);
        break;

    case TC_PAYLOAD_SEND_DATA:

        /* Reference processing:
         * plsize = 40;
         * packetwindow = 5;
         * GoTX_Flag = 1;
         * Tx_PL_Data_Flag = 1;
         */
        // TODO: enqueue measurement from OBDH
        break;

    /* ── OBC ────────────────────────────────────────────────────────────── */

    case TC_OBC_HARD_REBOOT:

        notify(main_get_obc_handle(), N_OBC_HARD_REBOOT);
        break;

    case TC_OBC_SOFT_REBOOT:

        /* Reference processing:
         * HAL_NVIC_SystemReset();
         */
        notify(main_get_obc_handle(), N_OBC_SOFT_REBOOT);
        break;

    case TC_OBC_PERIPH_REBOOT:

        notify(main_get_obc_handle(), N_OBC_PERIPHERALS_REBOOT);
        break;

    case TC_OBC_DEBUG_MODE:
        // TODO: TBD — enter debug mode
        break;

    /* ── Default / Unknown ──────────────────────────────────────────────── */

    case TC_ERR:
    default:
        break;
    }

}
