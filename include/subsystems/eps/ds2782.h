/**
 * @file ds2782.h
 * @brief Low-level driver for the Maxim DS2782 stand-alone fuel gauge.
 *
 * The DS2782 register file is auto-incrementing; a single I2C transaction
 * starting at RAAC MSB (0x02) and continuing through ACR LSB (0x11) yields a
 * temporally consistent snapshot of every register the EPS task consumes.
 * Higher-level decoding (sign extension, unit conversion) lives here too:
 * the driver owns everything that depends on the DS2782 register layout and
 * scale factors, so the EPS task never interprets raw bytes itself.
 *
 * The capacity model (Full40, Aging Capacity, full/empty detection
 * thresholds, RSNSP, cell-model slopes) lives in Parameter EEPROM block 1
 * (0x60..0x7F). EEPROM is only reachable through its shadow RAM: writes land
 * in the shadow, a Copy Data function command commits them, and a Recall
 * Data command reloads the shadow from EEPROM (datasheet "Memory", Fig. 23).
 * Until Full40 and RSNSP are programmed the gauge has no reference capacity
 * and RAAC/RSAC/RARC/RSRC read 0.
 */

#ifndef INC_SUBSYSTEMS_EPS_DS2782_H_
#define INC_SUBSYSTEMS_EPS_DS2782_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32l4xx_hal.h"
#include "eps_hw.h"   /* Battery_Telemetry_t */

/* I2C slave address — DS2782 fixed 7-bit address is 0110100b = 0x34
 * (confirmed by on-bench HAL_I2C_IsDeviceReady scan). The STM32 HAL I2C API
 * expects the address pre-shifted by one bit, giving 0x68 for write
 * transactions (0x69 for read). */
#define DS2782_I2C_ADDR_7BIT    0x34u
#define DS2782_I2C_ADDR_8BIT    (DS2782_I2C_ADDR_7BIT << 1)

/* Current-sense resistor R9 on the EPS board, in milliohms. ACR and the
 * capacity parameters (AC, Full40) are charge across this resistor
 * (6.25 uVh/LSB -> 0.4167 mAh/LSB at 15 mOhm); IMIN and IAE are voltages
 * across it (50 uV and 200 uV/LSB). RAAC/RSAC are already in mAh because the
 * gauge applies RSNSP internally. */
#define DS2782_RSNS_MOHM        15u

/* Register addresses (datasheet Table 2, "Memory Map"). */
#define DS2782_REG_STATUS       0x01u   /* status flags, see DS2782_STATUS_*  */
#define DS2782_REG_RAAC_M       0x02u   /* active absolute capacity, 1.6 mAh  */
#define DS2782_REG_RAAC_L       0x03u
#define DS2782_REG_RSAC_M       0x04u   /* standby absolute capacity, 1.6 mAh */
#define DS2782_REG_RSAC_L       0x05u
#define DS2782_REG_RARC         0x06u   /* active relative capacity, %        */
#define DS2782_REG_RSRC         0x07u   /* standby relative capacity, %       */
#define DS2782_REG_IAVG_M       0x08u   /* 28 s averaged current, MSB         */
#define DS2782_REG_IAVG_L       0x09u
#define DS2782_REG_TEMP_M       0x0Au   /* temperature, MSB (11-bit signed)   */
#define DS2782_REG_TEMP_L       0x0Bu
#define DS2782_REG_VOLT_M       0x0Cu   /* voltage, MSB (11-bit unsigned)     */
#define DS2782_REG_VOLT_L       0x0Du
#define DS2782_REG_CURR_M       0x0Eu   /* instantaneous current, MSB (s16)   */
#define DS2782_REG_CURR_L       0x0Fu
#define DS2782_REG_ACR_M        0x10u   /* accumulated charge, MSB (signed)   */
#define DS2782_REG_ACR_L        0x11u
#define DS2782_REG_ACRL_M       0x12u   /* fractional ACR (read only)         */
#define DS2782_REG_ACRL_L       0x13u
#define DS2782_REG_AS           0x14u   /* age scalar, 2^-7 (0.78 %)/LSB      */
#define DS2782_REG_SFR          0x15u   /* special feature register           */
#define DS2782_REG_EEPROM       0x1Fu   /* EEPROM access: EEC busy, locks     */
#define DS2782_REG_FUNC_CMD     0xFEu   /* function command (copy / recall)   */

/* Burst-read window: RAAC_M .. ACR_L inclusive. */
#define DS2782_BURST_START      DS2782_REG_RAAC_M
#define DS2782_BURST_LEN        16u

