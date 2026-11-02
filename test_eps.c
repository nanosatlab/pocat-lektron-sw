/**
 * @file test_eps.c
 * @brief On-target EPS test firmware: substitute main + unit tests + e2e scenarios.
 *
 * Built only by -DEPS_TESTS=ON (./build.sh --tests) as pocat_eps_tests.elf,
 * replacing src/core/main.c. Runs the REAL FreeRTOS, HAL, eps_task and
 * obdh_task. Output: printf -> USART2 (log.c), i.e. the normal serial monitor.
 *
 * Three suites run in one image, with no source edits between setups:
 *   - unit tests and e2e scenarios on injected DS2782/LTC4040 data, so their
 *     verdict does not depend on what is wired;
 *   - a hardware checkout against the real fuel gauge and PMIC. It detects the
 *     DS2782 on I2C1 and reports every hardware test as SKIP when it is
 *     absent. When present it programs and verifies the capacity model and
 *     measures the values the model still needs (load current for IAE,
 *     charge-end current for IMIN). Measurements are listed in the final
 *     summary.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"

#include "eps.h"
#include "ds2782.h"   /* the DS2782_Compute_* unit tests exercise the driver directly */
#include "obdh.h"
#include "flash.h"
#include "notifications.h"
#include "events.h"
#include "clock.h"
#include "periph.h"
#include "log.h"
#include "obc.h"   /* EPS/OBDH stack sizes and priorities */

/* ════════════════════════ 1. Test harness ═══════════════════════════════ */

volatile int g_failures = 0;
volatile int g_asserts  = 0;

/* Always prints expected vs actual — use for every assertion you want to inspect. */
#define TEST_CHECK(label, expected, actual)                                    \
    do {                                                                       \
        g_asserts++;                                                           \
        long _e = (long)(expected), _a = (long)(actual);                       \
        if (_e != _a) {                                                        \
            g_failures++;                                                      \
            printf("  [FAIL] %-42s exp=%-8ld got=%ld\r\n", label, _e, _a);    \
        } else {                                                               \
            printf("  [ok]   %-42s exp=%-8ld got=%ld\r\n", label, _e, _a);    \
        }                                                                      \
    } while (0)

/* Always prints for boolean / condition checks. */
#define TEST_CHECK_BOOL(label, cond)                                           \
    do {                                                                       \
        g_asserts++;                                                           \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("  [FAIL] %s\r\n", label);                                  \
        } else {                                                               \
            printf("  [ok]   %s\r\n", label);                                  \
        }                                                                      \
    } while (0)

