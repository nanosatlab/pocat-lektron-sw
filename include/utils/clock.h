/**
 * @file clock.h
 * @brief Dynamic system clock frequency switching for power management.
 * @details
 * Provides startup clock configuration and runtime switching between the
 * supported system clock modes: 80 MHz using HSI+PLL, 8 MHz using MSI range 7,
 * and 2 MHz using MSI range 5.
 */

#ifndef INC_CLOCK_H_
#define INC_CLOCK_H_

#include <stdbool.h>

/**
 * @brief Supported system clock frequency modes.
 */
typedef enum {
    CLK_FREQ_80MHZ,  /**< 80 MHz system clock using HSI with PLL. */
    CLK_FREQ_8MHZ,   /**< 8 MHz system clock using MSI range 7. */
    CLK_FREQ_2MHZ    /**< 2 MHz system clock using MSI range 5. */
} ClockFreq_t;

/**
 * @brief Initialize the system clock for a given frequency during startup.
 * @param freq Target system clock frequency.
 * @return true on success, false if a HAL call failed.
 */
bool systemclock_init_for_freq(ClockFreq_t freq);

/**
 * @brief Switch system clock to a given frequency at runtime.
 * @param freq Target system clock frequency.
 * @return true on success, false if a HAL call failed.
 */
bool clock_switch_to_freq(ClockFreq_t freq);

/**
 * @brief Get the current clock frequency setting.
 * @return Current ClockFreq_t value.
 */
ClockFreq_t clock_get_current(void);



#endif /* INC_CLOCK_H_ */