/* STATUS register (0x01) bits. */
#define DS2782_STATUS_CHGTF     0x80u   /* charge to full detected            */
#define DS2782_STATUS_AEF       0x40u   /* active empty (VOLT < VAE)          */
#define DS2782_STATUS_SEF       0x20u   /* standby empty (RSRC < 10 %)        */
#define DS2782_STATUS_LEARNF    0x10u   /* learn cycle armed                  */

/* EEPROM register (0x1F) bits. */
#define DS2782_EEPROM_EEC       0x80u   /* Copy Data in progress              */
#define DS2782_EEPROM_BL1       0x02u   /* block 1 permanently locked         */

/* Function commands for Parameter EEPROM block 1 (datasheet Table 5). The
 * Lock command (0x66) is deliberately not defined: a locked block can never
 * be written again, which would freeze the capacity model in flight. */
#define DS2782_CMD_COPY_BLOCK1   0x44u  /* shadow RAM -> EEPROM               */
#define DS2782_CMD_RECALL_BLOCK1 0xB4u  /* EEPROM -> shadow RAM               */

/* Parameter EEPROM block 1 (datasheet Table 3). The capacity window
 * 0x62..0x77 is contiguous; CONTROL, AB, RSGAIN, RSTC, FRSGAIN and the slave
 * address are outside it and never touched by this driver. */
#define DS2782_PARAM_AC_M       0x62u   /* aging capacity, 6.25 uVh/LSB       */
#define DS2782_PARAM_AC_L       0x63u
#define DS2782_PARAM_VCHG       0x64u   /* charge voltage, 19.52 mV/LSB       */
#define DS2782_PARAM_IMIN       0x65u   /* min charge current, 50 uV/LSB      */
#define DS2782_PARAM_VAE        0x66u   /* active-empty voltage, 19.52 mV/LSB */
#define DS2782_PARAM_IAE        0x67u   /* active-empty current, 200 uV/LSB   */
#define DS2782_PARAM_AE40       0x68u   /* active empty at +40 C, cell model  */
#define DS2782_PARAM_RSNSP      0x69u   /* sense conductance, 1 mho/LSB       */
#define DS2782_PARAM_FULL40_M   0x6Au   /* full capacity at +40 C, 6.25 uVh   */
#define DS2782_PARAM_FULL40_L   0x6Bu
#define DS2782_PARAM_SLOPES     0x6Cu   /* 12 slope bytes, 0x6C..0x77         */
#define DS2782_PARAM_WIN_START  DS2782_PARAM_AC_M
#define DS2782_PARAM_WIN_LEN    22u     /* 0x62 .. 0x77 inclusive             */
#define DS2782_NUM_SLOPES       12u

/**
 * @brief Battery capacity model held in Parameter EEPROM block 1.
 *
 * Fields with a known physical scale are in physical units and are encoded
 * by the driver (round to nearest, range-checked). The cell-model shape
 * (AE40, slopes) is raw because it comes from cell characterisation in the
 * datasheet's normalised format. Every field is written as given: there is
 * no "0 = keep" convention, so a caller that wants to change one parameter
 * reads the current model with ds2782_read_capacity_config(), edits it and
 * programs it back.
 */
typedef struct {
    uint16_t full40_mah;     /* Full40: capacity at +40 C (0.4167 mAh/LSB)    */
    uint16_t aging_cap_mah;  /* AC: rated capacity for cycle-count aging      */
    uint16_t vchg_mv;        /* VCHG: VOLT above this ...                     */
    uint16_t imin_ma;        /* IMIN: ... and 0 < IAVG below this -> full     */
    uint16_t vae_mv;         /* VAE: VOLT below this ...                      */
    uint16_t iae_ma;         /* IAE: ... at discharge above this -> empty     */
    uint8_t  rsnsp_mho;      /* RSNSP: 1 / RSNS, used for RAAC/RSAC in mAh    */
    uint8_t  ae40_raw;       /* Active Empty at +40 C, fraction of Full40     */
    uint8_t  slopes_raw[DS2782_NUM_SLOPES]; /* Full, AE, SE x 3040/2030/1020/0010 */
} DS2782_Capacity_Config_t;

