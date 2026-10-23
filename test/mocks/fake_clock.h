/**
 * @file fake_clock.h
 * @brief Fake of the clock and peripheral-reconfiguration functions (clock.c / periph.c) that remembers the frequency and counts calls.
 * @date 2026-10-23
 *
 * @details
 * Replaces these functions: `clock_get_current()`, `clock_switch_to_freq()`,
 * `systemclock_init_for_freq()`, `periph_reconfigure_for_freq()` and
 * `periph_init_for_freq()`. No register is touched. The fake stores the
 * selected frequency and counts each call, so a test can check, for
 * example, that a state change switched the clock exactly once.
 *
 * It also records the scheduler-suspend depth at the moment of each clock
 * switch, which proves the switch happened with the scheduler suspended.
 */
#ifndef FAKE_CLOCK_H
#define FAKE_CLOCK_H
#include <stdbool.h>
#include "clock.h"
#include "periph.h"

/** @brief Sets the current frequency back to 80 MHz (the boot default) and all counters to 0. Call it in `setUp()`. */
void fake_clock_reset(void);

/** @brief Sets the frequency `clock_get_current()` reports, without counting a switch. Use it to start a test at another speed. */
void fake_clock_set_current(ClockFreq_t f);

/** @brief Returns the number of `clock_switch_to_freq()` calls since the last reset. */
int  fake_clock_switch_count(void);

/** @brief Returns the frequency requested by the last `clock_switch_to_freq()` call. */
ClockFreq_t fake_clock_last_switch_freq(void);

/** @brief Returns the `vTaskSuspendAll` depth during the last clock switch (1 = done with the scheduler suspended, as required). */
int  fake_clock_last_switch_suspend_depth(void);

/** @brief Returns the number of `periph_reconfigure_for_freq()` calls since the last reset. */
int  fake_clock_reconfigure_count(void);

/** @brief Returns the frequency passed to the last `periph_reconfigure_for_freq()` call. */
ClockFreq_t fake_clock_last_reconfigure_freq(void);

/** @brief Returns the number of boot-time init calls (`systemclock_init_for_freq()` and `periph_init_for_freq()` together). */
int  fake_clock_init_count(void);

#endif
