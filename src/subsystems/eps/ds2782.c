/**
 * @file ds2782.c
 * @brief Low-level driver for the Maxim DS2782 stand-alone fuel gauge.
 */

#include "ds2782.h"
#include <stdio.h>
#include <string.h>

#define DS2782_I2C_TIMEOUT_MS   100u
#define DS2782_EEC_TIMEOUT_MS   20u     /* tEEC: 2 ms typ, 10 ms max */

/* Offsets into the Parameter EEPROM capacity window (0x62..0x77). */
#define PW(reg)                 ((reg) - DS2782_PARAM_WIN_START)

/* ── Bus helpers ─────────────────────────────────────────────────────────── */

static bool ds2782_read_regs(I2C_HandleTypeDef *hi2c, uint8_t reg,
                             uint8_t *buf, uint16_t len, const char *what)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
        hi2c, DS2782_I2C_ADDR_8BIT, reg, I2C_MEMADD_SIZE_8BIT,
        buf, len, DS2782_I2C_TIMEOUT_MS);

    if (status != HAL_OK) {
        printf("[DS2782] %s read fail: status=%d err=0x%lx\r\n",
               what, (int)status, (unsigned long)hi2c->ErrorCode);
        return false;
    }
    return true;
}

static bool ds2782_write_reg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t value)
{
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        hi2c, DS2782_I2C_ADDR_8BIT, reg, I2C_MEMADD_SIZE_8BIT,
        &value, 1, DS2782_I2C_TIMEOUT_MS);

    if (status != HAL_OK) {
        printf("[DS2782] write 0x%02X fail: status=%d err=0x%lx\r\n",
               (unsigned)reg, (int)status, (unsigned long)hi2c->ErrorCode);
        return false;
    }
    return true;
}

static bool ds2782_function_cmd(I2C_HandleTypeDef *hi2c, uint8_t cmd)
{
    return ds2782_write_reg(hi2c, DS2782_REG_FUNC_CMD, cmd);
}

/* Poll EEC until the Copy Data cycle has finished. Writes into block 1 are
 * ignored while EEC = 1, so nothing may follow a Copy before this returns. */
static bool ds2782_wait_eeprom_idle(I2C_HandleTypeDef *hi2c)
{
    uint32_t start = HAL_GetTick();
    uint8_t eeprom;

    for (;;) {
        if (!ds2782_read_regs(hi2c, DS2782_REG_EEPROM, &eeprom, 1, "EEPROM")) {
            return false;
        }
        if ((eeprom & DS2782_EEPROM_EEC) == 0u) {
            return true;
        }
        if ((HAL_GetTick() - start) > DS2782_EEC_TIMEOUT_MS) {
            printf("[DS2782] EEPROM copy timeout (EEC stuck)\r\n");
            return false;
        }
        HAL_Delay(1);
    }
}

/* ── Unit encoding (round to nearest, range-checked) ─────────────────────── */

static uint32_t div_round(uint32_t num, uint32_t den)
{
    return (num + den / 2u) / den;
}

/* Charge (ACR, AC, Full40): 6.25 uVh/LSB -> 6.25 / RSNS[mOhm] mAh/LSB. */
static uint32_t mah_to_raw(uint32_t mah)
{
    return div_round(mah * DS2782_RSNS_MOHM * 100u, 625u);
}

static uint16_t raw_to_mah(uint32_t raw)
{
    return (uint16_t)div_round(raw * 625u, DS2782_RSNS_MOHM * 100u);
}

/* VCHG, VAE: 19.52 mV/LSB. */
static uint32_t mv_to_raw(uint32_t mv)  { return div_round(mv * 100u, 1952u); }
static uint16_t raw_to_mv(uint32_t raw) { return (uint16_t)div_round(raw * 1952u, 100u); }

/* IMIN: 50 uV/LSB, IAE: 200 uV/LSB, both across RSNS. */
static uint32_t ma_to_raw(uint32_t ma, uint32_t uv_per_lsb)
{
    return div_round(ma * DS2782_RSNS_MOHM, uv_per_lsb);
}

static uint16_t raw_to_ma(uint32_t raw, uint32_t uv_per_lsb)
{
    return (uint16_t)div_round(raw * uv_per_lsb, DS2782_RSNS_MOHM);
}

static bool put_u8(uint8_t *dst, uint32_t raw, const char *name)
{
    if (raw > 0xFFu) {
        printf("[DS2782] %s out of range (raw=%lu > 255)\r\n", name, (unsigned long)raw);
        return false;
    }
    *dst = (uint8_t)raw;
    return true;
}

static bool put_u16(uint8_t *dst, uint32_t raw, const char *name)
{
    if (raw > 0xFFFFu) {
        printf("[DS2782] %s out of range (raw=%lu > 65535)\r\n", name, (unsigned long)raw);
        return false;
    }
    dst[0] = (uint8_t)(raw >> 8);   /* MSB at the lower address */
    dst[1] = (uint8_t)raw;
    return true;
}