/* Range check, prints the accepted interval and the actual value. */
#define TEST_RANGE(label, lo, hi, actual)                                      \
    do {                                                                       \
        g_asserts++;                                                           \
        long _l = (long)(lo), _h = (long)(hi), _a = (long)(actual);            \
        if (_a < _l || _a > _h) {                                              \
            g_failures++;                                                      \
            printf("  [FAIL] %-42s exp=[%ld..%ld] got=%ld\r\n", label, _l, _h, _a); \
        } else {                                                               \
            printf("  [ok]   %-42s exp=[%ld..%ld] got=%ld\r\n", label, _l, _h, _a); \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                       \
        int _before = g_failures;                                              \
        printf("[TEST] %s\r\n", #fn);                                          \
        fn();                                                                  \
        printf("       %s\r\n", (_before == g_failures) ? "[PASS]" : "[FAIL]");\
    } while (0)

/* Hardware checkout state and measurements, reported by tests_done(). */
static struct {
    bool     present;          /* DS2782 answered on I2C1                      */
    int      skipped;
    int      first_program;    /* DS2782_Prog_Result_t of the first program    */
    bool     full_est_valid;
    uint32_t full_est_mah;     /* RAAC / RARC: capacity the gauge assumes now  */
    bool     load_valid;       /* load profile ran with the input power absent */
    int16_t  load_ma[2];       /* mean discharge, heater off / on              */
    int16_t  load_peak_ma;
    uint16_t iae_candidate_ma;
    bool     charging_seen;
    bool     charge_end_seen;
    int16_t  charge_end_ma;    /* instantaneous current when !CHRG released    */
    uint16_t imin_candidate_ma;
    bool     chgtf_seen;       /* gauge full detection fired during the run    */
} g_hw;

#define RUN_HW_TEST(fn)                                                        \
    do {                                                                       \
        if (g_hw.present) {                                                    \
            RUN_TEST(fn);                                                      \
        } else {                                                               \
            g_hw.skipped++;                                                    \
            printf("[TEST] %s\r\n       [SKIP] no DS2782 on I2C1\r\n", #fn);   \
        }                                                                      \
    } while (0)

static void print_measurements(void)
{
    const DS2782_Capacity_Config_t tmpl = DS2782_PACK_2P_NCR14430_DEFAULTS;

    printf(" HARDWARE: %s", g_hw.present ? "DS2782 detected" : "DS2782 NOT detected");
    if (g_hw.skipped) printf(" (%d hardware tests skipped)", g_hw.skipped);
    printf("\r\n");
    if (!g_hw.present) return;

    printf(" MEASUREMENTS (inputs for DS2782_Capacity_Config_t):\r\n");
    printf("   model at start      : %s\r\n",
           g_hw.first_program == DS2782_PROG_UNCHANGED ? "already stored (EEPROM kept it across reset)" :
           g_hw.first_program == DS2782_PROG_WRITTEN   ? "written this run (first program of this gauge)" :
                                                         "program failed");
    if (g_hw.full_est_valid)
        printf("   effective capacity  : %lu mAh (RAAC/RARC; template Full40 %u mAh)\r\n",
               (unsigned long)g_hw.full_est_mah, (unsigned)tmpl.full40_mah);
    else
        printf("   effective capacity  : n/a (RARC = 0, gauge has no charge reference yet)\r\n");

    if (g_hw.load_valid)
        printf("   load on battery     : %d mA heater off, %d mA heater on, peak %d mA"
               " -> IAE candidate %u mA (template %u mA)\r\n",
               g_hw.load_ma[0], g_hw.load_ma[1], g_hw.load_peak_ma,
               (unsigned)g_hw.iae_candidate_ma, (unsigned)tmpl.iae_ma);
    else
        printf("   load on battery     : not measured, input power present"
               " (remove solar/USB input to measure IAE)\r\n");

    if (g_hw.charge_end_seen)
        printf("   charge end          : charger stopped at %d mA -> IMIN candidate %u mA"
               " (template %u mA)\r\n",
               g_hw.charge_end_ma, (unsigned)g_hw.imin_candidate_ma, (unsigned)tmpl.imin_ma);
    else
        printf("   charge end          : not observed (%s)\r\n",
               g_hw.charging_seen ? "still charging when the watch window closed"
                                  : "charger not active");
    printf("   gauge full detect   : CHGTF %s\r\n",
           g_hw.chgtf_seen ? "set (VCHG/IMIN detect full)" : "not seen this run");
}

static void tests_done(void)
{
    printf("\r\n==============================================\r\n");
    printf(" EPS TESTS DONE: %d asserts, %d failures -> %s\r\n",
           g_asserts, g_failures, g_failures ? "FAIL" : "ALL PASS");
    print_measurements();
    printf("==============================================\r\n");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

/* ════════════════════════ 2. Helpers ═════════════════════════════════════ */

static TaskHandle_t eps_handle;

/** Wait until the EPS task completes n more full cycles (sampling-period agnostic). */
static bool wait_cycles(uint32_t n, uint32_t timeout_ms)
{
    uint32_t   start = eps_cycle_count;
    TickType_t t0    = xTaskGetTickCount();
    while ((eps_cycle_count - start) < n) {
        if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(timeout_ms))
            return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

/* Reference battery telemetry: 2100 mAh pack at 3.9 V, 25 C, 80 % SoC */
static Battery_Telemetry_t nominal_battery(void)
{
    Battery_Telemetry_t b = {
        .raw_voltage          = 799,   /* 799 * 488/100  = 3899 mV */
        .raw_current          = 768,   /* 768 / 9.6      = 80 mA — battery charging */
        .raw_temperature      = 200,   /* 200 / 8        = 25 C    */
        .raw_relative_cap     = 80,    /* 80 % (RARC) */
        .raw_avg_current      = 640,   /* 640 / 9.6      = 66 mA — IAVG, 28 s window */
        .raw_acr              = 4012,  /* 4012 * 0.41875 = 1680 mAh (ACR x RSNSP) */
        .raw_active_abs_cap   = 1050,  /* 1050 * 1.6 = 1680 mAh — RAAC, 80 % of 2100 */
        .raw_standby_abs_cap  = 1076,  /* 1076 * 1.6 = 1722 mAh — RSAC, 82 % of 2100 */
        .raw_standby_rel_cap  = 82,    /* 82 % — RSRC */
    };
    return b;
}

static EPS_Status_t sunlit_pmic(void)
{
    EPS_Status_t p = {
        .is_charging = true, .has_fault = false,
        .is_eclipse = false, .charging_disabled = false,
        .raw_clprog_adc = 2048,
    };
    return p;
}

/** Inject battery telemetry with a given voltage (mV) and temperature (C). */
static void set_battery(uint16_t mv, int16_t temp_c)
{
    Battery_Telemetry_t b = nominal_battery();
    b.raw_voltage     = (uint16_t)(((uint32_t)mv * 100u + 244u) / 488u);
    b.raw_temperature = (int16_t)(temp_c * 8);
    DS2782_Set_Mock_Values(&b);
}

/** Force the battery state machine into a specific state for hysteresis tests. */
static void set_battery_state(uint32_t state)
{
    xEventGroupClearBits(batteryStatus, ALL_BATTERY_STATES);
    xEventGroupSetBits(batteryStatus, state);
}

/** Read the telemetry mailbox back from real flash through the OBDH task. */
static void read_mailbox(uint8_t out[sizeof(EPS_Telemetry_Frame_t)])
{
    memset(out, 0, sizeof(EPS_Telemetry_Frame_t));
    OBDH_Read_Request(OBDH_EPS_TELEMETRY_ADDR, out, sizeof(EPS_Telemetry_Frame_t));
}

static void dump_mailbox(const char *label)
{
    static const char *field_names[19] = {
        "vbat_raw[0]",        "vbat_raw[1]",
        "temp_raw[0]",        "temp_raw[1]",
        "rel_cap_raw",
        "current_raw[0]",     "current_raw[1]",
        "avg_current_raw[0]", "avg_current_raw[1]",
        "acr_raw[0]",         "acr_raw[1]",
        "aac_raw[0]",         "aac_raw[1]",
        "sac_raw[0]",         "sac_raw[1]",
        "rsrc_raw",
        "clprog_adc[0]",      "clprog_adc[1]",
        "system_status",
    };
    uint8_t mailbox[sizeof(EPS_Telemetry_Frame_t)];
    read_mailbox(mailbox);
    printf("  [mailbox @ %s]\r\n", label);
    for (int i = 0; i < 19; i++)
        printf("    [%02d] %-24s = 0x%02X (%3d)\r\n", i, field_names[i], mailbox[i], mailbox[i]);
}

/* ════════════════════════ 3. Unit tests ══════════════════════════════════ */

static void test_compute_voltage(void)
{
    /* Verifies: DS2782_Compute_Voltage() converts raw ADC to millivolts.
     * Formula: voltage_mv = (raw * 488) / 100  (4.88 mV/LSB)
     * BREAKPOINT: step into DS2782_Compute_Voltage(), watch the integer math. */
    Battery_Telemetry_t s = {0};
    uint16_t v;
    s.raw_voltage = 782;  v = DS2782_Compute_Voltage(&s); TEST_CHECK("voltage mV (raw=782  -> 3816)", 3816, v);
    s.raw_voltage = 0;    v = DS2782_Compute_Voltage(&s); TEST_CHECK("voltage mV (raw=0    -> 0)",    0,    v);
    s.raw_voltage = 1023; v = DS2782_Compute_Voltage(&s); TEST_CHECK("voltage mV (raw=1023 -> 4992)", 4992, v);
}

static void test_compute_current(void)
{
    /* Verifies: DS2782_Compute_Current() converts raw current to milliamperes.
     * Formula: current_ma = raw * 1.5625 uV / 15 mOhm = raw / 9.6 (signed, truncates)
     * BREAKPOINT: step into DS2782_Compute_Current(), inspect negative-raw path. */
    Battery_Telemetry_t s = {0};
    int16_t c;
    s.raw_current = 1000;  c = DS2782_Compute_Current(&s); TEST_CHECK("current mA (raw=1000  ->  104)",  104,  c);
    s.raw_current = -500;  c = DS2782_Compute_Current(&s); TEST_CHECK("current mA (raw=-500  ->  -52)", -52,   c);
    s.raw_current = 0;     c = DS2782_Compute_Current(&s); TEST_CHECK("current mA (raw=0     ->    0)",   0,   c);
    s.raw_current = 32767; c = DS2782_Compute_Current(&s); TEST_CHECK("current mA (raw=32767 -> 3413)", 3413, c);
}

static void test_compute_temperature(void)
{
    /* Verifies: DS2782_Compute_Temperature() converts raw ADC to degrees Celsius.
     * Formula: temp_c = raw / 8  (0.125 C/LSB, truncates toward zero)
     * BREAKPOINT: step into DS2782_Compute_Temperature(), check negative raw. */
    Battery_Telemetry_t s = {0};
    int16_t t;
    s.raw_temperature = 160; t = DS2782_Compute_Temperature(&s); TEST_CHECK("temp C (raw= 160 ->  20)",  20, t);
    s.raw_temperature = -80; t = DS2782_Compute_Temperature(&s); TEST_CHECK("temp C (raw=-80  -> -10)", -10, t);
    s.raw_temperature = 0;   t = DS2782_Compute_Temperature(&s); TEST_CHECK("temp C (raw=0    ->   0)",   0, t);
}

static void test_unit_consistency(void)
{
    /* Verifies: Compute_Voltage() output is in mV — the same unit as thresholds.
     * This catches the bug class where voltage is in V but thresholds are in mV
     * (3.816 V < 3700 mV would wrongly classify as SURVIVAL).
     * BREAKPOINT: check that mv = 3816, not 3 or 4 — it must dwarf the threshold. */
    const uint16_t th[3] = {3700, 3300, 3000};
    EPS_Test_Set_Thresholds_mV(th);
    Battery_Telemetry_t s = {0};
    s.raw_voltage = 782;
    uint16_t mv = DS2782_Compute_Voltage(&s);
    TEST_CHECK("voltage output is 3816 (mV, not 3 or 4)",          3816, mv);
    TEST_CHECK_BOOL("3816 mV > 3700 mV nominal threshold (units match)", mv > th[0]);
}

/* Payload layout tripwire — checked at compile time, not at runtime. */
_Static_assert(sizeof(EPS_Telemetry_Frame_t) == 19, "EPS_Telemetry_Frame_t size changed");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, vbat_raw)        ==  0, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, temp_raw)        ==  2, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, rel_cap_raw)     ==  4, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, current_raw)     ==  5, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, avg_current_raw) ==  7, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, acr_raw)         ==  9, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, aac_raw)         == 11, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, sac_raw)         == 13, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, rsrc_raw)        == 15, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, clprog_adc)      == 16, "layout");
_Static_assert(offsetof(EPS_Telemetry_Frame_t, system_status)   == 18, "layout");

