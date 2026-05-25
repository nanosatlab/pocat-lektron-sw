/**
 * @file notifications.h
 * @brief Inter-task notification bit definitions.
 *
 * FreeRTOS task notifications use a 32-bit value per task. Each target task
 * has its own notification value, so bit positions are independent across
 * tasks. Senders use xTaskNotify() with eSetBits; receivers use
 * xTaskNotifyWait() and test the bits defined here.
 *
 * Naming convention: N_<TARGET>_<ACTION>
 */

#pragma once

#include <stdint.h>

/* ── General Task Notifications ─────────────────────────────────────────── */
#define N_FLASH_OPERATION_COMPLETE         (1u << 31)  /**< Flash operation completed (success or failure) */
#define N_TASK_PAUSE                       (1u << 30)  /**< OBC requests task to quiesce and ACK */
#define N_TASK_RESUME                      (1u << 29)  /**< OBC signals task to resume normal operation */

/* ── ADCS Task Notifications ────────────────────────────────────────────── */

#define N_ADCS_DESIRED_STATE_IDLE        (1u << 0)  /**< Set ADCS state to IDLE */
#define N_ADCS_DESIRED_STATE_DETUMBLING  (1u << 1)  /**< Set ADCS state to DETUMBLING */
#define N_ADCS_DESIRED_STATE_NADIR       (1u << 2)  /**< Set ADCS state to NADIR */
#define N_ADCS_NEW_CALIBRATION           (1u << 3)  /**< New calibration data available in memory */
#define N_ADCS_NEW_TLE                   (1u << 4)  /**< New TLE available in memory */

/* ── OBC Task Notifications ─────────────────────────────────────────────── */

#define N_OBC_EXIT_STATE_TO_OBC_STATE_NM   (1u << 0)  /**< Permission to upgrade state to NM */
#define N_OBC_EXIT_STATE_TO_OBC_STATE_CM   (1u << 1)  /**< Transition state to CM */
#define N_OBC_EXIT_STATE_TO_OBC_STATE_SSM  (1u << 2)  /**< Transition state to SSM */
#define N_OBC_EXIT_STATE_TO_OBC_STATE_SM   (1u << 3)  /**< Transition state to SM */
#define N_OBC_UPDATE_TIME                  (1u << 4)  /**< New Unix timestamp available to sync */
#define N_OBC_HARD_REBOOT                  (1u << 5)  /**< Perform a hard reboot (including flash) */
#define N_OBC_SOFT_REBOOT                  (1u << 6)  /**< Perform a soft reboot (without clearing flash) */
#define N_OBC_PERIPHERALS_REBOOT           (1u << 7)  /**< Reboot peripheral devices */

#define N_OBC_EXIT_STATE_GROUP_MASK (N_OBC_EXIT_STATE_TO_OBC_STATE_NM | N_OBC_EXIT_STATE_TO_OBC_STATE_CM | \
                                N_OBC_EXIT_STATE_TO_OBC_STATE_SSM | N_OBC_EXIT_STATE_TO_OBC_STATE_SM)

/* ── COMMS Task Notifications ───────────────────────────────────────────── */

#define N_COMMS_NEW_CONFIG               (1u << 0)  /**< New comms configuration available in memory */
#define N_COMMS_NEW_PARAMS               (1u << 1)  /**< New parameter set available in memory */
#define N_COMMS_STOP_RF                  (1u << 2)  /**< Stop RF transmission */
#define N_COMMS_RESUME_RF                (1u << 3)  /**< Resume RF transmission */
#define N_COMMS_TRANSMIT_BEACON          (1u << 4)  /**< Transmit the beacon (deprecated: use beacon timeout) */

/* ── Transceiver Task Notifications ────────────────────────────────────────── */

#define N_TRANSCEIVER_RADIO_IRQ_BIT      (1u << 0)  /**< DIO1 hardware interrupt: RX_DONE or TX_DONE */
#define N_TRANSCEIVER_TX_READY_BIT       (1u << 1)  /**< TX packet available in tx_queue */
#define N_TRANSCEIVER_ARQ_DL_BIT         (1u << 2)  /**< Begin ARQ DL session (OBC → GS) */

/* ── EPS Task Notifications ─────────────────────────────────────────────── */

#define N_EPS_NEW_THRESHOLDS             (1u << 0)  /**< New power thresholds available in memory */
#define N_EPS_ENABLE_AUTO_HEAT           (1u << 1)  /**< Enable automatic heater activation */
#define N_EPS_DISABLE_AUTO_HEAT          (1u << 2)  /**< Disable automatic heater activation */

/* ── OBDH Task Notifications ────────────────────────────────────────────── */

#define N_OBDH_CLEAR_PAYLOAD             (1u << 0)  /**< Clear all payload data in memory */
#define N_OBDH_CLEAR_FLASH               (1u << 1)  /**< Clear all flash memory */
#define N_OBDH_CLEAR_HT                  (1u << 2)  /**< Clear all historic telemetry */

/* ── Payload Task Notifications ─────────────────────────────────────────── */

#define N_PAYLOAD_ACTIVATE               (1u << 0)  /**< Activate the payload */
#define N_PAYLOAD_DEACTIVATE             (1u << 1)  /**< Deactivate the payload */
