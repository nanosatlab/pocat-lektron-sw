/**
 * @file test_eps.c
 * @brief On-target EPS test firmware: substitute main + unit tests + e2e scenarios.
 *
 * Built only by -DEPS_TESTS=ON (./build.sh --tests) as pocat_eps_tests.elf,
 * replacing src/core/main.c. Runs the REAL FreeRTOS, HAL, eps_task and
 * obdh_task; only DS2782/LTC4040 data is injected (no sensors connected).
 * Output: printf -> USART2 (log.c), i.e. the normal serial monitor.
 *
 * Hardware checkout: when the sensors are wired, change USE_MOCK_SENSORS in
 * test_runner() to false and rerun the same image.
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

#define RUN_TEST(fn)                                                           \
    do {                                                                       \
        int _before = g_failures;                                              \
        printf("[TEST] %s\r\n", #fn);                                          \
        fn();                                                                  \
        printf("       %s\r\n", (_before == g_failures) ? "[PASS]" : "[FAIL]");\
    } while (0)

static void tests_done(void)
{
    printf("\r\n==============================================\r\n");
    printf(" EPS TESTS DONE: %d asserts, %d failures -> %s\r\n",
           g_asserts, g_failures, g_failures ? "FAIL" : "ALL PASS");
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

/* Reference battery telemetry: 3.9 V, 25 C, 80 % SoC */
static Battery_Telemetry_t nominal_battery(void)
{
    Battery_Telemetry_t b = {
        .raw_voltage      = 799,   /* 799 * 488/100  = 3899 mV */
        // .raw_current   = 768,   /* 768 * 5/32     = 120 mA  — enable with raw_current */
        .raw_temperature  = 200,   /* 200 / 8        = 25 C    */
        .raw_relative_cap = 80,    /* 80 %                     */
        // .raw_accumulated_cap = 2400,  /* enable if energy accounting needed */
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

/** Read the telemetry mailbox back from real flash through the OBDH task. */
static void read_mailbox(uint8_t out[sizeof(OBDH_Payload_t)])
{
    memset(out, 0, sizeof(OBDH_Payload_t));
    OBDH_Read_Request(OBDH_EPS_TELEMETRY_ADDR, out, sizeof(OBDH_Payload_t));
}

static void dump_mailbox(const char *label)
{
    static const char *field_names[8] = {
        "vbat_raw[0]",   "vbat_raw[1]",
        "temp_raw[0]",   "temp_raw[1]",
        "rel_cap_raw",
        "clprog_adc[0]", "clprog_adc[1]",
        "system_status",
    };
    uint8_t mailbox[sizeof(OBDH_Payload_t)];
    read_mailbox(mailbox);
    printf("  [mailbox @ %s]\r\n", label);
    for (int i = 0; i < 8; i++)
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

/* test_compute_current — enable when hardware team adds raw_current to Battery_Telemetry_t
static void test_compute_current(void)
{
    Battery_Telemetry_t s = {0};
    int16_t c;
    s.raw_current = 1000; c = DS2782_Compute_Current(&s); TEST_CHECK("current mA (raw=1000  ->  156)",  156, c);
    s.raw_current = -500; c = DS2782_Compute_Current(&s); TEST_CHECK("current mA (raw=-500  ->  -78)", -78,  c);
    s.raw_current = 0;    c = DS2782_Compute_Current(&s); TEST_CHECK("current mA (raw=0     ->    0)",   0,  c);
}
*/

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
_Static_assert(sizeof(OBDH_Payload_t) == 8, "OBDH_Payload_t size changed");
_Static_assert(offsetof(OBDH_Payload_t, vbat_raw)      == 0, "layout");
_Static_assert(offsetof(OBDH_Payload_t, temp_raw)      == 2, "layout");
_Static_assert(offsetof(OBDH_Payload_t, rel_cap_raw)   == 4, "layout");
_Static_assert(offsetof(OBDH_Payload_t, clprog_adc)    == 5, "layout");
_Static_assert(offsetof(OBDH_Payload_t, system_status) == 7, "layout");

static void test_payload_bytes(void)
{
    /* Verifies: EPS_Pack_Telemetry() writes each field at the right byte offset
     * in little-endian order. This is the non-circular check — inputs and expected
     * bytes are both hardcoded independently of Pack.
     * BREAKPOINT: step through EPS_Pack_Telemetry(), watch each field being written. */
    Battery_Telemetry_t battery = {0};
    EPS_Status_t        status  = {0};
    OBDH_Payload_t      payload;
    memset(&payload, 0, sizeof payload);

    battery.raw_voltage      = 0x1234;
    battery.raw_temperature  = 0x001A;
    battery.raw_relative_cap = 85;      /* 0x55 */
    status.raw_clprog_adc    = 0xBCDE;
    status.is_charging = true;          /* bit0 */
    status.is_eclipse  = true;          /* bit2 -> system_status = 0x05 */

    EPS_Pack_Telemetry(&battery, &status, &payload);

    static const char *byte_labels[8] = {
        "vbat_raw   lo", "vbat_raw   hi",
        "temp_raw   lo", "temp_raw   hi",
        "rel_cap_raw",
        "clprog_adc lo", "clprog_adc hi",
        "system_status",
    };
    const uint8_t expected[8] = {
        0x34, 0x12,  0x1A, 0x00,  0x55,  0xDE, 0xBC,  0x05,
    };
    const uint8_t *bytes = (const uint8_t *)&payload;
    for (int i = 0; i < 8; i++) {
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
     * BREAKPOINT: step through the boolean packing section of EPS_Pack_Telemetry(). */
    static const char *bit_labels[8] = {
        "bit0 is_charging       -> 0x01",
        "bit1 has_fault         -> 0x02",
        "bit2 is_eclipse        -> 0x04",
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
        OBDH_Payload_t payload;
        *((bool *)((uint8_t *)&status + offsets[bit])) = true;
        EPS_Pack_Telemetry(&battery, &status, &payload);
        TEST_CHECK(bit_labels[bit], (long)(1u << bit), (long)payload.system_status);
    }
}

static void test_pack_null_safety(void)
{
    /* Verifies: EPS_Pack_Telemetry() does not crash or write when given NULL inputs.
     * BREAKPOINT: step into EPS_Pack_Telemetry() — should hit the NULL guard and return. */
    OBDH_Payload_t payload;
    memset(&payload, 0xAB, sizeof payload);
    EPS_Pack_Telemetry(NULL, NULL, &payload);
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

static void test_heater_hysteresis(void)
{
    /* Verifies: EPS_Heater_Control() respects the 0-5C hysteresis band:
     *   ON  below 0C, stays ON  in band (0-5C), OFF above 5C,
     *   stays OFF in band, and ignores temperature when auto_heat is disabled.
     * BREAKPOINT: step into EPS_Heater_Control(), watch the threshold comparisons. */
    EPS_Test_Set_Auto_Heat(true);
    EPS_Heater_Disable();

    bool h;
    EPS_Heater_Control(-1);  h = EPS_Heater_Read(); TEST_CHECK("heater ON  below band   (-1C, exp=1)", true,  h);
    EPS_Heater_Control(3);   h = EPS_Heater_Read(); TEST_CHECK("heater ON  in band hold (+3C, exp=1)", true,  h);
    EPS_Heater_Control(6);   h = EPS_Heater_Read(); TEST_CHECK("heater OFF above band   (+6C, exp=0)", false, h);
    EPS_Heater_Control(3);   h = EPS_Heater_Read(); TEST_CHECK("heater OFF in band hold (+3C, exp=0)", false, h);
    EPS_Test_Set_Auto_Heat(false);
    EPS_Heater_Control(-10); h = EPS_Heater_Read(); TEST_CHECK("heater OFF disabled    (-10C, exp=0)", false, h);
}

/* ════════════════════════ 4. E2E scenarios (live tasks) ══════════════════ */

static void e2e_nominal_sunlit(void)
{
    /* Setup: 3.9V battery charging in sunlight. No heater. Mock sensors active.
     * Verifies: correct NOMINAL state bit, heater off, and byte-exact telemetry in flash.
     * Note: mailbox bytes are checked against hand-decoded values, not against Pack()
     *       output — this makes the check independent of EPS_Pack_Telemetry() itself.
     * BREAKPOINT A: after wait_cycles — inspect batteryStatus event group.
     * BREAKPOINT B: after read_mailbox — inspect mailbox[] byte by byte. */
    printf("  [setup] battery=3899mV charging=yes eclipse=no heater=off\r\n");
    Battery_Telemetry_t batt = nominal_battery();
    EPS_Status_t        pmic = sunlit_pmic();
    DS2782_Set_Mock_Values(&batt);
    LTC4040_Set_Mock_Values(&pmic);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));

    /* BREAKPOINT A */
    uint32_t bat_bits = xEventGroupGetBits(batteryStatus) & ALL_BATTERY_STATES;
    TEST_CHECK("battery state = NOMINAL (0x01)",   EV_BAT_NOMINAL, bat_bits);
    TEST_CHECK("heater GPIO = OFF (0)",             false,          EPS_Heater_Read());

    /* BREAKPOINT B — byte-by-byte, independent of EPS_Pack_Telemetry():
     * raw_voltage=799=0x031F -> lo=0x1F hi=0x03  [0-1]
     * raw_temperature=200=0x00C8 -> lo=0xC8 hi=0x00  [2-3]
     * rel_cap=80=0x50  [4]
     * clprog=2048=0x0800 -> lo=0x00 hi=0x08  [5-6]
     * system_status: is_charging=1 -> 0x01  [7] */
    uint8_t mailbox[sizeof(OBDH_Payload_t)];
    read_mailbox(mailbox);
    TEST_CHECK("mailbox vbat_raw   lo (799->0x1F)",    0x1F, mailbox[0]);
    TEST_CHECK("mailbox vbat_raw   hi (799->0x03)",    0x03, mailbox[1]);
    TEST_CHECK("mailbox temp_raw   lo (200->0xC8)",    0xC8, mailbox[2]);
    TEST_CHECK("mailbox temp_raw   hi (200->0x00)",    0x00, mailbox[3]);
    TEST_CHECK("mailbox rel_cap_raw    (80->0x50)",    0x50, mailbox[4]);
    TEST_CHECK("mailbox clprog_adc lo (2048->0x00)",   0x00, mailbox[5]);
    TEST_CHECK("mailbox clprog_adc hi (2048->0x08)",   0x08, mailbox[6]);
    TEST_CHECK("mailbox system_status (charging=0x01)", 0x01, mailbox[7]);
    dump_mailbox("e2e_nominal_sunlit");
}

static void e2e_eclipse(void)
{
    /* Setup: satellite in eclipse — no solar input, not charging.
     * Verifies: eclipse bit (2) set, charging bit (0) clear in system_status.
     * Expected system_status = 0x04 (only eclipse bit).
     * BREAKPOINT: after read_mailbox — check mailbox[7] = 0x04. */
    printf("  [setup] eclipse=yes charging=no\r\n");
    EPS_Status_t pmic = sunlit_pmic();
    pmic.is_charging = false;
    pmic.is_eclipse  = true;
    LTC4040_Set_Mock_Values(&pmic);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));

    uint8_t mailbox[sizeof(OBDH_Payload_t)];
    read_mailbox(mailbox);
    /* BREAKPOINT */
    TEST_CHECK("system_status bit2 eclipse set   (0x04)", 0x04, mailbox[7] & (1u << 2));
    TEST_CHECK("system_status bit0 charging clear (0x00)", 0x00, mailbox[7] & (1u << 0));
    TEST_CHECK("system_status full byte           (0x04)", 0x04, mailbox[7]);
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
    TEST_CHECK("PB10 heater GPIO HIGH (heater ON at -5C)",       true,  EPS_Heater_Read());
    uint8_t mailbox[sizeof(OBDH_Payload_t)];
    read_mailbox(mailbox);
    TEST_CHECK("system_status bit5 heater_state=1 (0x20)",       0x20, mailbox[7] & (1u << 5));
    TEST_CHECK("system_status bit4 auto_heater=1  (0x10)",       0x10, mailbox[7] & (1u << 4));

    printf("  [step 3] inject 25C + N_EPS_DISABLE_AUTO_HEAT -> heater OFF, flash=0\r\n");
    set_battery(3900, 25);
    xTaskNotify(eps_handle, N_EPS_DISABLE_AUTO_HEAT, eSetBits);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));
    /* BREAKPOINT C */
    TEST_CHECK("PB10 heater GPIO LOW (heater OFF after disable)", false, EPS_Heater_Read());
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