static bool encode_capacity(const DS2782_Capacity_Config_t *cfg,
                            uint8_t win[DS2782_PARAM_WIN_LEN])
{
    bool ok = true;
    ok &= put_u16(&win[PW(DS2782_PARAM_AC_M)],     mah_to_raw(cfg->aging_cap_mah), "AC");
    ok &= put_u8 (&win[PW(DS2782_PARAM_VCHG)],     mv_to_raw(cfg->vchg_mv),        "VCHG");
    ok &= put_u8 (&win[PW(DS2782_PARAM_IMIN)],     ma_to_raw(cfg->imin_ma, 50u),   "IMIN");
    ok &= put_u8 (&win[PW(DS2782_PARAM_VAE)],      mv_to_raw(cfg->vae_mv),         "VAE");
    ok &= put_u8 (&win[PW(DS2782_PARAM_IAE)],      ma_to_raw(cfg->iae_ma, 200u),   "IAE");
    win[PW(DS2782_PARAM_AE40)]  = cfg->ae40_raw;
    win[PW(DS2782_PARAM_RSNSP)] = cfg->rsnsp_mho;
    ok &= put_u16(&win[PW(DS2782_PARAM_FULL40_M)], mah_to_raw(cfg->full40_mah),    "Full40");
    memcpy(&win[PW(DS2782_PARAM_SLOPES)], cfg->slopes_raw, DS2782_NUM_SLOPES);
    return ok;
}

static void decode_capacity(const uint8_t win[DS2782_PARAM_WIN_LEN],
                            DS2782_Capacity_Config_t *cfg)
{
    uint32_t ac     = ((uint32_t)win[PW(DS2782_PARAM_AC_M)] << 8)     | win[PW(DS2782_PARAM_AC_L)];
    uint32_t full40 = ((uint32_t)win[PW(DS2782_PARAM_FULL40_M)] << 8) | win[PW(DS2782_PARAM_FULL40_L)];

    cfg->aging_cap_mah = raw_to_mah(ac);
    cfg->vchg_mv       = raw_to_mv(win[PW(DS2782_PARAM_VCHG)]);
    cfg->imin_ma       = raw_to_ma(win[PW(DS2782_PARAM_IMIN)], 50u);
    cfg->vae_mv        = raw_to_mv(win[PW(DS2782_PARAM_VAE)]);
    cfg->iae_ma        = raw_to_ma(win[PW(DS2782_PARAM_IAE)], 200u);
    cfg->ae40_raw      = win[PW(DS2782_PARAM_AE40)];
    cfg->rsnsp_mho     = win[PW(DS2782_PARAM_RSNSP)];
    cfg->full40_mah    = raw_to_mah(full40);
    memcpy(cfg->slopes_raw, &win[PW(DS2782_PARAM_SLOPES)], DS2782_NUM_SLOPES);
}

/* Recall block 1 into shadow RAM and read the capacity window back. */
static bool recall_param_window(I2C_HandleTypeDef *hi2c, uint8_t win[DS2782_PARAM_WIN_LEN])
{
    if (!ds2782_function_cmd(hi2c, DS2782_CMD_RECALL_BLOCK1)) {
        return false;
    }
    return ds2782_read_regs(hi2c, DS2782_PARAM_WIN_START, win,
                            DS2782_PARAM_WIN_LEN, "param block");
}

/* ── Telemetry ───────────────────────────────────────────────────────────── */

bool ds2782_burst_read(I2C_HandleTypeDef *hi2c, uint8_t out[DS2782_BURST_LEN])
{
    if (hi2c == NULL || out == NULL) {
        return false;
    }
    return ds2782_read_regs(hi2c, DS2782_BURST_START, out, DS2782_BURST_LEN, "burst");
}

bool ds2782_read_voltage(I2C_HandleTypeDef *hi2c, uint16_t *raw_voltage_out)
{
    if (hi2c == NULL || raw_voltage_out == NULL) {
        return false;
    }

    uint8_t buf[2];
    if (!ds2782_read_regs(hi2c, DS2782_REG_VOLT_M, buf, 2, "voltage")) {
        return false;
    }

    *raw_voltage_out = (uint16_t)((((uint16_t)buf[0] << 8) | buf[1]) >> 5);
    return true;
}

