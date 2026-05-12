/**
 * @file tc_handler.h
 * @brief Telecommand definitions and processing interface.
 *
 * Defines the telecommand ID enum (matching the Ground Station protocol)
 * and the dispatch function called from the COMMS state machine upon
 * receiving a valid uplink packet.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Telecommand IDs matching the GS uplink protocol.
 *
 * The TC ID is extracted from byte [2] of the deinterleaved RX packet.
 * Values must match the Ground Station software exactly.
 */
typedef enum {
    TC_ERR                    = 0,

    /* S/C Ping */
    TC_PING                   = 1,

    /* Mode Transits */
    TC_TRANSIT_TO_NM          = 2,
    TC_TRANSIT_TO_CM          = 3,
    TC_TRANSIT_TO_SSM         = 4,
    TC_TRANSIT_TO_SM          = 5,

    /* SS Configuration */
    TC_UPLOAD_ADCS_CAL        = 6,
    TC_UPLOAD_ADCS_TLE        = 7,
    TC_UPLOAD_COMMS_CONFIG    = 8,
    TC_UPLOAD_COMMS_PARAMS    = 9,
    TC_UPLOAD_UNIX_TIME       = 10,
    TC_UPLOAD_EPS_TH          = 11,
    TC_UPLOAD_PL_CONFIG       = 12,
    TC_DOWNLINK_CONFIG        = 13,

    /* EPS Heater */
    TC_EPS_HEATER_ENABLE      = 14,
    TC_EPS_HEATER_DISABLE     = 15,

    /* PoL up/down */
    TC_POL_PAYLOAD_SHUT       = 16,
    TC_POL_ADCS_SHUT          = 17,
    TC_POL_BURNCOMMS_SHUT     = 18,
    TC_POL_HEATER_SHUT        = 19,
    TC_POL_PAYLOAD_ENABLE     = 20,
    TC_POL_ADCS_ENABLE        = 21,
    TC_POL_BURNCOMMS_ENABLE   = 22,
    TC_POL_HEATER_ENABLE      = 23,

    /* Flash Memory */
    TC_CLEAR_PL_DATA          = 24,
    TC_CLEAR_FLASH            = 25,
    TC_CLEAR_HT               = 26,

    /* COMMS */
    TC_COMMS_STOP_TX          = 27,
    TC_COMMS_RESUME_TX        = 28,
    TC_COMMS_IT_DOWNLINK      = 29,
    TC_COMMS_HT_DOWNLINK      = 30,

    /* Payload */
    TC_PAYLOAD_SCHEDULE       = 31,
    TC_PAYLOAD_DEACTIVATE     = 32,
    TC_PAYLOAD_SEND_DATA      = 33,

    /* OBC */
    TC_OBC_HARD_REBOOT        = 34,
    TC_OBC_SOFT_REBOOT        = 35,
    TC_OBC_PERIPH_REBOOT      = 36,
    TC_OBC_DEBUG_MODE         = 37,
} tc_id_t;

/**
 * @brief Process a received telecommand.
 *
 * Parses the TC ID from @p rx_data[2], performs any local processing
 * (ACK enqueue, OBDH writes, config updates), and sends the appropriate
 * FreeRTOS task notification to the target subsystem.  Task handles are
 * obtained at call time via the OBC/main getter functions, so they are
 * always up-to-date even after a task reset.
 *
 * @param rx_data       Pointer to the deinterleaved 48-byte RX packet.
 */
void tc_process(const uint8_t *rx_data);