static void e2e_battery_sensor_failure(void)
{
    /* Verifies: when DS2782 read fails, battery_read_failure bit (6) is set
     * in system_status. Battery raw fields retain the last good values.
     * Consumers must check bit6 before trusting any battery field.
     * Expected system_status = 0x41 (charging=1, batt_fail=1).
     * BREAKPOINT: after wait_cycles — inspect mailbox[7], expect bit6=1. */
    printf("  [setup] DS2782 read failure injected\r\n");
    DS2782_Set_Mock_Fail(true);
    TEST_CHECK_BOOL("wait_cycles(2) completed in time", wait_cycles(2, 5000));

    uint8_t mailbox[sizeof(OBDH_Payload_t)];
    read_mailbox(mailbox);
    /* BREAKPOINT */
    TEST_CHECK("system_status bit6 battery_read_fail=1 (0x40)", 0x40, mailbox[7] & (1u << 6));
    TEST_CHECK("system_status full byte (charging+fail = 0x41)", 0x41, mailbox[7]);
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

/* ════════════════════════ 5. Runner task ═════════════════════════════════ */

static void test_runner(void *pv)
{
    (void)pv;
    const bool USE_MOCK_SENSORS = true;   /* <-- set false for hardware checkout */

    printf("\r\n========= EPS TEST FIRMWARE =========\r\n");
    DS2782_Set_Mock_Mode(USE_MOCK_SENSORS);

    batteryStatus = xEventGroupCreate();
    xEventGroupSetBits(batteryStatus, EV_BAT_NOMINAL);

    printf("--- unit tests ---\r\n");
    RUN_TEST(test_compute_voltage);
    // RUN_TEST(test_compute_current);  // enable with raw_current
    RUN_TEST(test_compute_temperature);
    RUN_TEST(test_unit_consistency);
    RUN_TEST(test_payload_bytes);
    RUN_TEST(test_status_bits);
    RUN_TEST(test_pack_null_safety);
    RUN_TEST(test_state_machine_boundaries);
    RUN_TEST(test_heater_hysteresis);

    printf("--- e2e scenarios ---\r\n");
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

    tests_done();
}

/* ════════════════════════ 6. main() + glue ═══════════════════════════════ */

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

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;                 /* PMIC pins are open-drain active-LOW */
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_5;      /* PB2 !CHRG, PB5 !PFO */
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_4;                   /* PC4 !FAULT */
    HAL_GPIO_Init(GPIOC, &gpio);
}

/** Same I2C1 setup as flight main.c (needed for the future hardware checkout). */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x10909CEC;          /* 100 kHz @ 80 MHz */
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
        Error_Handler();
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
    MX_I2C1_Init();

    obdh_queue_handle = xQueueCreate(10, sizeof(obdh_request));
    if (obdh_queue_handle == NULL)
        Error_Handler();

    xTaskCreate(obdh_task,   "OBDH",    OBDH_STACK_SIZE, NULL, OBDH_PRIORITY, NULL);
    xTaskCreate(test_runner, "TESTRUN", 1024,            NULL, 2,             NULL);
    xTaskCreate(wdog_task,   "WDOG",    128,             NULL, 1,             NULL);

    vTaskStartScheduler();
    return 0;
}