bool ds2782_read_telemetry(I2C_HandleTypeDef *hi2c, Battery_Telemetry_t *telemetry_out)
{
    if (telemetry_out == NULL) {
        return false;
    }

    uint8_t raw[DS2782_BURST_LEN];
    if (!ds2782_burst_read(hi2c, raw)) {
        return false;
    }

    /* Indexing convention: raw[i] holds register (0x02 + i). */
    telemetry_out->raw_active_abs_cap  = ((uint16_t)raw[0] << 8) | raw[1];
    telemetry_out->raw_standby_abs_cap = ((uint16_t)raw[2] << 8) | raw[3];
    telemetry_out->raw_relative_cap    =  raw[4];
    telemetry_out->raw_standby_rel_cap =  raw[5];
    telemetry_out->raw_avg_current     = (int16_t)(((uint16_t)raw[6] << 8) | raw[7]);
    /* TEMP and VOLT are 11-bit conversions stored left-justified in the
     * 16-bit register pair (low 5 bits are don't-care), hence the shift.
     * TEMP is two's complement: arithmetic shift preserves the sign. */
    telemetry_out->raw_temperature     = (int16_t)(((uint16_t)raw[8] << 8) | raw[9]) >> 5;
    telemetry_out->raw_voltage         = (uint16_t)((((uint16_t)raw[10] << 8) | raw[11]) >> 5);
    telemetry_out->raw_current         = (int16_t)(((uint16_t)raw[12] << 8) | raw[13]);
    telemetry_out->raw_acr             = (int16_t)(((uint16_t)raw[14] << 8) | raw[15]);

    return true;
}

uint16_t DS2782_Compute_Voltage(const Battery_Telemetry_t *telemetry) {
    // millivolts: raw * 4.88 mV/LSB (max raw 1023 -> 4992 mV, fits uint16_t)
    return (uint16_t)(((uint32_t)telemetry->raw_voltage * 488u) / 100u);
}

int16_t DS2782_Compute_Current(const Battery_Telemetry_t *telemetry) {
    // milliamps: raw * 1.5625 uV / 15 mOhm = raw * 0.10417 mA (truncates toward zero)
    return (int16_t)(((int32_t)telemetry->raw_current * 15625) /
                     (int32_t)(DS2782_RSNS_MOHM * 10000u));
}

int16_t DS2782_Compute_Temperature(const Battery_Telemetry_t *telemetry) {
    // degrees Celsius: raw * 0.125 C/LSB (truncates toward zero)
    return (int16_t)(telemetry->raw_temperature / 8);
}

uint32_t DS2782_Compute_Active_Capacity(const Battery_Telemetry_t *telemetry) {
    // milliamp-hours: raw * 1.6 mAh/LSB (RSNSP already applied by the gauge)
    return ((uint32_t)telemetry->raw_active_abs_cap * 16u) / 10u;
}

uint32_t DS2782_Compute_Standby_Capacity(const Battery_Telemetry_t *telemetry) {
    return ((uint32_t)telemetry->raw_standby_abs_cap * 16u) / 10u;
}

int16_t DS2782_Compute_Accumulated_Charge(const Battery_Telemetry_t *telemetry) {
    // milliamp-hours: raw * 6.25 uVh / RSNS (truncates toward zero)
    return (int16_t)(((int32_t)telemetry->raw_acr * 625) /
                     (int32_t)(DS2782_RSNS_MOHM * 100u));
}

/* ── Capacity model ──────────────────────────────────────────────────────── */