static void test_payload_bytes(void)
{
    /* Verifies: eps_frame_pack() writes each field at the right byte offset
     * in little-endian order. This is the non-circular check — inputs and expected
     * bytes are both hardcoded independently of Pack.
     * BREAKPOINT: step through eps_frame_pack(), watch each field being written. */
    Battery_Telemetry_t battery = {0};
    EPS_Status_t        status  = {0};
    EPS_Telemetry_Frame_t      payload;
    memset(&payload, 0, sizeof payload);

    battery.raw_voltage         = 0x1234;
    battery.raw_temperature     = 0x001A;
    battery.raw_relative_cap    = 85;             /* 0x55 */
    battery.raw_current         = (int16_t)0xFC18; /* -1000 — exercises sign bits */
    battery.raw_avg_current     = (int16_t)0xFE0C; /* -500 */
    battery.raw_acr             = (int16_t)0x00C8; /*  200 */
    battery.raw_active_abs_cap  = 0xABCD;
    battery.raw_standby_abs_cap = 0xDEAD;
    battery.raw_standby_rel_cap = 90;             /* 0x5A */
    status.raw_clprog_adc       = 0xBCDE;
    status.is_charging = true;                    /* bit0 */
    status.is_eclipse  = true;                    /* bit2 -> system_status = 0x05 */

    eps_frame_pack(&battery, &status, &payload);

    static const char *byte_labels[19] = {
        "vbat_raw        lo", "vbat_raw        hi",
        "temp_raw        lo", "temp_raw        hi",
        "rel_cap_raw",
        "current_raw     lo", "current_raw     hi",
        "avg_current_raw lo", "avg_current_raw hi",
        "acr_raw         lo", "acr_raw         hi",
        "aac_raw         lo", "aac_raw         hi",
        "sac_raw         lo", "sac_raw         hi",
        "rsrc_raw",
        "clprog_adc      lo", "clprog_adc      hi",
        "system_status",
    };
    const uint8_t expected[19] = {
        0x34, 0x12,  0x1A, 0x00,  0x55,
        0x18, 0xFC,  0x0C, 0xFE,  0xC8, 0x00,
        0xCD, 0xAB,  0xAD, 0xDE,  0x5A,
        0xDE, 0xBC,  0x05,
    };
    const uint8_t *bytes = (const uint8_t *)&payload;
    for (int i = 0; i < 19; i++) {
        g_asserts++;
        if (bytes[i] != expected[i]) {
            g_failures++;
            printf("  [FAIL] byte[%02d] %-16s exp=0x%02X got=0x%02X\r\n",
                   i, byte_labels[i], expected[i], bytes[i]);
        } else {
            printf("  [ok]   byte[%02d] %-16s exp=0x%02X got=0x%02X\r\n",
                   i, byte_labels[i], expected[i], bytes[i]);
        }
    }
}

static void test_status_bits(void)
{
    /* Verifies: each boolean field in EPS_Status_t maps to exactly its own bit
     * in system_status. Fields are set one at a time; expect exactly one bit set.
     * BREAKPOINT: step through the boolean packing section of eps_frame_pack(). */
    static const char *bit_labels[8] = {
        "bit0 is_charging       -> 0x01",
        "bit1 has_fault         -> 0x02",
        "bit2 is_eclipse/!PFO   -> 0x04",
        "bit3 charging_disabled -> 0x08",
        "bit4 auto_heater       -> 0x10",
        "bit5 heater_state      -> 0x20",
        "bit6 batt_read_fail    -> 0x40",
        "bit7 pmic_read_fail    -> 0x80",
    };
    static const size_t offsets[8] = {
        offsetof(EPS_Status_t, is_charging),
        offsetof(EPS_Status_t, has_fault),
        offsetof(EPS_Status_t, is_eclipse),
        offsetof(EPS_Status_t, charging_disabled),
        offsetof(EPS_Status_t, auto_heater_enabled),
        offsetof(EPS_Status_t, heater_state),
        offsetof(EPS_Status_t, battery_read_failure),
        offsetof(EPS_Status_t, pmic_read_failure),
    };
    Battery_Telemetry_t battery = {0};
    for (int bit = 0; bit < 8; bit++) {
        EPS_Status_t   status = {0};
        EPS_Telemetry_Frame_t payload;
        *((bool *)((uint8_t *)&status + offsets[bit])) = true;
        eps_frame_pack(&battery, &status, &payload);
        TEST_CHECK(bit_labels[bit], (long)(1u << bit), (long)payload.system_status);
    }
}

static void test_pack_null_safety(void)
{
    /* Verifies: eps_frame_pack() does not crash or write when given NULL inputs.
     * BREAKPOINT: step into eps_frame_pack() — should hit the NULL guard and return. */
    EPS_Telemetry_Frame_t payload;
    memset(&payload, 0xAB, sizeof payload);
    eps_frame_pack(NULL, NULL, &payload);
    TEST_CHECK("payload byte[0] unchanged after NULL inputs (0xAB)", 0xAB, ((uint8_t *)&payload)[0]);
}

