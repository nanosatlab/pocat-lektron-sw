/**
 * @file health.h
 * @brief Software watchdog for subsystem health monitoring.
 *
 * This module implements a software watchdog using FreeRTOS event groups.
 * Each subsystem task must periodically call health_kick() to signal it is
 * alive. The OBC task calls health_check() to check which subsystems have
 * failed to kick within the configured period and refresh the hardware watchdog
 * when the software health check passes.
 */

#pragma once

#include "FreeRTOS.h"
#include "event_groups.h"
#include <stdint.h>

/**
 * @brief Health monitoring bit flags for each subsystem.
 *
 * Each subsystem has a unique bit that it uses when calling health_kick().
 * These bits are also used with health_set_expected() to define which
 * subsystems should be monitored.
 */
typedef enum {
    HEALTH_BIT_PAYLOAD = (1u << 0),  /**< Payload subsystem */
    HEALTH_BIT_OBDH    = (1u << 1),  /**< On-Board Data Handling subsystem */
    HEALTH_BIT_EPS     = (1u << 2),  /**< Electrical Power System subsystem */
    HEALTH_BIT_COMMS        = (1u << 3),  /**< Communications subsystem */
    HEALTH_BIT_ADCS         = (1u << 4),  /**< Attitude Determination and Control subsystem */
    HEALTH_BIT_TRANSCEIVER  = (1u << 5),  /**< RF transceiver subsystem */
    HEALTH_BIT_BEACON       = (1u << 6),  /**< Beacon subsystem */
} health_bit_t;

/**
 * @brief Initialize the health monitoring system.
 *
 * Creates the FreeRTOS event group used for tracking health kicks.
 * Must be called once during system startup by OBC task before any other health functions.
 */
void health_init(void);

/**
 * @brief Signal that a subsystem is alive.
 *
 * Each subsystem task except the OBC task should call this function periodically (at least once
 * per health check period) to indicate it is operating normally.
 *
 * @param bit The health bit for the calling subsystem (e.g., HEALTH_BIT_EPS).
 */
void health_kick(EventBits_t bit);

/**
 * @brief Configure the health check period.
 *
 * Sets the time window within which all expected subsystems must kick.
 * After this period, health_check() will report any subsystems that failed
 * to kick. Only the OBC task should call this function.
 *
 * @param period Health check period in FreeRTOS ticks. Use pdMS_TO_TICKS()
 *               to convert from milliseconds.
 */
void health_config(TickType_t period);

/**
 * @brief Set which subsystems are expected to report health.
 *
 * Only subsystems whose bits are set here will be monitored.
 * Only the OBC task should call this function.
 *
 * @param expected_bits Bitmask of subsystems to monitor (OR of health_bit_t values).
 *
 * @note This restarts the health check window.
 */
void health_set_expected(EventBits_t expected_bits);

/**
 * @brief Get the currently expected subsystems for health monitoring.
 * Only the OBC task should call this function.
 *
 * @return Bitmask of currently expected subsystems (OR of health_bit_t values).
 */
EventBits_t health_get_expected(void);

/**
 * @brief Register the hardware watchdog handle.
 *
 * Must be called during initialization to enable automatic IWDG refresh
 * when the system is healthy. Only the OBC task should call this function.
 *
 * @param hiwdg Pointer to the IWDG handle (IWDG_HandleTypeDef*).
 */
void health_register_iwdg(void *hiwdg);

/**
 * @brief Perform health check and refresh watchdog if healthy.
 *
 * Call this periodically from the OBC task. The IWDG is refreshed only after
 * a completed health period has no missing subsystem kicks.
 *
 * @return Bitmask of faulty subsystems when a health period has elapsed.
 *         Returns 0 both when the check period has not
 *         elapsed yet and when the elapsed check
 *         found no faults.
 */
EventBits_t health_check(void);
