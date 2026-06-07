/**
 * @file eps.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2026-01-21
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef INC_EPS_H_
#define INC_EPS_H_
#ifndef DS2782_DRIVER_H
#define DS2782_DRIVER_H

#include <stdint.h>
#include <stdbool.h>


/**
 * @brief 
 * 
 * @param pv_parameters 
 */
void eps_task(void *pv_parameters);

// DS2782 raw data
typedef struct {
    uint16_t raw_voltage;               // Resolution: 4.88 mV
    int16_t  raw_current;               // Resolution: 0.104 mA
    int16_t  raw_avg_current;           // Resolution: 0.104 mA
    int16_t  raw_temperature;           // Resolution: 0.125 ºC
    uint16_t raw_accumulated_cap;       // Resolution: 0.4167 mAh
    uint8_t raw_relative_cap;           // Resolution: 1% (State of Charge)
    uint8_t raw_standby_relative_cap;   // Resolution: 1%
} Battery_Telemetry_t;

// LTC4040 (PMIC) data
typedef struct {
    bool    is_charging;           // pin !CHRG (true = charging)
    bool    has_fault;             // pin !FAULT (true = emergency fault)
    bool    is_eclipse;            // pin !PFO (true = eclipse active)
    bool    charging_disabled;     // pin CHROFF status (true = charging disabled)

    // !RST is hardwired to NRST, we can check if the external reset was triggered but not the pin exactly
    //bool    was_reset;             // Estat del pin !RST (RCC_FLAG_PINRST)

    uint16_t raw_clprog_adc;       // ADC value of generated solar current

    // --- Extra analog readings, can be added later ---
    //uint16_t raw_vsys_adc;         // general 5V rail ADC
    //uint16_t raw_killswitch_adc;   // battery voltage ADC
    //uint16_t raw_batt_ntc_adc;     // Analog battery temperature reading, could be used as backup

    // auto heater status
    bool auto_heater_enabled;
    bool heater_state;

    // read status
    bool battery_read_failure;
    bool pmic_read_failure;
} EPS_Status_t;


/* --- Hardware set functions --- */

// Use mocked data and not real hardware
void DS2782_Set_Mock_Mode(bool enable);

// Heater (Pin PB10)
void EPS_Heater_Enable(void);
void EPS_Heater_Disable(void);
bool EPS_Heater_Read(void);

//  LTC4040 Pin PA3 - CHGOFF
void EPS_Charger_Enable(void);
void EPS_Charger_Disable(void);


/* --- Hardware reading functions --- */

// Realitza el burst read complet del sensor I2C (Suporta mode simulació)
bool DS2782_Read_Hardware(Battery_Telemetry_t *telemetry_out);

// Realitza el sondeig (polling) dels canals analògics i digitals del PMIC
bool LTC4040_Read_Hardware(EPS_Status_t *pmic_out);


/* --- Compute values from read data functions --- */

uint16_t DS2782_Compute_Voltage(const Battery_Telemetry_t *telemetry);
int16_t DS2782_Compute_Current(const Battery_Telemetry_t *telemetry);
int16_t DS2782_Compute_Temperature(const Battery_Telemetry_t *telemetry);

// Force the compiler to pack this struct with ZERO empty padding bytes
#pragma pack(push, 1)

typedef struct {
    // 1. Digital Battery Sensor Data (DS2782) - 12 Bytes total
    uint16_t vbat_raw;
    int16_t  current_raw;
    int16_t  avg_current_raw;
    int16_t  temp_raw;
    uint16_t accum_cap_raw;
    uint8_t  rel_cap_raw;      // Fits in 1 byte since it's just 0-100%
    uint8_t standby_rel_cap_raw;

    // 2. Analog Power Manager Data (LTC4040 ADCs) - 8 Bytes total
    uint16_t clprog_adc;
    // --- Extra analog readings, can be added later
    //uint16_t vsys_adc;
    //uint16_t killswitch_adc;
    //uint16_t batt_ntc_adc;

    // 3. Compressed Status Word (All booleans compressed into 1 Byte)
    uint8_t  system_status;

} OBDH_Payload_t;

// Restore normal compiler padding for the rest of the code
#pragma pack(pop)

#endif /* DS2782_DRIVER_H */
#endif /* INC_EPS_H_ */