static void test_state_machine_boundaries(void)
{
    /* Verifies: EPS_Update_System_State() sets exactly one state bit per voltage.
     * Thresholds: nominal=3700, contingency=3300, sunsafe=3000 mV.
     * EV_BAT_NOMINAL=0x1, CONTINGENCY=0x2, SUNSAFE=0x4, SURVIVAL=0x8
     * BREAKPOINT: step into EPS_Update_System_State(), watch state_bitmask selection. */
    const uint16_t th[3] = {3700, 3300, 3000};
    EPS_Test_Set_Thresholds_mV(th);

    struct { uint16_t mv; uint32_t expect; const char *label; } cases[] = {
        {4200, EV_BAT_NOMINAL,     "4200mV -> NOMINAL     (0x01)"},
        {3700, EV_BAT_NOMINAL,     "3700mV -> NOMINAL     (0x01) on boundary"},
        {3699, EV_BAT_CONTINGENCY, "3699mV -> CONTINGENCY (0x02)"},
        {3300, EV_BAT_CONTINGENCY, "3300mV -> CONTINGENCY (0x02) on boundary"},
        {3299, EV_BAT_SUNSAFE,     "3299mV -> SUNSAFE     (0x04)"},
        {3100, EV_BAT_SUNSAFE,     "3100mV -> SUNSAFE     (0x04) B2 regression"},
        {3000, EV_BAT_SUNSAFE,     "3000mV -> SUNSAFE     (0x04) on boundary"},
        {2999, EV_BAT_SURVIVAL,    "2999mV -> SURVIVAL    (0x08)"},
        {2500, EV_BAT_SURVIVAL,    "2500mV -> SURVIVAL    (0x08)"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        EPS_Update_System_State(cases[i].mv);
        uint32_t bits = xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES;
        TEST_CHECK(cases[i].label, (long)cases[i].expect, (long)bits);
    }
}

static void test_hysteresis_rising_blocks(void)
{
    /* Verifies: rising transitions require crossing threshold + EPS_HYSTERESIS_MV (50 mV).
     * From CONTINGENCY, voltage at exactly 3700 mV (the nominal boundary) must NOT
     * promote to NOMINAL; only 3750 mV does.
     * BREAKPOINT: step into EPS_Update_System_State and inspect the rising checks. */
    const uint16_t th[3] = {3700, 3300, 3000};
    EPS_Test_Set_Thresholds_mV(th);

    set_battery_state(EV_BAT_CONTINGENCY);
    EPS_Update_System_State(3700);
    TEST_CHECK("3700 mV from CONTINGENCY -> blocked (need 3750)",
               EV_BAT_CONTINGENCY,
               xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES);

    EPS_Update_System_State(3749);
    TEST_CHECK("3749 mV from CONTINGENCY -> still blocked (need 3750)",
               EV_BAT_CONTINGENCY,
               xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES);

    EPS_Update_System_State(3750);
    TEST_CHECK("3750 mV from CONTINGENCY -> NOMINAL (crossed hysteresis)",
               EV_BAT_NOMINAL,
               xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES);
}

static void test_hysteresis_falling_immediate(void)
{
    /* Verifies: falling transitions happen at the bare threshold (no hysteresis).
     * From NOMINAL, voltage just below the nominal threshold falls to CONTINGENCY
     * immediately, without any margin requirement.
     * BREAKPOINT: step into EPS_Update_System_State and check the falling cascade. */
    const uint16_t th[3] = {3700, 3300, 3000};
    EPS_Test_Set_Thresholds_mV(th);

    set_battery_state(EV_BAT_NOMINAL);
    EPS_Update_System_State(3699);
    TEST_CHECK("3699 mV from NOMINAL -> immediate fall to CONTINGENCY",
               EV_BAT_CONTINGENCY,
               xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES);
}

static void test_falling_multi_step(void)
{
    /* Verifies: a catastrophic voltage drop can skip intermediate states in one call.
     * This protects the battery in the event of sudden load step or cell failure
     * — the state machine reacts in one cycle rather than three.
     * BREAKPOINT: step into EPS_Update_System_State and watch the cascade fire all
     * the way down to SURVIVAL in a single invocation. */
    const uint16_t th[3] = {3700, 3300, 3000};
    EPS_Test_Set_Thresholds_mV(th);

    set_battery_state(EV_BAT_NOMINAL);
    EPS_Update_System_State(2900);
    TEST_CHECK("2900 mV from NOMINAL -> skipped to SURVIVAL in one call",
               EV_BAT_SURVIVAL,
               xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES);

    set_battery_state(EV_BAT_CONTINGENCY);
    EPS_Update_System_State(2900);
    TEST_CHECK("2900 mV from CONTINGENCY -> skipped to SURVIVAL in one call",
               EV_BAT_SURVIVAL,
               xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES);

    set_battery_state(EV_BAT_NOMINAL);
    EPS_Update_System_State(3100);
    TEST_CHECK("3100 mV from NOMINAL -> skipped to SUNSAFE in one call",
               EV_BAT_SUNSAFE,
               xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES);
}

static void test_heater_hysteresis(void)
{
    /* Verifies: EPS_Heater_Control() respects the 0-5C hysteresis band:
     *   ON  below 0C, stays ON  in band (0-5C), OFF above 5C,
     *   stays OFF in band, and ignores temperature when auto_heat is disabled.
     * BREAKPOINT: step into EPS_Heater_Control(), watch the threshold comparisons. */
    EPS_Test_Set_Auto_Heat(true);
    eps_hw_heater_set(false);

    bool h;
    EPS_Heater_Control(-1);  h = eps_hw_heater_get(); TEST_CHECK("heater ON  below band   (-1C, exp=1)", true,  h);
    EPS_Heater_Control(3);   h = eps_hw_heater_get(); TEST_CHECK("heater ON  in band hold (+3C, exp=1)", true,  h);
    EPS_Heater_Control(6);   h = eps_hw_heater_get(); TEST_CHECK("heater OFF above band   (+6C, exp=0)", false, h);
    EPS_Heater_Control(3);   h = eps_hw_heater_get(); TEST_CHECK("heater OFF in band hold (+3C, exp=0)", false, h);
    EPS_Test_Set_Auto_Heat(false);
    EPS_Heater_Control(-10); h = eps_hw_heater_get(); TEST_CHECK("heater OFF disabled    (-10C, exp=0)", false, h);
}

/* ════════════════════════ 4. E2E scenarios (live tasks) ══════════════════ */

static void e2e_nominal_sunlit(void)
{
    /* Setup: 3.9V battery charging in sunlight. No heater. Mock sensors active.
     * Verifies: correct NOMINAL state bit, heater off, and byte-exact telemetry in flash.
     * Note: mailbox bytes are checked against hand-decoded values, not against Pack()
     *       output — this makes the check independent of eps_frame_pack() itself.
     * BREAKPOINT A: after wait_cycles — inspect batteryStatus event group.
     * BREAKPOINT B: after read_mailbox — inspect mailbox[] byte by byte. */
    printf("  [setup] battery=3899mV charging=yes power_fail=no heater=off\r\n");
    Battery_Telemetry_t batt = nominal_battery();
    EPS_Status_t        pmic = sunlit_pmic();
    DS2782_Set_Mock_Values(&batt);
    LTC4040_Set_Mock_Values(&pmic);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));

    /* BREAKPOINT A */
    uint32_t bat_bits = xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES;
    TEST_CHECK("battery state = NOMINAL (0x01)",   EV_BAT_NOMINAL, bat_bits);
    TEST_CHECK("heater GPIO = OFF (0)",             false,          eps_hw_heater_get());

    /* BREAKPOINT B — byte-by-byte, independent of eps_frame_pack():
     * raw_voltage=799=0x031F          -> lo=0x1F hi=0x03   [0-1]
     * raw_temperature=200=0x00C8      -> lo=0xC8 hi=0x00   [2-3]
     * rel_cap=80=0x50                                       [4]
     * raw_current=768=0x0300          -> lo=0x00 hi=0x03   [5-6]
     * raw_avg_current=640=0x0280      -> lo=0x80 hi=0x02   [7-8]
     * raw_acr=4012=0x0FAC             -> lo=0xAC hi=0x0F   [9-10]
     * raw_active_abs_cap=1050=0x041A  -> lo=0x1A hi=0x04   [11-12]
     * raw_standby_abs_cap=1076=0x0434 -> lo=0x34 hi=0x04   [13-14]
     * raw_standby_rel_cap=82=0x52                          [15]
     * clprog=2048=0x0800              -> lo=0x00 hi=0x08   [16-17]
     * system_status: is_charging=1 -> 0x01                 [18] */
    uint8_t mailbox[sizeof(EPS_Telemetry_Frame_t)];
    read_mailbox(mailbox);
    TEST_CHECK("mailbox vbat_raw        lo (799->0x1F)",     0x1F, mailbox[0]);
    TEST_CHECK("mailbox vbat_raw        hi (799->0x03)",     0x03, mailbox[1]);
    TEST_CHECK("mailbox temp_raw        lo (200->0xC8)",     0xC8, mailbox[2]);
    TEST_CHECK("mailbox temp_raw        hi (200->0x00)",     0x00, mailbox[3]);
    TEST_CHECK("mailbox rel_cap_raw         (80->0x50)",     0x50, mailbox[4]);
    TEST_CHECK("mailbox current_raw     lo  (768->0x00)",    0x00, mailbox[5]);
    TEST_CHECK("mailbox current_raw     hi  (768->0x03)",    0x03, mailbox[6]);
    TEST_CHECK("mailbox avg_current_raw lo  (640->0x80)",    0x80, mailbox[7]);
    TEST_CHECK("mailbox avg_current_raw hi  (640->0x02)",    0x02, mailbox[8]);
    TEST_CHECK("mailbox acr_raw         lo (4012->0xAC)",    0xAC, mailbox[9]);
    TEST_CHECK("mailbox acr_raw         hi (4012->0x0F)",    0x0F, mailbox[10]);
    TEST_CHECK("mailbox aac_raw         lo (1050->0x1A)",    0x1A, mailbox[11]);
    TEST_CHECK("mailbox aac_raw         hi (1050->0x04)",    0x04, mailbox[12]);
    TEST_CHECK("mailbox sac_raw         lo (1076->0x34)",    0x34, mailbox[13]);
    TEST_CHECK("mailbox sac_raw         hi (1076->0x04)",    0x04, mailbox[14]);
    TEST_CHECK("mailbox rsrc_raw            (82->0x52)",     0x52, mailbox[15]);
    TEST_CHECK("mailbox clprog_adc      lo (2048->0x00)",    0x00, mailbox[16]);
    TEST_CHECK("mailbox clprog_adc      hi (2048->0x08)",    0x08, mailbox[17]);
    TEST_CHECK("mailbox system_status       (charging=0x01)", 0x01, mailbox[18]);
    dump_mailbox("e2e_nominal_sunlit");
}

static void e2e_eclipse(void)
{
    /* Setup: !PFO power-fail asserted — no input power (e.g. eclipse), not charging.
     * Verifies: power-fail bit (2) set, charging bit (0) clear in system_status.
     * Expected system_status = 0x04 (only the !PFO power-fail bit).
     * BREAKPOINT: after read_mailbox — check mailbox[18] = 0x04. */
    printf("  [setup] power_fail=yes charging=no\r\n");
    EPS_Status_t pmic = sunlit_pmic();
    pmic.is_charging = false;
    pmic.is_eclipse  = true;
    LTC4040_Set_Mock_Values(&pmic);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));

    uint8_t mailbox[sizeof(EPS_Telemetry_Frame_t)];
    read_mailbox(mailbox);
    /* BREAKPOINT */
    TEST_CHECK("system_status bit2 eclipse set   (0x04)", 0x04, mailbox[18] & (1u << 2));
    TEST_CHECK("system_status bit0 charging clear (0x00)", 0x00, mailbox[18] & (1u << 0));
    TEST_CHECK("system_status full byte           (0x04)", 0x04, mailbox[18]);
    dump_mailbox("e2e_eclipse");

    EPS_Status_t restore = sunlit_pmic();
    LTC4040_Set_Mock_Values(&restore);
    wait_cycles(1, 3000);
}

