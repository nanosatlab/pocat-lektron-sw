/**
 * @file notifications.h
 * @brief Inter-task notification bit definitions.
 *
 * Each task has an array of notification slots; two are used:
 *   index 0 — general signalling. Per-task bitmask (eSetBits), read by
 *             wait_for_notification(). A bit's meaning is per task, so the
 *             N_<TARGET>_* groups below reuse low bits independently.
 *   index 1 — OBDH flash completion (OBDH_NOTIFY_IDX): the HAL status as a
 *             whole value (eSetValueWithOverwrite).
 *
 * Naming convention: N_<TARGET>_<ACTION>
 */

#pragma once

#include <stdint.h>
#include "FreeRTOS.h"   /* for TickType_t */

/* ════════════════════════ Notification indices ═════════════════════════ */
/* Index 0 carries every N_* bit and is read by wait_for_notification().
   Index 1 is reserved for the OBDH flash round-trip, so a flash completion can
   never collide with a task's own signalling. */
#define OBDH_NOTIFY_IDX                    1u   /**< Flash completion; notification value = HAL status */


/* ═══════════ Index 0 — GLOBAL notifications (sent to any task) ═══════════ */
#define N_TASK_PAUSE                       (1u << 30)  /**< OBC requests task to quiesce and ACK */
#define N_TASK_RESUME                      (1u << 29)  /**< OBC signals task to resume normal operation */


/* ══════════ Index 0 — PER-TASK notifications (bits local to each) ════════ */

/* ── ADCS Task Notifications ────────────────────────────────────────────── */

#define N_ADCS_DESIRED_STATE_IDLE        (1u << 0)  /**< Set ADCS state to IDLE */
#define N_ADCS_DESIRED_STATE_DETUMBLING  (1u << 1)  /**< Set ADCS state to DETUMBLING */
#define N_ADCS_DESIRED_STATE_NADIR       (1u << 2)  /**< Set ADCS state to NADIR */
#define N_ADCS_NEW_CALIBRATION           (1u << 3)  /**< New calibration data available in memory */
#define N_ADCS_NEW_TLE                   (1u << 4)  /**< New TLE available in memory */

/* ── OBC Task Notifications ─────────────────────────────────────────────── */

#define N_OBC_EXIT_STATE_TO_NOMINAL      (1u << 0)  /**< Permission to upgrade state to NOMINAL */
#define N_OBC_EXIT_STATE_TO_CONTINGENCY  (1u << 1)  /**< Transition state to CONTINGENCY */
#define N_OBC_EXIT_STATE_TO_SUNSAFE      (1u << 2)  /**< Transition state to SUNSAFE */
#define N_OBC_EXIT_STATE_TO_SURVIVAL     (1u << 3)  /**< Transition state to SURVIVAL */
#define N_OBC_UPDATE_TIME                (1u << 4)  /**< New Unix timestamp available to sync */
#define N_OBC_HARD_REBOOT                (1u << 5)  /**< Perform a hard reboot (including flash) */
#define N_OBC_SOFT_REBOOT                (1u << 6)  /**< Perform a soft reboot (without clearing flash) */
#define N_OBC_PERIPHERALS_REBOOT         (1u << 7)  /**< Reboot peripheral devices */
#define N_OBC_EPS_FAULT_DETECTED         (1u << 8)  /**< EPS reported a PMIC fault via EXTI */
#define N_OBC_EPS_ECLIPSE_START          (1u << 9)  /**< EPS reported !PFO power-fail (input power lost; commonly eclipse) via EXTI */
#define N_OBC_EPS_ECLIPSE_END            (1u << 10) /**< EPS reported !PFO cleared (input power restored) via EXTI */

#define N_OBC_EXIT_STATE_GROUP_MASK (N_OBC_EXIT_STATE_TO_NOMINAL | N_OBC_EXIT_STATE_TO_CONTINGENCY | \
                                N_OBC_EXIT_STATE_TO_SUNSAFE | N_OBC_EXIT_STATE_TO_SURVIVAL)

/* ── COMMS Task Notifications ───────────────────────────────────────────── */

#define N_COMMS_NEW_CONFIG               (1u << 0)  /**< New comms configuration available in memory */
#define N_COMMS_NEW_PARAMS               (1u << 1)  /**< New parameter set available in memory */
#define N_COMMS_STOP_RF                  (1u << 2)  /**< Stop RF transmission */
#define N_COMMS_RESUME_RF                (1u << 3)  /**< Resume RF transmission */
#define N_COMMS_TRANSMIT_BEACON          (1u << 4)  /**< Transmit the beacon (deprecated: use beacon timeout) */

/* ── Transceiver Task Notifications ────────────────────────────────────────── */

#define N_TRANSCEIVER_RADIO_IRQ_BIT      (1u << 0)  /**< DIO1 hardware interrupt: RX_DONE or TX_DONE */
#define N_TRANSCEIVER_TX_READY_BIT       (1u << 1)  /**< TX packet available in tx_queue */

/* ── EPS Task Notifications ─────────────────────────────────────────────── */

#define N_EPS_NEW_THRESHOLDS             (1u << 0)  /**< New power thresholds available in memory */
#define N_EPS_ENABLE_AUTO_HEAT           (1u << 1)  /**< Enable automatic heater activation */
#define N_EPS_DISABLE_AUTO_HEAT          (1u << 2)  /**< Disable automatic heater activation */
#define N_EPS_NEW_SAMPLING               (1u << 3)  /**< New sampling period available in memory */
#define N_EPS_ENABLE_CHARGER             (1u << 4)  /**< Enable battery charger (CHROFF low) */
#define N_EPS_DISABLE_CHARGER            (1u << 5)  /**< Disable battery charger (CHROFF high) */
#define N_EPS_FAULT_DETECTED             (1u << 6)  /**< !FAULT EXTI fired — charger already disabled by ISR */
#define N_EPS_ECLIPSE_START              (1u << 7)  /**< !PFO fell — input power lost (commonly eclipse, but also any brown-out/low-sun) */
#define N_EPS_ECLIPSE_END                (1u << 8)  /**< !PFO rose — input power restored */
#define N_EPS_NEW_HEATER_BANDS           (1u << 9)  /**< New heater hysteresis bands available in memory */


/* ── OBDH Task Notifications ────────────────────────────────────────────── */

#define N_OBDH_CLEAR_PAYLOAD             (1u << 0)  /**< Clear all payload data in memory */
#define N_OBDH_CLEAR_FLASH               (1u << 1)  /**< Clear all flash memory */
#define N_OBDH_CLEAR_HT                  (1u << 2)  /**< Clear all historic telemetry */

/* ── Payload Task Notifications ─────────────────────────────────────────── */

#define N_PAYLOAD_ACTIVATE               (1u << 0)  /**< Activate the payload */
#define N_PAYLOAD_DEACTIVATE             (1u << 1)  /**< Deactivate the payload */

/**
 * @brief Wait for pending task notifications, blocking up to a timeout.
 *
 * Wraps xTaskNotifyWait(): clears nothing on entry, clears all bits on exit,
 * and blocks for up to @p timeout ticks. The notification value is always
 * fully drained, so a return value of 0 means no notification arrived.
 *
 * @param timeout Maximum time to block, in ticks.
 *                Pass 0 for a non-blocking poll.
 * @return Notification bitmask received by the task (0 if none).
 */
uint32_t wait_for_notification(TickType_t timeout);
