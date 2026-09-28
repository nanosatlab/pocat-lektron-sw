/**
 * @file time.h
 * @brief RTC time helpers — Unix timestamp get/set.
 * @author Jaume Cortés Grimalt
 * @date 2026-03-18
 */

#ifndef UTILS_TIME_H
#define UTILS_TIME_H

#include <stdint.h>

/**
 * @brief Initialize the time module (creates RTC mutex).
 * @details Must be called before any other time_* functions.
 */
void time_init(void);

/**
 * @brief Get the current Unix timestamp from the RTC.
 * @return Seconds since 1970-01-01 00:00:00 UTC.
 */
uint32_t time_get_unix(void);

/**
 * @brief Set the RTC from a Unix timestamp.
 * @param epoch Seconds since 1970-01-01 00:00:00 UTC.
 */
void time_set_unix(uint32_t epoch);

/**
 * @brief Print a Unix timestamp as a human-readable date/time over UART.
 * @param epoch Seconds since 1970-01-01 00:00:00 UTC.
 */
void time_print_epoch(uint32_t epoch);

#endif /* UTILS_TIME_H */