static void e2e_heater_telecommand_and_cold_case(void)
{
    /* Three-step scenario:
     *   Step 1: send N_EPS_ENABLE_AUTO_HEAT -> verify persisted to flash.
     *   Step 2: inject -5C -> verify heater GPIO ON + status bits in mailbox.
     *   Step 3: inject 25C + N_EPS_DISABLE_AUTO_HEAT -> verify heater OFF + flash=0.
     * BREAKPOINT A: after step 1 wait_cycles — read HEATER_CONFIG_ADDR, expect 1.
     * BREAKPOINT B: after step 2 wait_cycles — check PB10 HIGH, mailbox bits 4+5.
     * BREAKPOINT C: after step 3 wait_cycles — check PB10 LOW, HEATER_CONFIG_ADDR=0. */

    printf("  [step 1] send N_EPS_ENABLE_AUTO_HEAT -> flash should persist 1\r\n");
    xTaskNotify(eps_handle, N_EPS_ENABLE_AUTO_HEAT, eSetBits);
    TEST_CHECK_BOOL("wait_cycles(1) completed in time", wait_cycles(1, 5000));
    /* BREAKPOINT A */
    uint8_t conf = 0xEE;
    OBDH_Read_Request(HEATER_CONFIG_ADDR, &conf, 1);
    TEST_CHECK("HEATER_CONFIG_ADDR in flash = 1 (auto-heat on)", 1, conf);

    printf("  [step 2] inject -5C -> expect heater GPIO ON, bits 4+5 in mailbox\r\n");
    set_battery(3900, -5);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));
    /* BREAKPOINT B */
    TEST_CHECK("PB10 heater GPIO HIGH (heater ON at -5C)",       true,  eps_hw_heater_get());
    uint8_t mailbox[sizeof(EPS_Telemetry_Frame_t)];
    read_mailbox(mailbox);
    TEST_CHECK("system_status bit5 heater_state=1 (0x20)",       0x20, mailbox[18] & (1u << 5));
    TEST_CHECK("system_status bit4 auto_heater=1  (0x10)",       0x10, mailbox[18] & (1u << 4));

    printf("  [step 3] inject 25C + N_EPS_DISABLE_AUTO_HEAT -> heater OFF, flash=0\r\n");
    set_battery(3900, 25);
    xTaskNotify(eps_handle, N_EPS_DISABLE_AUTO_HEAT, eSetBits);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));
    /* BREAKPOINT C */
    TEST_CHECK("PB10 heater GPIO LOW (heater OFF after disable)", false, eps_hw_heater_get());
    OBDH_Read_Request(HEATER_CONFIG_ADDR, &conf, 1);
    TEST_CHECK("HEATER_CONFIG_ADDR in flash = 0 (auto-heat off)", 0, conf);
    dump_mailbox("e2e_heater_telecommand");
}

static void e2e_voltage_decay_sweep(void)
{
    /* Verifies: live EPS task transitions through all four battery states as
     * voltage drops. Checks that the state machine boundaries work end-to-end
     * with real FreeRTOS scheduling (not just the unit test seam).
     * BREAKPOINT: after each wait_cycles — inspect batteryStatus event group. */
    struct { uint16_t mv; uint32_t expect; const char *label; } steps[] = {
        {3900, EV_BAT_NOMINAL,     "3900mV -> NOMINAL     (0x01)"},
        {3500, EV_BAT_CONTINGENCY, "3500mV -> CONTINGENCY (0x02)"},
        {3150, EV_BAT_SUNSAFE,     "3150mV -> SUNSAFE     (0x04)"},
        {2900, EV_BAT_SURVIVAL,    "2900mV -> SURVIVAL    (0x08)"},
    };
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        printf("  [inject] %u mV\r\n", steps[i].mv);
        set_battery(steps[i].mv, 25);
        TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));
        /* BREAKPOINT */
        uint32_t bits = xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES;
        TEST_CHECK(steps[i].label, (long)steps[i].expect, (long)bits);
    }
    set_battery(3900, 25);
    wait_cycles(2, 5000);
}

static void e2e_threshold_update(void)
{
    /* Verifies: EPS loads new thresholds from flash when notified.
     * New thresholds: 4000/3600/3300 mV (vs defaults 3700/3300/3000).
     * Test voltage: 3450 mV — above old contingency (3300) but below new (3600).
     * Without the update: 3450 mV -> CONTINGENCY. With update: 3450 mV -> SUNSAFE.
     * BREAKPOINT: after N_EPS_NEW_THRESHOLDS + wait_cycles — inspect thresholds_mv[]
     *             in debugger, then check batteryStatus. */
    printf("  [setup] new thresholds: 4000/3600/3300 mV (flash bytes: 40,36,33)\r\n");
    const uint8_t new_th[3] = {40, 36, 33};
    TEST_CHECK_BOOL("flash write thresholds OK",
        OBDH_Write_Request(EPS_THRESHOLDS_ADDR, new_th, 3) == HAL_OK);
    xTaskNotify(eps_handle, N_EPS_NEW_THRESHOLDS, eSetBits);

    printf("  [inject] 3450mV (below new 3600 contingency -> expect SUNSAFE)\r\n");
    set_battery(3450, 25);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));
    /* BREAKPOINT */
    uint32_t bits = xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES;
    TEST_CHECK("3450mV with new thresholds -> SUNSAFE (0x04)", EV_BAT_SUNSAFE, bits);

    const uint8_t def_th[3] = {37, 33, 30};
    OBDH_Write_Request(EPS_THRESHOLDS_ADDR, def_th, 3);
    xTaskNotify(eps_handle, N_EPS_NEW_THRESHOLDS, eSetBits);
    set_battery(3900, 25);
    wait_cycles(2, 5000);
}

static void e2e_sampling_period_update(void)
{
    /* Verifies: EPS uses the sampling period from flash, not the hardcoded default.
     * New period: 5 (500ms). Measure how many cycles complete in 2200ms.
     * At 1000ms (default): ~2 cycles. At 500ms (new): ~4 cycles. Assert >= 3.
     * BREAKPOINT: watch eps_cycle_count increment faster after N_EPS_NEW_SAMPLING. */
    printf("  [setup] new sampling period: 5 x 100ms = 500ms\r\n");
    const uint16_t fast = 5;
    TEST_CHECK_BOOL("flash write sampling period OK",
        OBDH_Write_Request(EPS_SAMPLING_ADDR, (const uint8_t *)&fast, 2) == HAL_OK);
    xTaskNotify(eps_handle, N_EPS_NEW_SAMPLING, eSetBits);
    wait_cycles(1, 3000);

    uint32_t before = eps_cycle_count;
    vTaskDelay(pdMS_TO_TICKS(2200));
    uint32_t cycles = eps_cycle_count - before;
    printf("  [result] cycles in 2200ms = %lu (expect >= 3 at 500ms period)\r\n", cycles);
    /* BREAKPOINT */
    TEST_CHECK_BOOL("cycles in 2200ms >= 3 (confirms 500ms period is active)", cycles >= 3);

    const uint16_t normal = 10;
    OBDH_Write_Request(EPS_SAMPLING_ADDR, (const uint8_t *)&normal, 2);
    xTaskNotify(eps_handle, N_EPS_NEW_SAMPLING, eSetBits);
    wait_cycles(1, 3000);
}

static void e2e_report_by_exception(void)
{
    /* Verifies the decoupled supervisory loop:
     *  (1) with a long telemetry period the EPS keeps ticking (watchdog stays fed
     *      because the heartbeat is fixed, not tied to the sampling period), and
     *  (2) a battery-state transition forces an immediate telemetry write even
     *      though the long period has not elapsed, so the recorded voltage never
     *      lags the broadcast state.
     * BREAKPOINT A: confirm wait_cycles keeps returning true under the 60s period.
     * BREAKPOINT B: after read_mailbox, vbat must be the contingency reading. */

    /* Start NOMINAL and let a periodic write latch the nominal voltage. */
    Battery_Telemetry_t batt = nominal_battery();      /* raw 799 -> ~3899 mV NOMINAL */
    EPS_Status_t        pmic = sunlit_pmic();
    DS2782_Set_Mock_Values(&batt);
    LTC4040_Set_Mock_Values(&pmic);
    wait_cycles(2, 5000);

    /* Switch to a long telemetry period (60 s). */
    const uint16_t slow = 600;                         /* 600 x 100 ms = 60 s */
    TEST_CHECK_BOOL("flash write 60s sampling period OK",
        OBDH_Write_Request(EPS_SAMPLING_ADDR, (const uint8_t *)&slow, 2) == HAL_OK);
    xTaskNotify(eps_handle, N_EPS_NEW_SAMPLING, eSetBits);
    wait_cycles(1, 5000);

    /* (1) Task must keep ticking despite the 60s period (heartbeat decoupled). */
    TEST_CHECK_BOOL("task keeps ticking with 60s period (watchdog-safe)",
        wait_cycles(3, 5000));

    /* (2) Drop voltage into CONTINGENCY: the transition must trigger an immediate
     *     telemetry write carrying the new voltage, long before 60 s elapse. */
    batt.raw_voltage = (uint16_t)((3500u * 100u) / 488u);  /* ~3500 mV -> CONTINGENCY (717) */
    DS2782_Set_Mock_Values(&batt);
    wait_cycles(3, 5000);

    uint32_t bat_bits = xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES;
    TEST_CHECK("state transitioned to CONTINGENCY (0x02)", EV_BAT_CONTINGENCY, bat_bits);

    uint8_t mailbox[sizeof(EPS_Telemetry_Frame_t)];
    read_mailbox(mailbox);
    uint16_t mb_vbat = (uint16_t)(mailbox[0] | (mailbox[1] << 8));
    printf("  [result] mailbox vbat_raw = %u (expect %u, the contingency reading)\r\n",
           mb_vbat, batt.raw_voltage);
    TEST_CHECK("on-change telemetry recorded the contingency voltage",
               batt.raw_voltage, mb_vbat);

    /* Restore default period and nominal battery for subsequent tests. */
    const uint16_t normal_period = 10;
    OBDH_Write_Request(EPS_SAMPLING_ADDR, (const uint8_t *)&normal_period, 2);
    xTaskNotify(eps_handle, N_EPS_NEW_SAMPLING, eSetBits);
    batt = nominal_battery();
    DS2782_Set_Mock_Values(&batt);
    wait_cycles(2, 5000);
}