/**
 * Template capacity model for the flight pack: 2 x NCR14430A105 in parallel
 * (1050 mAh, 4.2 V CC-CV, 0.03 C cut-off, 2.50 V discharge cut-off per cell)
 * on a 15 mOhm sense resistor. Values are chosen to be exactly representable,
 * so a readback returns them unchanged.
 *
 *   Full40/AC  2100 mAh  pack nominal; AC = rated capacity per datasheet
 *   VCHG       4158 mV   just below the 4.2 V +/-0.03 V charge voltage
 *   IMIN         70 mA   just above the 63 mA pack charge cut-off
 *   VAE        3006 mV   matches the EPS SURVIVAL threshold (3000 mV)
 *   IAE          40 mA   TEMPLATE: empty is reached in SURVIVAL mode with
 *                        light loads, and active-empty detection requires a
 *                        discharge current above IAE. Replace with the
 *                        measured high-load current once it is known.
 *   RSNSP        67 mho  1 / 15 mOhm
 *   AE40, slopes  0      no cell characterisation yet (no temperature
 *                        compensation of the model)
 */
#define DS2782_PACK_2P_NCR14430_DEFAULTS {                                    \
    .full40_mah    = 2100u,                                                   \
    .aging_cap_mah = 2100u,                                                   \
    .vchg_mv       = 4158u,                                                   \
    .imin_ma       = 70u,                                                     \
    .vae_mv        = 3006u,                                                   \
    .iae_ma        = 40u,                                                     \
    .rsnsp_mho     = (uint8_t)((1000u + DS2782_RSNS_MOHM / 2u) / DS2782_RSNS_MOHM), \
    .ae40_raw      = 0u,                                                      \
    .slopes_raw    = { 0u },                                                  \
}

/**
 * @brief Read the DS2782 telemetry register window in a single I2C transaction.
 * @param hi2c I2C handle (expected: &hi2c1).
 * @param[out] out Buffer of DS2782_BURST_LEN bytes. out[0] holds the value of
 *                 register DS2782_REG_RAAC_M, out[15] holds DS2782_REG_ACR_L.
 * @return true on HAL_OK, false on any I2C failure or NULL argument.
 *
 * On failure, logs the HAL status and the I2C ErrorCode register to USART2.
 */
bool ds2782_burst_read(I2C_HandleTypeDef *hi2c, uint8_t out[DS2782_BURST_LEN]);

/**
 * @brief Ping the DS2782 with HAL_I2C_IsDeviceReady and log the result.
 *
 * Intended for one-shot use at task start to confirm wiring/address. Non-fatal:
 * the EPS task should keep running even if the probe fails so the failure mode
 * is observable through telemetry instead of locking the system.
 *
 * @param hi2c I2C handle (expected: &hi2c1).
 * @return true if the slave acknowledged at DS2782_I2C_ADDR_8BIT.
 */
bool ds2782_probe(I2C_HandleTypeDef *hi2c);

/**
 * @brief Read only the voltage registers (VOLT_M, VOLT_L) as a minimal sanity
 *        test. Independent of the full burst-read path.
 *
 * @param hi2c I2C handle (expected: &hi2c1).
 * @param[out] raw_voltage_out Right-shifted 11-bit unsigned value (4.88 mV/LSB).
 *             Decode to millivolts with DS2782_Compute_Voltage(), or directly:
 *             mV = raw * 488 / 100.
 * @return true on HAL_OK, false on any I2C failure. Logs error on failure.
 */
bool ds2782_read_voltage(I2C_HandleTypeDef *hi2c, uint16_t *raw_voltage_out);

/**
 * @brief Full telemetry snapshot: burst-read the register window and decode
 *        it into a Battery_Telemetry_t (sign extension, bit alignment).
 *
 * TEMP and VOLT are left-justified 11-bit values in a 16-bit word; the
 * signed 16-bit quantities (currents, ACR) are reassembled MSB-first.
 * The raw fields keep the DS2782 native scale factors (see Battery_Telemetry_t);
 * decode to physical units with the DS2782_Compute_*() functions below.
 *
 * @param hi2c I2C handle (expected: &hi2c1).
 * @param[out] telemetry_out Decoded snapshot.
 * @return true on success, false on any I2C failure or NULL argument.
 */
bool ds2782_read_telemetry(I2C_HandleTypeDef *hi2c, Battery_Telemetry_t *telemetry_out);

