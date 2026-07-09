/**
 * @file ltc4040.h
 * @brief Driver for the ADI LTC4040 battery backup power manager (PMIC).
 *
 * The LTC4040 has no digital bus: its interface is a set of GPIO lines plus
 * one analog monitor. This driver owns their semantics; the pin assignments
 * live in periph.h (EPS_PIN_*) and the pins are configured centrally at boot
 * by periph_gpio_init().
 *
 * Status outputs (!CHRG, !FAULT, !PFO) are open-drain and active-low: the
 * chip sinks the line low to assert, a pull-up provides the de-asserted high.
 * CHRGOFF is a push-pull input to the chip, driven high to disable charging.
 * CLPROG carries a voltage proportional to the input (solar) current,
 * sampled via the ADC.
 */

#ifndef INC_SUBSYSTEMS_EPS_LTC4040_H_
#define INC_SUBSYSTEMS_EPS_LTC4040_H_

#include <stdbool.h>
#include "eps_hw.h"   /* EPS_Status_t */

/**
 * @brief Poll the PMIC status pins and the CLPROG input-current ADC channel.
 *
 * Fills is_charging / has_fault / is_eclipse (active-low pins, true = pin
 * low) and charging_disabled (CHRGOFF read-back, true = pin high). On an ADC
 * failure raw_clprog_adc is set to the out-of-band sentinel 0xFFFF (the ADC
 * is 12-bit, valid range 0x0000-0x0FFF) so ground software can tell a failed
 * conversion from a genuine zero-current reading; the GPIO reads stay valid.
 *
 * @return false only on NULL argument.
 */
bool ltc4040_read_status(EPS_Status_t *pmic_out);

/** @brief Drive CHRGOFF low: charging enabled. ISR-safe (plain GPIO write). */
void ltc4040_charger_enable(void);

/** @brief Drive CHRGOFF high: charging disabled. ISR-safe (plain GPIO write). */
void ltc4040_charger_disable(void);

/** @brief Read back the CHRGOFF output state (true = charging disabled). */
bool ltc4040_charger_is_disabled(void);

/** @brief Raw !PFO line state (true = asserted, input power lost). ISR-safe. */
bool ltc4040_pfo_is_asserted(void);

#endif /* INC_SUBSYSTEMS_EPS_LTC4040_H_ */