DS2782_Prog_Result_t ds2782_program_capacity(I2C_HandleTypeDef *hi2c, const DS2782_Capacity_Config_t *cfg)
{
    if (hi2c == NULL || cfg == NULL) {
        return DS2782_PROG_ERROR;
    }

    uint8_t want[DS2782_PARAM_WIN_LEN];
    if (!encode_capacity(cfg, want)) {
        return DS2782_PROG_ERROR;
    }

    /* A locked block silently ignores writes and copies; report it instead. */
    uint8_t eeprom;
    if (!ds2782_read_regs(hi2c, DS2782_REG_EEPROM, &eeprom, 1, "EEPROM")) {
        return DS2782_PROG_ERROR;
    }
    if (eeprom & DS2782_EEPROM_BL1) {
        printf("[DS2782] param block 1 is locked, cannot program\r\n");
        return DS2782_PROG_ERROR;
    }

    /* Start from what EEPROM holds, not from stale shadow RAM. */
    uint8_t have[DS2782_PARAM_WIN_LEN];
    if (!recall_param_window(hi2c, have)) {
        return DS2782_PROG_ERROR;
    }
    if (memcmp(have, want, sizeof want) == 0) {
        printf("[DS2782] capacity model already programmed\r\n");
        return DS2782_PROG_UNCHANGED;
    }

    /* One byte per transaction: the gauge drops auto-incremented writes
     * beyond 0x4F, and block 1 lives at 0x60..0x7F. Ascending order writes
     * the MSB of AC and Full40 before the LSB. */
    for (uint8_t i = 0; i < DS2782_PARAM_WIN_LEN; i++) {
        if (have[i] != want[i] &&
            !ds2782_write_reg(hi2c, (uint8_t)(DS2782_PARAM_WIN_START + i), want[i])) {
            return DS2782_PROG_ERROR;
        }
    }

    if (!ds2782_read_regs(hi2c, DS2782_PARAM_WIN_START, have,
                          DS2782_PARAM_WIN_LEN, "param block")) {
        return DS2782_PROG_ERROR;
    }
    if (memcmp(have, want, sizeof want) != 0) {
        printf("[DS2782] shadow RAM verify mismatch, not copying\r\n");
        return DS2782_PROG_ERROR;
    }

    if (!ds2782_function_cmd(hi2c, DS2782_CMD_COPY_BLOCK1) ||
        !ds2782_wait_eeprom_idle(hi2c)) {
        return DS2782_PROG_ERROR;
    }

    /* Verify what persisted: Recall overwrites the shadow with EEPROM. */
    if (!recall_param_window(hi2c, have)) {
        return DS2782_PROG_ERROR;
    }
    if (memcmp(have, want, sizeof want) != 0) {
        printf("[DS2782] EEPROM verify mismatch after copy\r\n");
        return DS2782_PROG_ERROR;
    }

    printf("[DS2782] capacity model programmed: Full40=%u mAh AC=%u mAh "
           "VCHG=%u mV IMIN=%u mA VAE=%u mV IAE=%u mA RSNSP=%u\r\n",
           (unsigned)cfg->full40_mah, (unsigned)cfg->aging_cap_mah,
           (unsigned)cfg->vchg_mv, (unsigned)cfg->imin_ma,
           (unsigned)cfg->vae_mv, (unsigned)cfg->iae_ma,
           (unsigned)cfg->rsnsp_mho);
    return DS2782_PROG_WRITTEN;
}

bool ds2782_read_capacity_config(I2C_HandleTypeDef *hi2c, DS2782_Capacity_Config_t *cfg_out)
{
    if (hi2c == NULL || cfg_out == NULL) {
        return false;
    }

    uint8_t win[DS2782_PARAM_WIN_LEN];
    if (!recall_param_window(hi2c, win)) {
        return false;
    }
    decode_capacity(win, cfg_out);
    return true;
}

bool ds2782_read_status(I2C_HandleTypeDef *hi2c, uint8_t *status_out)
{
    if (hi2c == NULL || status_out == NULL) {
        return false;
    }
    return ds2782_read_regs(hi2c, DS2782_REG_STATUS, status_out, 1, "status");
}

bool ds2782_set_accumulated_charge(I2C_HandleTypeDef *hi2c, uint16_t charge_mah)
{
    if (hi2c == NULL) {
        return false;
    }

    uint32_t raw = mah_to_raw(charge_mah);
    if (raw > 0x7FFFu) {
        printf("[DS2782] ACR out of range (%u mAh)\r\n", (unsigned)charge_mah);
        return false;
    }

    /* MSB first, both bytes within one 3.515 s ACR update period. */
    return ds2782_write_reg(hi2c, DS2782_REG_ACR_M, (uint8_t)(raw >> 8)) &&
           ds2782_write_reg(hi2c, DS2782_REG_ACR_L, (uint8_t)raw);
}

bool ds2782_set_age_scalar(I2C_HandleTypeDef *hi2c, uint8_t as_raw)
{
    if (hi2c == NULL) {
        return false;
    }
    return ds2782_write_reg(hi2c, DS2782_REG_AS, as_raw);
}

bool ds2782_probe(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL) {
        return false;
    }

    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(
        hi2c, DS2782_I2C_ADDR_8BIT, 3, DS2782_I2C_TIMEOUT_MS);

    printf("[DS2782] probe @0x%02X: %s (status=%d, err=0x%lx)\r\n",
           (unsigned)DS2782_I2C_ADDR_8BIT,
           (status == HAL_OK) ? "ACK" : "NO ACK",
           (int)status,
           (unsigned long)hi2c->ErrorCode);

    /* If the expected address NACKs, scan the bus so the failure isn't
     * ambiguous. Any address that ACKs is printed in 8-bit (HAL-shifted) form
     * so the value can be compared directly against DS2782_I2C_ADDR_8BIT. */
    if (status != HAL_OK) {
        printf("[DS2782] bus scan (8-bit addrs):");
        bool any = false;
        for (uint8_t a = 1u; a < 128u; a++) {
            uint16_t dev = (uint16_t)(a << 1);
            if (HAL_I2C_IsDeviceReady(hi2c, dev, 1, 5u) == HAL_OK) {
                printf(" 0x%02X", (unsigned)dev);
                any = true;
            }
        }
        printf("%s\r\n", any ? "" : " (no devices responding)");
    }

    return (status == HAL_OK);
}