/* Raw-to-physical conversions (DS2782 scale factors, integer-only math). */
uint16_t DS2782_Compute_Voltage(const Battery_Telemetry_t *telemetry);     /* mV */
int16_t  DS2782_Compute_Current(const Battery_Telemetry_t *telemetry);     /* mA */
int16_t  DS2782_Compute_Temperature(const Battery_Telemetry_t *telemetry); /* °C */
uint32_t DS2782_Compute_Active_Capacity(const Battery_Telemetry_t *telemetry);    /* mAh, RAAC */
uint32_t DS2782_Compute_Standby_Capacity(const Battery_Telemetry_t *telemetry);   /* mAh, RSAC */
int16_t  DS2782_Compute_Accumulated_Charge(const Battery_Telemetry_t *telemetry); /* mAh, ACR  */

/* --- Capacity model (Parameter EEPROM block 1) ---
 *
 * All functions below share I2C1 with the telemetry read and must be called
 * from the task that owns the bus (the EPS task). */

/** @brief Outcome of ds2782_program_capacity(). */
typedef enum {
    DS2782_PROG_ERROR     = 0,   /* nothing usable stored, see log            */
    DS2782_PROG_WRITTEN   = 1,   /* model differed and was copied to EEPROM   */
    DS2782_PROG_UNCHANGED = 2,   /* model already stored, no EEPROM cycle     */
} DS2782_Prog_Result_t;

/**
 * @brief Program the capacity model into Parameter EEPROM block 1.
 *
 * Sequence: refuse if block 1 is locked; Recall the EEPROM into shadow RAM;
 * compare with the encoded model and return early if it is already stored
 * (no EEPROM cycle is spent, so the call is safe on every boot); write the
 * differing bytes one per transaction (the DS2782 ignores auto-incremented
 * writes past 0x4F); verify the shadow; Copy Data; wait for EEC to clear;
 * Recall and verify again so the check covers what actually persisted.
 *
 * EEPROM endurance is 50,000 copies: reprogramming on telecommand is fine,
 * reprogramming periodically with changing values is not.
 *
 * @param hi2c I2C handle (expected: &hi2c1).
 * @param cfg  Capacity model to store (e.g. DS2782_PACK_2P_NCR14430_DEFAULTS).
 * @return DS2782_PROG_WRITTEN or DS2782_PROG_UNCHANGED if EEPROM holds cfg
 *         on return (both non-zero, so the result can be tested as a bool);
 *         DS2782_PROG_ERROR on bus failure, locked block, out-of-range field
 *         or verification mismatch (all logged).
 */
DS2782_Prog_Result_t ds2782_program_capacity(I2C_HandleTypeDef *hi2c,
                                             const DS2782_Capacity_Config_t *cfg);

/**
 * @brief Read the capacity model stored in Parameter EEPROM block 1.
 *
 * Issues a Recall first so the result reflects EEPROM, not shadow RAM that
 * may hold uncommitted writes. Physical fields are rounded to nearest.
 *
 * @param hi2c I2C handle (expected: &hi2c1).
 * @param[out] cfg_out Decoded model.
 * @return true on success, false on any I2C failure or NULL argument.
 */
bool ds2782_read_capacity_config(I2C_HandleTypeDef *hi2c, DS2782_Capacity_Config_t *cfg_out);

/**
 * @brief Read the STATUS register (CHGTF, AEF, SEF, LEARNF, ...).
 * @return true on success, false on any I2C failure or NULL argument.
 */
bool ds2782_read_status(I2C_HandleTypeDef *hi2c, uint8_t *status_out);

/**
 * @brief Overwrite the Accumulated Current Register (present charge).
 *
 * Seeds the gauge with a known state of charge, for instance full40_mah
 * after a completed charge, instead of waiting for the first full detection
 * (CHGTF), at which the gauge sets ACR to the full model value by itself.
 * Writing ACR clears ACRL and LEARNF. ACR is backed up to EEPROM by the gauge.
 *
 * @param hi2c I2C handle (expected: &hi2c1).
 * @param charge_mah Charge to load, 0..13653 mAh at 15 mOhm (int16 ACR range).
 * @return true on success, false on I2C failure or out-of-range value.
 */
bool ds2782_set_accumulated_charge(I2C_HandleTypeDef *hi2c, uint16_t charge_mah);

/**
 * @brief Overwrite the Age Scalar (0x14), 2^-7 per LSB, 0x80 = 100 %.
 *
 * The datasheet recommends 95 % (0x7A) at first fit so the learn function
 * can grow the capacity of a cell better than nominal. AS carries the
 * cumulative aging estimate, so write it only on a fresh pack.
 *
 * @return true on success, false on I2C failure.
 */
bool ds2782_set_age_scalar(I2C_HandleTypeDef *hi2c, uint8_t as_raw);

#endif /* INC_SUBSYSTEMS_EPS_DS2782_H_ */