static void e2e_battery_sensor_failure(void)
{
    /* Verifies: when DS2782 read fails, battery_read_failure bit (6) is set
     * in system_status. Battery raw fields retain the last good values.
     * Consumers must check bit6 before trusting any battery field.
     * Expected system_status = 0x41 (charging=1, batt_fail=1).
     * BREAKPOINT: after wait_cycles — inspect mailbox[18], expect bit6=1. */
    printf("  [setup] DS2782 read failure injected\r\n");
    DS2782_Set_Mock_Fail(true);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));

    uint8_t mailbox[sizeof(EPS_Telemetry_Frame_t)];
    read_mailbox(mailbox);
    /* BREAKPOINT */
    TEST_CHECK("system_status bit6 battery_read_fail=1 (0x40)", 0x40, mailbox[18] & (1u << 6));
    TEST_CHECK("system_status full byte (charging+fail = 0x41)", 0x41, mailbox[18]);
    dump_mailbox("e2e_battery_sensor_failure");
    DS2782_Set_Mock_Fail(false);
}

static void e2e_thresholds_not_clobbered(void)
{
    /* Regression for B3: telemetry writes go to 0x08031800, thresholds live at
     * 0x08030800 — different flash pages. This test confirms that repeated EPS
     * telemetry cycles do not overwrite config data at the threshold address.
     * BREAKPOINT: after wait_cycles — read EPS_THRESHOLDS_ADDR and compare to seeded. */
    const uint8_t seeded[3] = {37, 33, 30};
    printf("  [setup] seeded {37,33,30} at EPS_THRESHOLDS_ADDR (0x%08lX)\r\n",
           (uint32_t)EPS_THRESHOLDS_ADDR);
    OBDH_Write_Request(EPS_THRESHOLDS_ADDR, seeded, 3);
    TEST_CHECK_BOOL("wait_cycles(3) completed in time", wait_cycles(3, 8000));

    uint8_t readback[3] = {0};
    OBDH_Read_Request(EPS_THRESHOLDS_ADDR, readback, 3);
    /* BREAKPOINT */
    TEST_CHECK("threshold[0] = 37 unchanged after telemetry cycles", seeded[0], readback[0]);
    TEST_CHECK("threshold[1] = 33 unchanged after telemetry cycles", seeded[1], readback[1]);
    TEST_CHECK("threshold[2] = 30 unchanged after telemetry cycles", seeded[2], readback[2]);
}

/* ════════════════════════ 5. Hardware checkout ═══════════════════════════ */
/*
 * Runs against the real DS2782 and LTC4040, before eps_task exists, so the
 * gauge tests own I2C1. Each run leaves the gauge holding
 * DS2782_PACK_2P_NCR14430_DEFAULTS and spends at most three EEPROM copies
 * (endurance 50,000).
 *
 * The two measurement tests only report: their [MEAS] values feed the
 * capacity model (IAE from the load on battery, IMIN from the current at
 * which the charger stops) and are repeated in the final summary.
 */

#define HW_GAUGE_UPDATE_MS   3600u  /* RAAC and CURRENT refresh every 3.515 s */
#define HW_SAMPLE_MS         1000u
#define HW_LOAD_WINDOW_S     20u    /* per step of the load profile            */
#define HW_CHARGE_WATCH_S    60u    /* raise (e.g. 7200) for an end-of-charge soak */

static const DS2782_Capacity_Config_t k_pack_model = DS2782_PACK_2P_NCR14430_DEFAULTS;

static const char *prog_result_name(DS2782_Prog_Result_t r)
{
    switch (r) {
    case DS2782_PROG_WRITTEN:   return "WRITTEN";
    case DS2782_PROG_UNCHANGED: return "UNCHANGED";
    default:                    return "ERROR";
    }
}

static int16_t avg_current_ma(const Battery_Telemetry_t *b)
{
    Battery_Telemetry_t t = { .raw_current = b->raw_avg_current };
    return DS2782_Compute_Current(&t);   /* IAVG has the CURRENT scale */
}

/* Smallest value of a current register with uv_per_lsb across RSNS that is >= ma. */
static uint16_t ceil_to_lsb_ma(uint32_t ma, uint32_t uv_per_lsb)
{
    uint32_t raw = (ma * DS2782_RSNS_MOHM + uv_per_lsb - 1u) / uv_per_lsb;
    return (uint16_t)((raw * uv_per_lsb + DS2782_RSNS_MOHM / 2u) / DS2782_RSNS_MOHM);
}

static void print_model(const char *label, const DS2782_Capacity_Config_t *m)
{
    printf("  [info] %s: Full40=%u mAh AC=%u mAh VCHG=%u mV IMIN=%u mA VAE=%u mV"
           " IAE=%u mA RSNSP=%u AE40=%u\r\n",
           label, (unsigned)m->full40_mah, (unsigned)m->aging_cap_mah,
           (unsigned)m->vchg_mv, (unsigned)m->imin_ma, (unsigned)m->vae_mv,
           (unsigned)m->iae_ma, (unsigned)m->rsnsp_mho, (unsigned)m->ae40_raw);
}

/** Read the model back from EEPROM and compare every field with exp. */
static void check_stored_model(const DS2782_Capacity_Config_t *exp)
{
    DS2782_Capacity_Config_t got;
    memset(&got, 0, sizeof got);
    TEST_CHECK_BOOL("read model back from EEPROM", ds2782_read_capacity_config(&hi2c1, &got));
    TEST_CHECK("  Full40 mAh",        exp->full40_mah,    got.full40_mah);
    TEST_CHECK("  AC mAh",            exp->aging_cap_mah, got.aging_cap_mah);
    TEST_CHECK("  VCHG mV",           exp->vchg_mv,       got.vchg_mv);
    TEST_CHECK("  IMIN mA",           exp->imin_ma,       got.imin_ma);
    TEST_CHECK("  VAE mV",            exp->vae_mv,        got.vae_mv);
    TEST_CHECK("  IAE mA",            exp->iae_ma,        got.iae_ma);
    TEST_CHECK("  RSNSP mho",         exp->rsnsp_mho,     got.rsnsp_mho);
    TEST_CHECK("  AE40 raw",          exp->ae40_raw,      got.ae40_raw);
    TEST_CHECK("  slope bytes differing", 0,
               memcmp(exp->slopes_raw, got.slopes_raw, DS2782_NUM_SLOPES));
}

static void hw_detect(void)
{
    g_hw.present = ds2782_probe(&hi2c1);
    printf("[HW] DS2782 %s\r\n", g_hw.present
           ? "detected: running hardware checkout"
           : "not answering on I2C1: hardware tests will be skipped");
}

static void hw_gauge_program_template(void)
{
    /* Verifies: the pack template reaches EEPROM and reads back field by field.
     * WRITTEN on a fresh gauge; UNCHANGED on later runs proves EEPROM kept the
     * model across the reset. */
    DS2782_Capacity_Config_t stored;
    bool ok = ds2782_read_capacity_config(&hi2c1, &stored);
    TEST_CHECK_BOOL("read stored model before programming", ok);
    if (ok) print_model("stored at start", &stored);

    DS2782_Prog_Result_t r = ds2782_program_capacity(&hi2c1, &k_pack_model);
    g_hw.first_program = (int)r;
    printf("  [info] program template -> %s\r\n", prog_result_name(r));
    TEST_CHECK_BOOL("program template succeeds (WRITTEN or UNCHANGED)", r != DS2782_PROG_ERROR);
    check_stored_model(&k_pack_model);
}

