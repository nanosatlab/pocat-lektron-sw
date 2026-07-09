/**
 * @file eps_hw.h
 * @brief Hardware abstraction interface for the EPS task.
 *
 * eps.c contains only task logic and talks to the power hardware exclusively
 * through this interface. The POCAT-Lektron binding (eps_hw_pocat.c)
 * implements it on the DS2782 fuel gauge (ds2782.c), the LTC4040 PMIC
 * (ltc4040.c) and the heater GPIO; porting the EPS task to different power
 * hardware means reimplementing this file's functions, not touching eps.c.
 *
 * In the UNIT_TEST build the sensor-read functions dispatch at runtime to the
 * injectable mocks in eps_mock.c (see the injection API below); the actuator
 * functions (charger, heater) always drive the real pins so on-target tests
 * can assert the actual GPIO state.
 */

#ifndef INC_SUBSYSTEMS_EPS_EPS_HW_H_
#define INC_SUBSYSTEMS_EPS_EPS_HW_H_

#include <stdint.h>
#include <stdbool.h>

/** @brief Raw battery telemetry snapshot as delivered by the fuel gauge. */
typedef struct {
    uint16_t raw_voltage;             // 4.88 mV/LSB
    int16_t  raw_current;             // 0.15625 mA/LSB (10mΩ sense) — instantaneous
    int16_t  raw_temperature;         // 0.125 ºC/LSB
    uint8_t  raw_relative_cap;        // 1% State of Charge — RARC (active)
    int16_t  raw_avg_current;         // 0.15625 mA/LSB (10mΩ sense) — IAVG, 28 s window
    int16_t  raw_acr;                 // 0.625 mAh/LSB (10mΩ sense) — accumulated charge in/out
    uint16_t raw_active_abs_cap;      // 1.6 mAh/LSB — RAAC (active absolute capacity)
    uint16_t raw_standby_abs_cap;     // 1.6 mAh/LSB — RSAC (standby absolute capacity)
    uint8_t  raw_standby_rel_cap;     // 1% — RSRC (standby relative capacity)
} Battery_Telemetry_t;

/** @brief PMIC status snapshot plus EPS-level status flags. */
typedef struct {
    bool    is_charging;           // pin !CHRG (true = charging)
    bool    has_fault;             // pin !FAULT (true = emergency fault)
    bool    is_eclipse;            // pin !PFO power-fail (true = input power lost; commonly eclipse, but not only)
    bool    charging_disabled;     // pin CHRGOFF status (true = charging disabled)

    uint16_t raw_clprog_adc;       // ADC value of generated solar current

    // auto heater status
    bool auto_heater_enabled;
    bool heater_state;

    // read status
    bool battery_read_failure;
    bool pmic_read_failure;
} EPS_Status_t;

/* --- Boot-time self-check (called once from setup_eps()) --- */

/**
 * @brief Confirm the power hardware is reachable and log the result.
 *
 * NOT initialization: all EPS pins and buses are configured centrally at
 * boot by periph_init_for_freq() (pin map in periph.h), before any task
 * runs. This function only probes the fuel gauge on its bus and prints a
 * standalone voltage reading as a minimum-viable sanity test, so a wiring or
 * address problem is visible in the boot log. Non-fatal on failure: the EPS
 * task keeps running and reports battery_read_failure each cycle.
 */
void eps_hw_probe(void);

/* --- Sensor reads (mockable in the UNIT_TEST build) --- */

/** @brief Full battery telemetry snapshot. @return false on bus failure. */
bool eps_hw_read_battery(Battery_Telemetry_t *telemetry_out);

/** @brief PMIC status pins + input-current ADC (0xFFFF on ADC failure). */
bool eps_hw_read_pmic(EPS_Status_t *pmic_out);

/* --- Actuators (always drive the real pins, ISR-safe: plain GPIO writes) --- */

void eps_hw_charger_set(bool enable);
void eps_hw_heater_set(bool on);
bool eps_hw_heater_get(void);

/** @brief Raw power-fail line state, for the !PFO edge ISR. */
bool eps_hw_pfo_is_asserted(void);

/* --- Raw-to-physical conversions (fuel-gauge scale factors) --- */

uint16_t eps_hw_battery_voltage_mv(const Battery_Telemetry_t *telemetry);
int16_t  eps_hw_battery_current_ma(const Battery_Telemetry_t *telemetry);
int16_t  eps_hw_battery_temp_c(const Battery_Telemetry_t *telemetry);

/* --- Mock injection API (defined in eps_mock.c, UNIT_TEST builds only) --- */

void DS2782_Set_Mock_Mode(bool enable);
void DS2782_Set_Mock_Values(const Battery_Telemetry_t *v);
void DS2782_Set_Mock_Fail(bool fail);
void LTC4040_Set_Mock_Values(const EPS_Status_t *v);
void LTC4040_Set_Mock_Fail(bool fail);

#endif /* INC_SUBSYSTEMS_EPS_EPS_HW_H_ */
