/**
 * @file types.h
 * @brief TT&C protocol v2 shared constants (TTC.md single source of truth).
 *
 * Enumerates every TYPE, FLAGS, NACK_REASON, TC_ID, and TM_ID code from
 * the protocol spec. Keep in sync with:
 *   - gs-server/domain/air/types/types.go (Go mirror)
 *   - firmware/serial_framing.h (outer-TYPE subset only)
 *   - bridge/internal/framing/framing.go (outer-TYPE subset only)
 */

#pragma once

#include <stdint.h>

/* ---------- Protocol version (§4.1) ---------- */
#define AIR_PROTOCOL_VER  0x02u
#define AIR_BODY_VER      0x01u

/* ---------- Outer (wired) frame TYPE codes (§3.2) ---------- */
typedef enum {
    OUTER_AIR_TX        = 0x01,
    OUTER_AIR_RX        = 0x02,
    OUTER_TX_DONE       = 0x03,
    OUTER_TX_FAIL       = 0x04,
    OUTER_FW_CONFIG     = 0x05,
    OUTER_FW_CONFIG_OK  = 0x06,
    OUTER_FW_STATUS_REQ = 0x07,
    OUTER_FW_STATUS     = 0x08,
    OUTER_FW_LOG        = 0x09,
} outer_type_t;

/* ---------- TX_FAIL reason codes (§3.5) ---------- */
typedef enum {
    TX_FAIL_TIMEOUT     = 0x01,
    TX_FAIL_CAD_GIVEUP  = 0x02,
    TX_FAIL_TOO_LONG    = 0x03,
    TX_FAIL_WRONG_STATE = 0x04,
    TX_FAIL_QUEUE_FULL  = 0x05,
} tx_fail_reason_t;

/* ---------- Air-layer TYPE codes (§4.3) ---------- */
typedef enum {
    AIR_TC          = 0x01,
    AIR_TM          = 0x02,
    AIR_ACK         = 0x03,
    AIR_NACK        = 0x04,
    AIR_BEACON      = 0x10,
    AIR_DATA_BEGIN  = 0x20,
    AIR_DATA        = 0x21,
    AIR_DATA_ACK    = 0x22,
    AIR_DATA_END    = 0x23,
    AIR_PING        = 0x30,
    AIR_PONG        = 0x31,
} air_type_t;

/* ---------- FLAGS bitfield (§4.4) ---------- */
#define AIR_FLAG_REQUIRES_ACK   (1u << 0)
#define AIR_FLAG_IS_RETX        (1u << 1)
#define AIR_FLAG_URGENT         (1u << 2)
#define AIR_FLAG_AUTH_TAG       (1u << 7)

/* ---------- NACK REASON codes (§6.5) ---------- */
typedef enum {
    NACK_UNKNOWN_TYPE       = 0x01,
    NACK_UNKNOWN_TC_ID      = 0x02,
    NACK_BAD_BODY_VER       = 0x03,
    NACK_BAD_PARAMS         = 0x04,
    NACK_PARAM_OUT_OF_RANGE = 0x05,
    NACK_NOT_ALLOWED        = 0x06,
    NACK_BUSY               = 0x07,
    NACK_ARQ_NO_SESSION     = 0x08,
    NACK_ARQ_SESSION_FULL   = 0x09,
    NACK_ARQ_BAD_WINDOW     = 0x0A,
    NACK_CONFIG_REJECTED    = 0x0B,
} nack_reason_t;

/* ---------- TC_ID catalogue (§10) ---------- */
typedef enum {
    TC_PING                    = 0x01,
    TC_TRANSIT_TO_NM           = 0x02,
    TC_TRANSIT_TO_CM           = 0x03,
    TC_TRANSIT_TO_SSM          = 0x04,
    TC_TRANSIT_TO_SM           = 0x05,
    TC_LORA_CONFIG             = 0x08,
    TC_COMMS_PARAMS            = 0x09,
    TC_UPLOAD_UNIX_TIME        = 0x0A,
    TC_UPLOAD_EPS_TH           = 0x0B,
    TC_UPLOAD_PL_CONFIG        = 0x0C,
    TC_REQUEST_BEACON_NOW      = 0x0D,
    TC_REQUEST_DOWNLINK_CONFIG = 0x0E,
    TC_REQUEST_HK_LIVE         = 0x0F,
    TC_REQUEST_HK_HISTORY      = 0x10,
    TC_REQUEST_PAYLOAD_DATA    = 0x11,
    TC_REQUEST_OBC_LOG         = 0x12,
    TC_UPLOAD_TLE_BEGIN        = 0x18,
    TC_UPLOAD_ADCS_CAL_BEGIN   = 0x19,
    TC_EPS_HEATER_ENABLE       = 0x20,
    TC_EPS_HEATER_DISABLE      = 0x21,
    TC_POL_PAYLOAD_SHUT        = 0x30,
    TC_POL_PAYLOAD_ENABLE      = 0x31,
    TC_POL_ADCS_SHUT           = 0x32,
    TC_POL_ADCS_ENABLE         = 0x33,
    TC_POL_BURNCOMMS_SHUT      = 0x34,
    TC_POL_BURNCOMMS_ENABLE    = 0x35,
    TC_POL_HEATER_SHUT         = 0x36,
    TC_POL_HEATER_ENABLE       = 0x37,
    TC_CLEAR_PL_DATA           = 0x40,
    TC_CLEAR_FLASH             = 0x41,
    TC_CLEAR_HT                = 0x42,
    TC_COMMS_STOP_TX           = 0x50,
    TC_COMMS_RESUME_TX         = 0x51,
    TC_SET_BEACON_PERIOD       = 0x52,
    TC_PAYLOAD_SCHEDULE        = 0x60,
    TC_PAYLOAD_DEACTIVATE      = 0x61,
    TC_OBC_HARD_REBOOT         = 0x70,
    TC_OBC_SOFT_REBOOT         = 0x71,
    TC_OBC_PERIPH_REBOOT       = 0x72,
    TC_OBC_DEBUG_MODE          = 0x73,
} tc_id_t;

/* ---------- TM_ID catalogue (§11) ---------- */
typedef enum {
    TM_HK_LIVE         = 0x01,
    TM_DOWNLINK_CONFIG = 0x02,
    TM_LORA_CFG_REPORT = 0x03,
    TM_TC_RESULT       = 0x04,
    TM_PING_REPLY      = 0x05,
} tm_id_t;

/* ---------- ARQ TRANSFER_TYPE codes (§6.7.1) ---------- */
typedef enum {
    TRANSFER_PAYLOAD_DATA     = 0x01,
    TRANSFER_HK_HISTORY       = 0x02,
    TRANSFER_OBC_LOG          = 0x03,
    TRANSFER_TLE_UPLOAD       = 0x10,
    TRANSFER_ADCS_CALIBRATION = 0x11,
    TRANSFER_FLIGHT_PARAMS    = 0x12,
} transfer_type_t;

/* ---------- DATA_END status codes (§6.10) ---------- */
typedef enum {
    DATA_END_COMPLETE_OK         = 0x00,
    DATA_END_ABORTED_BY_SENDER   = 0x01,
    DATA_END_ABORTED_BY_RECEIVER = 0x02,
} data_end_status_t;