static void hw_gauge_program_idempotent(void)
{
    /* Verifies: programming an identical model spends no EEPROM cycle. */
    DS2782_Prog_Result_t r = ds2782_program_capacity(&hi2c1, &k_pack_model);
    TEST_CHECK("same model again -> UNCHANGED (2)", DS2782_PROG_UNCHANGED, r);
}

static void hw_gauge_reprogram_roundtrip(void)
{
    /* Verifies: a single changed field is written, persists, and the template
     * can be restored afterwards (two EEPROM copies). */
    DS2782_Capacity_Config_t alt = k_pack_model;
    alt.iae_ma = 200u;   /* raw 15 at 15 mOhm, exactly representable */

    TEST_CHECK("program IAE=200 mA -> WRITTEN (1)", DS2782_PROG_WRITTEN,
               ds2782_program_capacity(&hi2c1, &alt));
    check_stored_model(&alt);

    TEST_CHECK("restore template -> WRITTEN (1)", DS2782_PROG_WRITTEN,
               ds2782_program_capacity(&hi2c1, &k_pack_model));
    check_stored_model(&k_pack_model);
}

static void hw_gauge_rejects_out_of_range(void)
{
    /* Verifies: a field that does not fit its register is refused before any
     * write, and the stored model stays intact. */
    DS2782_Capacity_Config_t bad = k_pack_model;
    bad.vchg_mv = 6000u;   /* raw 307 > 255 */

    TEST_CHECK("VCHG=6000 mV -> ERROR (0)", DS2782_PROG_ERROR,
               ds2782_program_capacity(&hi2c1, &bad));
    check_stored_model(&k_pack_model);
}

static void hw_gauge_live_telemetry(void)
{
    /* Verifies: live readings are plausible for the 2P pack, and RAAC/RSAC
     * agree with ACR. With AE40 = 0 and zero slopes, AE(T) = SE(T) = 0, so the
     * gauge computes RAAC = RSAC = ACR x RSNSP (6.25 uVh x 67 mho = 0.41875 mAh
     * per ACR LSB). Agreement checks the burst window and RSNSP together. */
    vTaskDelay(pdMS_TO_TICKS(HW_GAUGE_UPDATE_MS));   /* one result update with the new model */

    Battery_Telemetry_t b;
    memset(&b, 0, sizeof b);
    TEST_CHECK_BOOL("battery telemetry read", eps_hw_read_battery(&b));

    uint16_t mv    = eps_hw_battery_voltage_mv(&b);
    int16_t  temp  = eps_hw_battery_temp_c(&b);
    int16_t  acr   = DS2782_Compute_Accumulated_Charge(&b);
    uint32_t raac  = DS2782_Compute_Active_Capacity(&b);
    uint32_t rsac  = DS2782_Compute_Standby_Capacity(&b);
    printf("  [info] V=%u mV I=%d mA IAVG=%d mA T=%d C ACR=%d mAh RAAC=%lu mAh"
           " RSAC=%lu mAh RARC=%u %% RSRC=%u %%\r\n",
           (unsigned)mv, eps_hw_battery_current_ma(&b), avg_current_ma(&b), temp, acr,
           (unsigned long)raac, (unsigned long)rsac,
           (unsigned)b.raw_relative_cap, (unsigned)b.raw_standby_rel_cap);

    TEST_RANGE("battery voltage mV",           2500, 4300, mv);
    TEST_RANGE("battery temperature C",         -20,   60, temp);
    TEST_RANGE("RARC %",                           0,  100, b.raw_relative_cap);
    TEST_RANGE("RSRC %",                           0,  100, b.raw_standby_rel_cap);

    int32_t exp_cap = (b.raw_acr > 0)
        ? (int32_t)b.raw_acr * 625 * k_pack_model.rsnsp_mho / 100000 : 0;
    int32_t tol = 2 + exp_cap / 100;   /* 1.6 mAh RAAC LSB + 1 % */
    TEST_RANGE("RAAC mAh = ACR x RSNSP", exp_cap - tol, exp_cap + tol, raac);
    TEST_RANGE("RSAC mAh = ACR x RSNSP", exp_cap - tol, exp_cap + tol, rsac);
    if (b.raw_acr <= 0)
        printf("  [info] ACR <= 0: no charge reference yet; RAAC/RARC stay 0 until the"
               " first full charge (CHGTF) or ds2782_set_accumulated_charge()\r\n");

    if (b.raw_relative_cap > 0) {
        g_hw.full_est_valid = true;
        g_hw.full_est_mah   = raac * 100u / b.raw_relative_cap;
        printf("  [MEAS] effective capacity RAAC/RARC = %lu mAh\r\n",
               (unsigned long)g_hw.full_est_mah);
    }

    uint8_t st = 0;
    TEST_CHECK_BOOL("STATUS register read", ds2782_read_status(&hi2c1, &st));
    g_hw.chgtf_seen |= (st & DS2782_STATUS_CHGTF) != 0u;
    printf("  [info] STATUS=0x%02X CHGTF=%d AEF=%d SEF=%d LEARNF=%d\r\n", st,
           (st & DS2782_STATUS_CHGTF) != 0u, (st & DS2782_STATUS_AEF) != 0u,
           (st & DS2782_STATUS_SEF) != 0u,   (st & DS2782_STATUS_LEARNF) != 0u);
}

static void hw_measure_load_profile(void)
{
    /* Measures: battery discharge current with the heater off and on. Only
     * valid on battery: with input power present the PMIC feeds the load from
     * the input and the battery current is not the load. The image exercises
     * the MCU and the heater only, not radio TX. */
    EPS_Status_t p;
    memset(&p, 0, sizeof p);
    TEST_CHECK_BOOL("PMIC status read", eps_hw_read_pmic(&p));
    g_hw.load_valid = p.is_eclipse;
    printf("  [info] input power %s\r\n", p.is_eclipse
           ? "absent (!PFO asserted): battery carries the load"
           : "present: battery current is NOT the load, values below are indicative only");

    bool heater_was = eps_hw_heater_get();
    int16_t peak_discharge = 0;

    for (int step = 0; step < 2; step++) {
        eps_hw_heater_set(step == 1);
        vTaskDelay(pdMS_TO_TICKS(HW_GAUGE_UPDATE_MS));   /* settle one CURRENT update */

        int32_t sum = 0;
        int     n   = 0;
        for (uint32_t s = 0; s < HW_LOAD_WINDOW_S; s++) {
            Battery_Telemetry_t b;
            if (eps_hw_read_battery(&b)) {
                int16_t discharge = (int16_t)-eps_hw_battery_current_ma(&b);
                sum += discharge;
                n++;
                if (discharge > peak_discharge) peak_discharge = discharge;
            }
            vTaskDelay(pdMS_TO_TICKS(HW_SAMPLE_MS));
        }
        g_hw.load_ma[step] = (int16_t)(n ? sum / n : 0);
        printf("  [MEAS] heater %s: mean discharge %d mA over %d samples\r\n",
               step ? "on " : "off", g_hw.load_ma[step], n);
        TEST_CHECK_BOOL(step ? "samples taken (heater on)" : "samples taken (heater off)", n > 0);
    }
    eps_hw_heater_set(heater_was);

    g_hw.load_peak_ma = peak_discharge;
    int16_t sustained = (g_hw.load_ma[0] > g_hw.load_ma[1]) ? g_hw.load_ma[0] : g_hw.load_ma[1];
    g_hw.iae_candidate_ma = ceil_to_lsb_ma(sustained > 0 ? (uint32_t)sustained : 0u, 200u);
    printf("  [MEAS] peak discharge %d mA, IAE candidate %u mA (largest sustained step,"
           " rounded up to 13.33 mA LSB)\r\n",
           g_hw.load_peak_ma, (unsigned)g_hw.iae_candidate_ma);
}

