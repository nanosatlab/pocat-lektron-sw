/**
 * @file tc_handler.c
 * @brief Telecommand dispatch and processing.
 *
 * Each case in the switch corresponds to a telecommand defined in
 * tc_handler.h. Processing that was already implemented in the reference
 * codebase (reference/COMMS/comms.c) is included as comments for
 * reference during reimplementation.
 * In void tc_process, each case performs local work (flash writes, queue enqueues) and then
 * sends a FreeRTOS notification to the target subsystem task.
 * Link-layer ACK is sent automatically by transceiver_task for every
 * received packet — tc_process() does NOT need to do that.
 * Application-level response packets (beacons, downlink data) are pushed
 * onto tx_queue and transceiver_task drains them.
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


/* ---- Constants ---- */

/* Telemetry layout in flash (legacy format) */
#define TLCOUNTER_MAX        146
#define BEACON_PL_LEN         28

/* Packet sizes for multi-packet uploads */
#define TLE_PACKET_SIZE       34
#define CALIBRATION_PKT_SIZE  36

/* Output power address — TODO: add to flash.h */
#define OUTPUT_POWER_ADDR    (TIMEOUT_ADDR + 2u)   /* 1 byte */
#define FRF_ADDR             (TIMEOUT_ADDR + 3u)   /* 1 byte */
#define CADMODE_ADDR         (TIMEOUT_ADDR + 4u)   /* 1 byte */


/* ---- Private helpers ---- */

/**
 * @brief Send a notification to a target task (NULL-safe).
 */
static inline void notify(TaskHandle_t handle, uint32_t bits)
{
    if (handle != NULL) {
        xTaskNotify(handle, bits, eSetBits);
    }}
/**
 * @brief Push a packet onto tx_queue and wake transceiver_task.
 *
 * @param data      48-byte packet buffer (already has header filled).
 * @param needs_ack true  → transceiver does ARQ (retry on no ACK).
 *                  false → fire-and-forget.
 */
static void tc_enqueue_tx(const uint8_t* data, bool needs_ack)
{
    TxQueueEntry_t entry = { 0 };
    memcpy(entry.data, data, COMMS_PKT_SIZE);
    entry.length = COMMS_PKT_SIZE;
    entry.needs_ack = needs_ack;

    QueueHandle_t tx_q = comms_get_tx_queue();
    if (tx_q == NULL) return;

    if (xQueueSend(tx_q, &entry, pdMS_TO_TICKS(100)) != pdTRUE) {
        printf("TC: tx_queue full, dropping packet\r\n");
        return;}

    TaskHandle_t trx = tm_get_task_handle(TM_TASK_TRANSCEIVER);
    if (trx != NULL) {
        xTaskNotify(trx, N_TRANSCEIVER_TX_READY_BIT, eSetBits);
    }}

/**
 * @brief Fill the standard 6-byte packet header.
 *
 * Layout: [0..3] unix timestamp, [4] packet_num, [5] op_type.
 */
static void tc_fill_header(uint8_t* pkt, uint8_t op_type, uint8_t pkt_num)
{
    uint32_t t = time_get_unix();
    pkt[0] = (t >> 24) & 0xFF;
    pkt[1] = (t >> 16) & 0xFF;
    pkt[2] = (t >> 8) & 0xFF;
    pkt[3] = t & 0xFF;
    pkt[4] = pkt_num;
    pkt[5] = op_type;
}

/**
 * @brief NULL-safe xTaskNotify wrapper.
 */
static inline void notify(TaskHandle_t h, uint32_t bits)
{
    if (h != NULL) xTaskNotify(h, bits, eSetBits);
}

/* ---- Multi-packet upload state ---- */

/* ADCS calibration arrives in 3 consecutive TC packets */
static uint8_t adcs_cal_counter = 1;

/* TLE arrives in 5 consecutive TC packets */
static uint8_t tle_counter = 1;


/* ---- Public functions ---- */

void tc_process(const uint8_t *rx_data)
{
    tc_id_t tc_id = (tc_id_t)rx_data[2];
    uint8_t pkt[COMMS_PKT_SIZE] = { 0 };

    switch (tc_id) {

    /* ── S/C Ping ───────────────────────────────────────────────────────── */

    case TC_PING: ¡
        /*
        * The link-layer ACK is already sent by transceiver_task.
        * We also send a beacon as application-level confirmation so the
        * GS knows the satellite is "alive" and in what state.
        */
        tc_fill_header(pkt, BEACON_OP, 0);
        OBDH_Read_Request(CURRENT_STATE_ADDR, &pkt[6], 1);
        tc_enqueue_tx(pkt, /*needs_ack=*/false);
        break;
/*Beacon de confirmación para que la GS vea que el state del satélite. Si el equipo de GS espera solo el ACK de enlace, 
puedes dejar el case vacío con solo un break.*/
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

    case TC_UPLOAD_ADCS_CAL: //(3 packets)
        /*
        * Packet structure (reference):
        *   Packet 1 (marker byte 86):  36 bytes magnetometer matrix
        *                                3 bytes magnetometer offset [0..2]
        *   Packet 2 (marker byte 164):  9 bytes magnetometer offset [3..11]
        *                               21 bytes gyro polynomial
        *                                6 bytes photodiodes offset [0..5]
        *   Packet 3 (marker byte 255): 18 bytes photodiodes offset [6..23]
        *
        * rx_data[4] carries the marker byte distinguishing each packet.
        */
        if (adcs_cal_counter == 1 && rx_data[4] == 86) {
            OBDH_Write_Request(MAGNETO_MATRIX_ADDR, &rx_data[4], CALIBRATION_PKT_SIZE);
            OBDH_Write_Request(MAGNETO_OFFSET_ADDR, &rx_data[40], 3);
            adcs_cal_counter++;
        }
        else if (adcs_cal_counter == 2 && rx_data[4] == 164) {
            OBDH_Write_Request(MAGNETO_OFFSET_ADDR + 3, &rx_data[4], 9);
            OBDH_Write_Request(GYRO_POLYN_ADDR, &rx_data[13], CALIBRATION_PKT_SIZE - 12);
            OBDH_Write_Request(PHOTODIODES_OFFSET_ADDR, &rx_data[37], 6);
            adcs_cal_counter++;
        }
        else if (adcs_cal_counter == 3 && rx_data[4] == 255) {
            OBDH_Write_Request(PHOTODIODES_OFFSET_ADDR + 6, &rx_data[4], 18);
            adcs_cal_counter = 1;
            /* Notify ADCS that new calibration is ready */
            notify(tm_get_task_handle(TM_TASK_ADCS), N_ADCS_NEW_CALIBRATION);
        }
        else {
            /* Unexpected marker — reset counter and log */
            printf("TC_UPLOAD_ADCS_CAL: unexpected marker 0x%02X at step %u\r\n",
                rx_data[4], adcs_cal_counter);
            adcs_cal_counter = 1;
        }
        break;

    case TC_UPLOAD_ADCS_TLE: //(5 packets)

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