static void hw_measure_charge_end(void)
{
    /* Measures: the current at which the LTC4040 ends the charge (!CHRG
     * releases). The gauge detects full when VOLT > VCHG and 0 < IAVG < IMIN,
     * so IMIN must sit just above that current. Only observable near the end
     * of a charge; raise HW_CHARGE_WATCH_S for a soak. */
    EPS_Status_t p;
    memset(&p, 0, sizeof p);
    TEST_CHECK_BOOL("PMIC status read", eps_hw_read_pmic(&p));
    if (!p.is_charging || p.charging_disabled) {
        printf("  [info] charger not active (charging=%d disabled=%d): charge end not"
               " observable this run\r\n", p.is_charging, p.charging_disabled);
        return;
    }
    g_hw.charging_seen = true;

    int16_t last_ma = 0;
    for (uint32_t s = 0; s < HW_CHARGE_WATCH_S; s++) {
        Battery_Telemetry_t b;
        uint8_t st = 0;
        if (!eps_hw_read_battery(&b) || !eps_hw_read_pmic(&p)) {
            vTaskDelay(pdMS_TO_TICKS(HW_SAMPLE_MS));
            continue;
        }
        if (ds2782_read_status(&hi2c1, &st) && (st & DS2782_STATUS_CHGTF))
            g_hw.chgtf_seen = true;

        if (!p.is_charging) {
            g_hw.charge_end_seen  = true;
            g_hw.charge_end_ma    = last_ma;
            /* "Marginally greater": 10 % above the end current, register LSB. */
            g_hw.imin_candidate_ma = ceil_to_lsb_ma((uint32_t)(last_ma > 0 ? last_ma : 0) * 11u / 10u, 50u);
            printf("  [MEAS] charger stopped after %lu s at %d mA -> IMIN candidate %u mA\r\n",
                   (unsigned long)s, last_ma, (unsigned)g_hw.imin_candidate_ma);
            return;
        }
        last_ma = eps_hw_battery_current_ma(&b);

        if (s % 10u == 0u)
            printf("  [MEAS] t=%lus V=%u mV I=%d mA IAVG=%d mA CHGTF=%d (VCHG=%u mV)\r\n",
                   (unsigned long)s, (unsigned)eps_hw_battery_voltage_mv(&b), last_ma,
                   avg_current_ma(&b), (st & DS2782_STATUS_CHGTF) != 0u,
                   (unsigned)k_pack_model.vchg_mv);
        vTaskDelay(pdMS_TO_TICKS(HW_SAMPLE_MS));
    }
    printf("  [info] still charging after %u s: end of charge not reached\r\n",
           (unsigned)HW_CHARGE_WATCH_S);
}

/** Hardware suite: real sensors on, mocks restored afterwards for the e2e suite. */
static void run_hw_suite(void)
{
    DS2782_Set_Mock_Mode(false);
    hw_detect();
    RUN_HW_TEST(hw_gauge_program_template);
    RUN_HW_TEST(hw_gauge_program_idempotent);
    RUN_HW_TEST(hw_gauge_reprogram_roundtrip);
    RUN_HW_TEST(hw_gauge_rejects_out_of_range);
    RUN_HW_TEST(hw_gauge_live_telemetry);
    RUN_HW_TEST(hw_measure_load_profile);
    RUN_HW_TEST(hw_measure_charge_end);
    DS2782_Set_Mock_Mode(true);
}

/* ════════════════════════ 6. Runner task ═════════════════════════════════ */

static void test_runner(void *pv)
{
    (void)pv;

    printf("\r\n========= EPS TEST FIRMWARE =========\r\n");
    /* Unit and e2e suites run on injected data; run_hw_suite() switches the
     * real sensors on for its own duration. */
    DS2782_Set_Mock_Mode(true);

    batteryStatus = xEventGroupCreate();
    xEventGroupSetBits(batteryStatus, EV_BAT_NOMINAL);

    printf("--- unit tests ---\r\n");
    RUN_TEST(test_compute_voltage);
    RUN_TEST(test_compute_current);
    RUN_TEST(test_compute_temperature);
    RUN_TEST(test_unit_consistency);
    RUN_TEST(test_payload_bytes);
    RUN_TEST(test_status_bits);
    RUN_TEST(test_pack_null_safety);
    RUN_TEST(test_state_machine_boundaries);
    RUN_TEST(test_hysteresis_rising_blocks);
    RUN_TEST(test_hysteresis_falling_immediate);
    RUN_TEST(test_falling_multi_step);
    RUN_TEST(test_heater_hysteresis);

    /* Before eps_task is created, so the gauge tests own I2C1. */
    printf("--- hardware checkout ---\r\n");
    run_hw_suite();

    printf("--- e2e scenarios ---\r\n");
    /* The e2e suite runs at a 1 s period; the stored period is put back
     * afterwards so a flight image flashed later keeps its own default. */
    uint8_t saved_period[2];
    OBDH_Read_Request(EPS_SAMPLING_ADDR, saved_period, sizeof(saved_period));

    const uint8_t  def_th[3]  = {37, 33, 30};
    const uint16_t def_period = 10;
    const uint8_t  heater_off = 0;
    OBDH_Write_Request(EPS_THRESHOLDS_ADDR, def_th, 3);
    OBDH_Write_Request(EPS_SAMPLING_ADDR, (const uint8_t *)&def_period, 2);
    OBDH_Write_Request(HEATER_CONFIG_ADDR, &heater_off, 1);

    xTaskCreate(eps_task, "EPS", EPS_STACK_SIZE, NULL, EPS_PRIORITY, &eps_handle);
    wait_cycles(1, 5000);

    RUN_TEST(e2e_nominal_sunlit);
    RUN_TEST(e2e_eclipse);
    RUN_TEST(e2e_heater_telecommand_and_cold_case);
    RUN_TEST(e2e_voltage_decay_sweep);
    RUN_TEST(e2e_threshold_update);
    RUN_TEST(e2e_sampling_period_update);
    RUN_TEST(e2e_battery_sensor_failure);
    RUN_TEST(e2e_thresholds_not_clobbered);
    RUN_TEST(e2e_report_by_exception);

    /* Flash only, no N_EPS_NEW_SAMPLING: eps_task keeps 1 s for the rest of
     * this run. An erased (0xFFFF) value is written back as 0xFFFF. */
    OBDH_Write_Request(EPS_SAMPLING_ADDR, saved_period, sizeof(saved_period));
    printf("  [teardown] sampling period in flash restored to 0x%02X%02X\r\n",
           saved_period[1], saved_period[0]);

    tests_done();
}

/* ════════════════════════ 7. main() + glue ═══════════════════════════════ */

static void wdog_task(void *pv)
{
    (void)pv;
    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* main.c is excluded from this build; provide what the linker needs from it. */
TaskHandle_t main_get_obc_handle(void) { return NULL; }  /* tc_handler links this */

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}

/** EPS pins on the dev board: heater + CHGOFF as outputs, PMIC flags as inputs. */
static void MX_GPIO_Init_EPS(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_10;                  /* PB10 heater  */
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_3;                   /* PA3 CHGOFF   */
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3,  GPIO_PIN_RESET);

    /* PB2 !CHRG — polled GPIO (charging status changes gradually) */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PB5 !PFO — EXTI both edges (falling = eclipse start, rising = eclipse end) */
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PC4 !FAULT — EXTI falling edge (fault when pin goes LOW) */
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PA4 CLPROG — ADC1 channel 9 (LTC4040 solar input current monitor) */
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin  = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Enable NVIC for both EXTI lines — priority 5 satisfies FreeRTOS fromISR requirement */
    HAL_NVIC_SetPriority(EXTI4_IRQn,   5, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

int main(void)
{
    HAL_Init();

    if (!systemclock_init_for_freq(CLK_FREQ_80MHZ))
        Error_Handler();
    periph_init_for_freq(CLK_FREQ_80MHZ);    /* brings up USART2 for printf   */
    log_init();
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
        printf("*** RESET CAUSE: IWDG watchdog ***\r\n");
    __HAL_RCC_CLEAR_RESET_FLAGS();
    MX_GPIO_Init_EPS();
    /* I2C1 is already initialized by periph_init_for_freq() above. */

    obdh_queue_handle = xQueueCreate(10, sizeof(obdh_request));
    if (obdh_queue_handle == NULL)
        Error_Handler();

    xTaskCreate(obdh_task,   "OBDH",    OBDH_STACK_SIZE, NULL, OBDH_PRIORITY, NULL);
    xTaskCreate(test_runner, "TESTRUN", 1024,            NULL, 2,             NULL);
    xTaskCreate(wdog_task,   "WDOG",    128,             NULL, 1,             NULL);

    vTaskStartScheduler();
    return 0;
}
