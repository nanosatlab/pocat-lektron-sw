/**
 * @file test_param_store.c
 * @brief Host tests for the parameter store: values by ID in two flash copies, safe against power cuts.
 * @date 2026-10-27
 *
 * @details
 * **What is under test**
 * - src/subsystems/obdh/param_store.c: param_get(), param_set_request(),
 *   param_reset_request(), the load (param_store_load(), at boot and each time
 *   OBDH starts) and the writes the OBDH task runs (param_store_write()).
 * - src/utils/crc32.c, which the records use.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `mocks/fake_flash_driver.c` replaces the flash driver with a RAM copy of
 *   the data area. It programs and erases like the STM32L4 (only 1 to 0, 8
 *   bytes at a time, whole 2 KB pages), and can cut the power at any program
 *   or erase step and inject ECC errors.
 * - obdh_param_write_request() is defined below: it runs param_store_write()
 *   at once, as the OBDH task. The request protocol itself is tested in
 *   test_obdh_requests.c.
 * - The store accepts only the OBDH task, or main() before the scheduler
 *   starts (spec FLS-REQ-110). tm_get_task_handle() is faked below to name an
 *   OBDH task, and the tests run as that task unless they say otherwise.
 * - A "reboot" is fake_flashdrv_power_on() followed by param_store_load(),
 *   which rebuilds every value from flash alone.
 *
 * **Sizes used below** (spec §5.4): a half holds 8,184 bytes of records; the
 * beacon period takes 24 bytes per record, the TLE 160, the PoL mask 24.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_param_store
 * docker run --rm -v "${PWD}:/app" pocat-tests test_param_store -f power_cut
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_flash_driver.h"
#include "param_store.h"
#include "obdh_requests.h"
#include "task_management.h"
#include "crc32.h"
#include "pocat_test_main.h"
#include <string.h>

#define HALF      (FLASH_PARAM_SIZE / 2u)
#define TLE_SIZE  138u

static TickType_t last_block_time;   /**< Block time of the last request that reached the seam below. */

static TaskHandle_t obdh_task_handle;    /**< The OBDH task, created in setUp(): what tm_get_task_handle(TM_TASK_OBDH) returns. */
static TaskHandle_t comms_task_handle;   /**< Another task, created in setUp(), whose calls the store must refuse. */

/** @brief Fake of the task manager: the store asks it only for the OBDH task, to check its caller. */
TaskHandle_t tm_get_task_handle(uint32_t task_bit)
{
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(TM_TASK_OBDH, task_bit, "only the OBDH task is asked for");
    return obdh_task_handle;
}

static void task_stub(void *p) { (void)p; }

/** @brief The OBDH task, run at once: param_set_request() and param_reset_request() end here, and run as the OBDH task. */
HAL_StatusTypeDef obdh_param_write_request(uint8_t id, const uint8_t *data, size_t len, TickType_t ticks_to_wait)
{
    last_block_time = ticks_to_wait;
    TaskHandle_t caller = fake_rtos_current_task();
    fake_rtos_set_current_task(obdh_task_handle);
    HAL_StatusTypeDef status = param_store_write(id, data, len);
    fake_rtos_set_current_task(caller);
    return status;
}

/* ---- Helpers ---- */

/** @brief Power back on and rebuild the store from flash, as at a reset. */
static void reboot(void)
{
    fake_flashdrv_power_on();
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_load());
}

static HAL_StatusTypeDef set_beacon_period(uint16_t seconds)
{
    return param_set_request(PARAM_BEACON_PERIOD, &seconds, sizeof(seconds), OBDH_REQUEST_WAIT);
}

static uint16_t beacon_period(void)
{
    uint16_t seconds = 0;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_BEACON_PERIOD, &seconds, sizeof(seconds)));
    return seconds;
}

static void tle_pattern(uint8_t *tle, uint8_t seed)
{
    for (uint32_t i = 0; i < TLE_SIZE; i++) tle[i] = (uint8_t)(seed + i * 7u);
}

static void set_tle(uint8_t seed)
{
    uint8_t tle[TLE_SIZE];
    tle_pattern(tle, seed);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_set_request(PARAM_TLE, tle, sizeof(tle), OBDH_REQUEST_WAIT));
}

static void assert_tle(uint8_t seed)
{
    uint8_t expected[TLE_SIZE], got[TLE_SIZE];
    tle_pattern(expected, seed);
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_OK, param_get(PARAM_TLE, got, sizeof(got)), "the TLE was lost");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, got, TLE_SIZE, "the TLE changed");
}

static uint32_t read_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, sizeof(v)); return v; }
static uint16_t read_u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, sizeof(v)); return v; }

/** @brief Destroys one whole copy (both halves), as if its pages had been overwritten. */
static void destroy_copy(flash_region_id_t region)
{
    memset(fake_flashdrv_bytes(region, 0), 0x00, FLASH_PARAM_SIZE);
}

/** @brief Runs before every test: erased flash, no faults, the OBDH task current, and the store rebuilt from flash (all defaults). */
void setUp(void)
{
    fake_rtos_reset();
    fake_flashdrv_reset();
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(task_stub, "OBDH", 512, NULL, 5, &obdh_task_handle));
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(task_stub, "COMMS", 512, NULL, 4, &comms_task_handle));
    fake_rtos_set_current_task(obdh_task_handle);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_load());
}

/** @brief Runs after every test: param_get() and the writes left no critical section open. */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_critical_depth());
}

/* =========================================================================
 * 1. BASICS
 * ========================================================================= */

/** @brief The shared CRC-32 is CRC-32/ISO-HDLC: its check value for "123456789" is 0xCBF43926. */
void test_crc32_check_value(void)
{
    const uint8_t text[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_finish(crc32_update(CRC32_INIT, text, 9)));
}

/**
 * @brief With blank flash every parameter has its default, and the scan at boot writes nothing (FLS-BOOT-010, FLS-BOOT-050).
 */
void test_blank_flash_gives_the_defaults(void)
{
    uint16_t period = 0;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_BEACON_PERIOD, &period, sizeof(period)));
    TEST_ASSERT_EQUAL_UINT16(30, period);

    uint8_t tle[TLE_SIZE];
    memset(tle, 0xAA, sizeof(tle));
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_TLE, tle, sizeof(tle)));
    TEST_ASSERT_EACH_EQUAL_UINT8(0, tle, sizeof(tle));

    TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_programs());
    TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_erases(FLASH_REGION_PARAM_A, 0));
}

/** @brief A value written survives a reboot, read back from flash. */
void test_a_value_survives_a_reboot(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());

    reboot();
    uint16_t period = 0;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_BEACON_PERIOD, &period, sizeof(period)));
    TEST_ASSERT_EQUAL_UINT16(60, period);
}

/**
 * @brief The first write stores the same half header and record in both copies (spec §5.4).
 * @details The first doubleword holds half sequence 1 and its inverted CRC-32. The
 * beacon record starts at offset 8 and contains ID 0x42, length 2, check
 * 0xFDBD, sequence 1, the value 60 padded with 0xFF, and its CRC trailer.
 */
void test_record_layout_matches_the_spec(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));

    const uint8_t *a = fake_flashdrv_bytes(FLASH_REGION_PARAM_A, 0);
    TEST_ASSERT_EQUAL_UINT32(1, read_u32(a));
    TEST_ASSERT_EQUAL_HEX32(~crc32_finish(crc32_update(CRC32_INIT, a, 4)), read_u32(a + 4));

    const uint8_t *r = a + 8;
    TEST_ASSERT_EQUAL_HEX8(0x42, r[0]);
    TEST_ASSERT_EQUAL_HEX8(2, r[1]);
    TEST_ASSERT_EQUAL_HEX16(0xFDBD, read_u16(r + 2));
    TEST_ASSERT_EQUAL_UINT32(1, read_u32(r + 4));
    TEST_ASSERT_EQUAL_HEX8(60, r[8]);
    TEST_ASSERT_EQUAL_HEX8(0, r[9]);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFF, r + 10, 6);
    uint32_t crc = crc32_finish(crc32_update(crc32_update(CRC32_INIT, r, 8), r + 8, 2));
    TEST_ASSERT_EQUAL_HEX32(crc, read_u32(r + 16));
    TEST_ASSERT_EQUAL_HEX32(~crc, read_u32(r + 20));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFF, r + 24, 64);

    const uint8_t *b = fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 0);
    TEST_ASSERT_EQUAL_MEMORY(a, b, 8 + 24);
}

/** @brief Writing the value already stored programs nothing, before or after a reboot (FLS-DRV-070). */
void test_an_unchanged_value_is_not_written_again(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    uint32_t programs = fake_flashdrv_programs();

    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    TEST_ASSERT_EQUAL_UINT32(programs, fake_flashdrv_programs());

    reboot();
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    TEST_ASSERT_EQUAL_UINT32(programs, fake_flashdrv_programs());
}

/**
 * @brief param_reset_request() brings back the default, for good; resetting what is already the default writes nothing.
 * @details The reset is an ordinary write of the default value (30 s), so it
 * programs one record per copy; param_get() gives the default value again.
 */
void test_reset_returns_to_the_default(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    uint32_t before_reset = fake_flashdrv_programs();
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_reset_request(PARAM_BEACON_PERIOD, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_UINT32(before_reset + 6u, fake_flashdrv_programs());   /* 3 doublewords in each copy */

    uint16_t period = 0;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_BEACON_PERIOD, &period, sizeof(period)));
    TEST_ASSERT_EQUAL_UINT16(30, period);

    reboot();
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_BEACON_PERIOD, &period, sizeof(period)));
    TEST_ASSERT_EQUAL_UINT16(30, period);

    uint32_t programs = fake_flashdrv_programs();
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_reset_request(PARAM_BEACON_PERIOD, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_reset_request(PARAM_TX_INHIBIT, OBDH_REQUEST_WAIT));   /* never written */
    TEST_ASSERT_EQUAL_UINT32(programs, fake_flashdrv_programs());
}

/** @brief Unknown IDs, wrong sizes and NULL buffers are refused, and nothing is written. */
void test_wrong_size_or_unknown_id_is_refused(void)
{
    uint16_t v = 5;
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_set_request(PARAM_BEACON_PERIOD, &v, 1, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_set_request(PARAM_BEACON_PERIOD, NULL, sizeof(v), OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_set_request((param_id_t)0x99, &v, sizeof(v), OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_reset_request((param_id_t)0x99, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_store_write(0x99, (const uint8_t *)&v, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_store_write(PARAM_BEACON_PERIOD, NULL, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_store_write(PARAM_BEACON_PERIOD, NULL, 0));   /* no special "empty" write */
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_store_write(PARAM_BEACON_PERIOD, (const uint8_t *)&v, 0));

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_get(PARAM_BEACON_PERIOD, &v, 1));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_get(PARAM_BEACON_PERIOD, NULL, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_get((param_id_t)0x99, &v, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_get((param_id_t)0x142, &v, sizeof(v)));   /* not ID 0x42, the beacon period */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, v, "a refused param_get() wrote the buffer");
    TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_programs());
}

/**
 * @brief A stored record of an ID this software does not have is ignored at loading, and the other values still load.
 * @details Flash written by an older version can hold such records, such as
 * the boot mode, ID 0x01, which this version no longer has. The test turns
 * the stored PoL mask record into one of ID 0x01, with a valid header check
 * and CRC, in both copies.
 */
void test_a_stored_record_of_an_unknown_id_is_ignored(void)
{
    const uint8_t mask = 5u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_set_request(PARAM_POL_MASK, &mask, sizeof(mask), OBDH_REQUEST_WAIT));   /* the first record, at offset 8 */
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));

    const flash_region_id_t copies[] = { FLASH_REGION_PARAM_A, FLASH_REGION_PARAM_B };
    for (size_t i = 0; i < 2u; i++) {
        uint8_t *r = fake_flashdrv_bytes(copies[i], 8);
        TEST_ASSERT_EQUAL_HEX8(PARAM_POL_MASK, r[0]);
        r[0] = 0x01u;
        uint16_t check = (uint16_t)~(0x01u | (1u << 8));   /* ~(id | len << 8) */
        memcpy(r + 2, &check, sizeof(check));
        uint32_t crc = crc32_finish(crc32_update(crc32_update(CRC32_INIT, r, 8), r + 8, 1));
        uint32_t inverted = ~crc;
        memcpy(r + 16, &crc, sizeof(crc));
        memcpy(r + 20, &inverted, sizeof(inverted));
    }

    reboot();

    uint8_t got = 0xFFu;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_POL_MASK, &got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, got, "the record of ID 0x01 was taken for another parameter");
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(90));
    reboot();
    TEST_ASSERT_EQUAL_UINT16(90, beacon_period());
}

/** @brief The requests hand the caller's block time to OBDH unchanged. */
void test_requests_pass_the_block_time(void)
{
    uint16_t v = 45;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_set_request(PARAM_BEACON_PERIOD, &v, sizeof(v), 123));
    TEST_ASSERT_EQUAL_UINT32(123, last_block_time);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_reset_request(PARAM_BEACON_PERIOD, 77));
    TEST_ASSERT_EQUAL_UINT32(77, last_block_time);
}

/**
 * @brief Sequence numbers continue from flash after a reboot, so a newer value always wins.
 * @details If they restarted at 1, the value written after the reboot would
 * tie with the older one and could lose to it.
 */
void test_sequence_numbers_continue_after_a_reboot(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    reboot();
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(61));
    reboot();
    TEST_ASSERT_EQUAL_UINT16(61, beacon_period());
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(62));
    reboot();
    TEST_ASSERT_EQUAL_UINT16(62, beacon_period());
}

/* =========================================================================
 * 2. TWO COPIES
 * ========================================================================= */

/** @brief Either copy alone gives back every value; a lost copy is rebuilt at the next write. */
void test_either_copy_alone_holds_every_value(void)
{
    uint8_t on = 1, got = 0;
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    set_tle(1);

    destroy_copy(FLASH_REGION_PARAM_A);
    reboot();
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());
    assert_tle(1);

    TEST_ASSERT_EQUAL_INT(HAL_OK, param_set_request(PARAM_TX_INHIBIT, &on, 1, OBDH_REQUEST_WAIT));   /* rebuilds copy A */
    destroy_copy(FLASH_REGION_PARAM_B);
    reboot();
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());
    assert_tle(1);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_TX_INHIBIT, &got, 1));
    TEST_ASSERT_EQUAL_UINT8(1, got);
}

/**
 * @brief An ECC error in a record makes it invalid; the other copy, or else the older value, is used.
 * @details The first beacon record starts at offset 8. The second starts at
 * offset 32 and its data starts at 40.
 */
void test_an_ecc_error_in_a_record_falls_back(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(61));

    fake_flashdrv_inject_ecc(FLASH_REGION_PARAM_A, 40);
    reboot();
    TEST_ASSERT_EQUAL_UINT16(61, beacon_period());   /* from copy B */

    fake_flashdrv_inject_ecc(FLASH_REGION_PARAM_B, 40);
    reboot();
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());   /* the older record, still valid in both */
}

/**
 * @brief A copy that missed a write gets the record at the next write, without a compaction.
 * @details Copy B is put back as it was before the write of 90, as if the
 * write had reached copy A only. The next write (another parameter) appends
 * the missed record to B first. Then copy A is destroyed: B alone must give 90.
 */
void test_a_copy_that_missed_a_write_is_repaired_at_the_next_write(void)
{
    static uint8_t copy_b[FLASH_PARAM_SIZE];
    uint8_t on = 1;

    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    memcpy(copy_b, fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 0), sizeof(copy_b));
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(90));
    memcpy(fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 0), copy_b, sizeof(copy_b));

    reboot();
    TEST_ASSERT_EQUAL_UINT16(90, beacon_period());
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_set_request(PARAM_TX_INHIBIT, &on, 1, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_erases(FLASH_REGION_PARAM_B, 0));   /* appended, not compacted */

    destroy_copy(FLASH_REGION_PARAM_A);
    reboot();
    TEST_ASSERT_EQUAL_UINT16(90, beacon_period());
}

/**
 * @brief With invalid half headers throughout both regions, the store loads defaults and erases the target half on the first write.
 */
void test_invalid_halves_are_erased_before_reuse(void)
{
    memset(fake_flashdrv_bytes(FLASH_REGION_PARAM_A, 0), 0x5A, FLASH_PARAM_SIZE);
    memset(fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 0), 0x5A, FLASH_PARAM_SIZE);
    reboot();
    TEST_ASSERT_EQUAL_UINT16(30, beacon_period());
    TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_programs());

    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    TEST_ASSERT_EQUAL_UINT32(1, fake_flashdrv_erases(FLASH_REGION_PARAM_A, 0));
    TEST_ASSERT_EQUAL_UINT32(1, fake_flashdrv_erases(FLASH_REGION_PARAM_B, 0));
    reboot();
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());
}

/**
 * @brief A damaged half header prevents the scanner from loading even intact records in that half.
 * @details With one bad header the other copy supplies the stored value. When
 * both headers fail their CRC, the beacon period returns to its default.
 */
void test_a_bad_half_header_crc_invalidates_the_half(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    fake_flashdrv_bytes(FLASH_REGION_PARAM_A, 4)[0] ^= 1u;
    reboot();
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());

    fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 4)[0] ^= 1u;
    reboot();
    TEST_ASSERT_EQUAL_UINT16(30, beacon_period());
}

/* =========================================================================
 * 3. COMPACTION AND WEAR
 * ========================================================================= */

/** @brief Writes the TLE, the PoL mask and enough beacons to force compaction on the next write in both copies. */
static void fill_first_half(void)
{
    const uint8_t mask = 2u;
    set_tle(4);
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_set_request(PARAM_POL_MASK, &mask, sizeof(mask), OBDH_REQUEST_WAIT));
    /* 8,184 - 160 - 24 = 8,000 bytes left. 333 beacon records leave 8 bytes. */
    for (uint16_t i = 0; i < 333; i++) {
        TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period((uint16_t)(1000 + i)));
    }
}

static void assert_pol_mask(void)
{
    uint8_t mask = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_get(PARAM_POL_MASK, &mask, sizeof(mask)));
    TEST_ASSERT_EQUAL_UINT8(2, mask);
}

/**
 * @brief A full half is compacted: every value is kept, and only the old half is erased, once.
 */
void test_compaction_keeps_every_value_and_erases_one_half(void)
{
    fill_first_half();
    TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_erases(FLASH_REGION_PARAM_A, 0));

    param_store_stats_t before = param_store_stats();   /* counted since boot: earlier tests left their own counts */
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(7));   /* no room left: compacts both copies */
    param_store_stats_t after = param_store_stats();
    TEST_ASSERT_EQUAL_UINT32(before.compactions[0] + 1u, after.compactions[0]);
    TEST_ASSERT_EQUAL_UINT32(before.compactions[1] + 1u, after.compactions[1]);
    TEST_ASSERT_EQUAL_UINT32(before.copy_failures[0], after.copy_failures[0]);
    TEST_ASSERT_EQUAL_UINT32(before.copy_failures[1], after.copy_failures[1]);
    for (uint32_t page = 0; page < 4; page++) {
        TEST_ASSERT_EQUAL_UINT32(1, fake_flashdrv_erases(FLASH_REGION_PARAM_A, page * FLASH_MAP_PAGE_SIZE));
        TEST_ASSERT_EQUAL_UINT32(1, fake_flashdrv_erases(FLASH_REGION_PARAM_B, page * FLASH_MAP_PAGE_SIZE));
        TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_erases(FLASH_REGION_PARAM_A, HALF + page * FLASH_MAP_PAGE_SIZE));
        TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_erases(FLASH_REGION_PARAM_B, HALF + page * FLASH_MAP_PAGE_SIZE));
    }

    reboot();
    TEST_ASSERT_EQUAL_UINT16(7, beacon_period());
    assert_tle(4);
    assert_pol_mask();
}

/**
 * @brief A compaction whose new half header reads back wrong fails: the old half stays active and is not erased, and the next write rebuilds the copy.
 * @details The compaction of copy A targets half 1, whose header is at offset
 * HALF. The fake stores three of its bits wrong and reports no error, so only
 * the read-back can see it. Had the compaction gone on, it would have erased
 * half 0, and copy A would have had no valid half left. Copy B compacts
 * normally, so the write is still stored.
 */
void test_a_half_header_that_reads_back_wrong_keeps_the_old_half(void)
{
    static uint8_t copy_b[FLASH_PARAM_SIZE];

    fill_first_half();   /* the beacon period is now 1332 */
    param_store_stats_t before = param_store_stats();
    fake_flashdrv_inject_wrong_program(FLASH_REGION_PARAM_A, HALF);
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_OK, set_beacon_period(7), "copy B alone should have stored the value");
    param_store_stats_t after = param_store_stats();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.compactions[0], after.compactions[0], "a compaction whose half header read back wrong was counted");
    TEST_ASSERT_EQUAL_UINT32(before.copy_failures[0] + 1u, after.copy_failures[0]);
    TEST_ASSERT_EQUAL_UINT32(before.compactions[1] + 1u, after.compactions[1]);
    for (uint32_t page = 0; page < 4; page++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, fake_flashdrv_erases(FLASH_REGION_PARAM_A, page * FLASH_MAP_PAGE_SIZE), "copy A's old half was erased");
    }

    /* Copy A alone still gives the values from before the write */
    memcpy(copy_b, fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 0), sizeof(copy_b));
    destroy_copy(FLASH_REGION_PARAM_B);
    reboot();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1332, beacon_period(), "copy A alone lost the value from before the write");
    assert_tle(4);
    assert_pol_mask();
    memcpy(fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 0), copy_b, sizeof(copy_b));

    /* With both copies the new value, from B; the next write rebuilds A */
    reboot();
    TEST_ASSERT_EQUAL_UINT16(7, beacon_period());
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(9));
    destroy_copy(FLASH_REGION_PARAM_B);
    reboot();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(9, beacon_period(), "the next write did not rebuild copy A");
    assert_tle(4);
    assert_pol_mask();
}

/**
 * @brief Erases follow the spec §4 formula: 3,000 one-value writes erase each page 4 times.
 * @details bytes written / (half size - header - live set) / 2 =
 * 3,000 x 24 / (8,192 - 8 - 24) / 2 = 4.4, so 4 erases per page.
 */
void test_wear_follows_the_formula(void)
{
    for (uint16_t i = 0; i < 3000; i++) {
        TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(i));
    }

    for (uint32_t off = 0; off < FLASH_PARAM_SIZE; off += FLASH_MAP_PAGE_SIZE) {
        TEST_ASSERT_EQUAL_UINT32(4, fake_flashdrv_erases(FLASH_REGION_PARAM_A, off));
        TEST_ASSERT_EQUAL_UINT32(4, fake_flashdrv_erases(FLASH_REGION_PARAM_B, off));
    }
    reboot();
    TEST_ASSERT_EQUAL_UINT16(2999, beacon_period());
}

/**
 * @brief A reload, as when the OBDH task restarts, takes every value from flash again and keeps the counters.
 * @details OBDH may stop between writing flash and updating RAM, so when it
 * starts it loads the store again (spec §5.2.1 rule 5). Here flash is changed
 * behind the store's back: the reload must follow flash, not what RAM held.
 * The event counters count since boot, so the reload keeps them.
 */
void test_a_reload_follows_flash_and_keeps_the_counters(void)
{
    fill_first_half();
    param_store_stats_t before = param_store_stats();
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(7));   /* compacts both copies */
    TEST_ASSERT_EQUAL_UINT32(before.compactions[0] + 1u, param_store_stats().compactions[0]);

    param_store_load();
    TEST_ASSERT_EQUAL_UINT16(7, beacon_period());
    assert_tle(4);
    assert_pol_mask();
    TEST_ASSERT_EQUAL_UINT32(before.compactions[0] + 1u, param_store_stats().compactions[0]);
    TEST_ASSERT_EQUAL_UINT32(before.compactions[1] + 1u, param_store_stats().compactions[1]);

    destroy_copy(FLASH_REGION_PARAM_A);
    destroy_copy(FLASH_REGION_PARAM_B);
    param_store_load();
    TEST_ASSERT_EQUAL_UINT16(30, beacon_period());   /* nothing left in flash: the default */
}

/* =========================================================================
 * 4. POWER CUTS (spec FLS-INT-030)
 *    The cut moves one step at a time (one doubleword or one page erase)
 *    through a whole write, until the write finishes before the cut. After
 *    each cut and a reboot, every value must be old or new, a value reported
 *    stored must be new, each copy alone must also give old or new values
 *    (so a cut cannot leave one copy depending on the other), and the store
 *    must keep working.
 * ========================================================================= */

static void others_after_write(void) { assert_tle(3); }
static void others_after_compaction(void) { assert_tle(4); assert_pol_mask(); }

/**
 * @brief Checks each copy alone: the other one is destroyed, the store rebuilt from the remaining one, then both are put back.
 * @param old_period The beacon period before the write.
 * @param new_period The value being written.
 * @param check_others Checks the parameters the write did not touch.
 */
static void check_each_copy_alone(uint16_t old_period, uint16_t new_period, void (*check_others)(void))
{
    static uint8_t a[FLASH_PARAM_SIZE], b[FLASH_PARAM_SIZE];
    memcpy(a, fake_flashdrv_bytes(FLASH_REGION_PARAM_A, 0), sizeof(a));
    memcpy(b, fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 0), sizeof(b));

    for (int lost = 0; lost < 2; lost++) {
        destroy_copy((lost == 0) ? FLASH_REGION_PARAM_A : FLASH_REGION_PARAM_B);
        reboot();
        uint16_t period = beacon_period();
        TEST_ASSERT_TRUE_MESSAGE(period == old_period || period == new_period, "one copy alone gives neither the old nor the new value");
        check_others();
        memcpy(fake_flashdrv_bytes(FLASH_REGION_PARAM_A, 0), a, sizeof(a));
        memcpy(fake_flashdrv_bytes(FLASH_REGION_PARAM_B, 0), b, sizeof(b));
    }
    reboot();
}

/** @brief One cut point of a write of the beacon period from 60 to 90; true if the cut happened. */
static bool cut_during_a_write(long cut, bool ecc)
{
    fake_flashdrv_reset();
    fake_flashdrv_set_cut_ecc(ecc);
    param_store_load();
    set_tle(3);
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));

    param_store_stats_t before = param_store_stats();   /* counted since boot: a reload keeps them */
    fake_flashdrv_cut_power_after(cut);
    HAL_StatusTypeDef status = set_beacon_period(90);
    bool cut_happened = fake_flashdrv_power_is_cut();
    if (cut_happened) {
        param_store_stats_t after = param_store_stats();
        TEST_ASSERT_TRUE_MESSAGE(after.copy_failures[0] + after.copy_failures[1] > before.copy_failures[0] + before.copy_failures[1],
                                 "a write cut short was not counted");
    }

    reboot();
    uint16_t period = beacon_period();
    TEST_ASSERT_TRUE_MESSAGE(period == 60 || period == 90, "the value is neither the old nor the new one");
    if (status == HAL_OK) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(90, period, "a write reported as stored was lost");
    }
    assert_tle(3);
    check_each_copy_alone(60, 90, others_after_write);

    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_OK, set_beacon_period(120), "the store does not work after the cut");
    reboot();
    TEST_ASSERT_EQUAL_UINT16(120, beacon_period());
    assert_tle(3);
    return cut_happened;
}

static void sweep_write(bool ecc)
{
    long cut = 0;
    while (cut_during_a_write(cut, ecc)) {
        cut++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, cut, "a 2-byte value should take 3 doublewords in each copy");
}

/** @brief Power cut at every step of a write; the cut doubleword fails ECC, as on the satellite. */
void test_power_cut_during_a_write(void) { sweep_write(true); }

/** @brief Same, with the cut doubleword readable but half programmed. */
void test_power_cut_during_a_write_without_ecc_error(void) { sweep_write(false); }

/** @brief One cut point of a write that compacts both copies; true if the cut happened. */
static bool cut_during_a_compaction(long cut, bool ecc)
{
    fake_flashdrv_reset();
    fake_flashdrv_set_cut_ecc(ecc);
    param_store_load();
    fill_first_half();   /* the beacon period is now 1332 */

    fake_flashdrv_cut_power_after(cut);
    HAL_StatusTypeDef status = set_beacon_period(7);
    bool cut_happened = fake_flashdrv_power_is_cut();

    reboot();
    uint16_t period = beacon_period();
    TEST_ASSERT_TRUE_MESSAGE(period == 1332 || period == 7, "the value is neither the old nor the new one");
    if (status == HAL_OK) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(7, period, "a write reported as stored was lost");
    }
    assert_tle(4);
    assert_pol_mask();
    check_each_copy_alone(1332, 7, others_after_compaction);

    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_OK, set_beacon_period(9), "the store does not work after the cut");
    reboot();
    TEST_ASSERT_EQUAL_UINT16(9, beacon_period());
    assert_tle(4);
    assert_pol_mask();
    return cut_happened;
}

static void sweep_compaction(bool ecc)
{
    long cut = 0;
    while (cut_during_a_compaction(cut, ecc)) {
        cut++;
    }
    /* Each copy takes 26 doublewords for live records, 1 for the half header, 4 page erases and 3 doublewords for the new record. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(68, cut, "unexpected number of steps in a compacting write");
}

/** @brief Power cut at every step of a write that compacts both copies, with ECC errors on cut doublewords. */
void test_power_cut_during_a_compaction(void) { sweep_compaction(true); }

/** @brief Same, without ECC errors. */
void test_power_cut_during_a_compaction_without_ecc_error(void) { sweep_compaction(false); }

/* =========================================================================
 * 5. CALLERS (spec FLS-REQ-110)
 * ========================================================================= */

/**
 * @brief Called from a task other than OBDH, the load and the write return HAL_ERROR before they read, program or change anything.
 * @details The same write through param_set_request() is served by OBDH. A
 *          refused load that had cleared the store's state would have reset
 *          its sequence numbers: OBDH's next write would then lose to the older
 *          records at the reboot.
 */
void test_a_task_other_than_obdh_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(70));   /* records 1 and 2 */
    uint32_t reads = fake_flashdrv_reads();
    uint32_t programs = fake_flashdrv_programs();

    fake_rtos_set_current_task(comms_task_handle);
    const uint16_t v = 90;
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_store_write(PARAM_BEACON_PERIOD, (const uint8_t *)&v, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, param_store_load());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(reads, fake_flashdrv_reads(), "a refused call read flash");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(programs, fake_flashdrv_programs(), "a refused call programmed flash");
    TEST_ASSERT_EQUAL_UINT16(70, beacon_period());

    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(80));   /* from COMMS too: OBDH serves the request */
    fake_rtos_set_current_task(obdh_task_handle);
    reboot();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(80, beacon_period(), "the refused load reset the sequence numbers");
}

/** @brief Before the scheduler starts, main() loads and writes the store: no task runs then, whatever task the kernel names. */
void test_main_may_use_the_store_before_the_scheduler_starts(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    fake_rtos_set_current_task(comms_task_handle);
    fake_rtos_set_scheduler_state(taskSCHEDULER_NOT_STARTED);

    uint32_t reads = fake_flashdrv_reads();
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_load());
    TEST_ASSERT_TRUE_MESSAGE(fake_flashdrv_reads() > reads, "main()'s load read nothing");
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());
    const uint16_t v = 90;
    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_write(PARAM_BEACON_PERIOD, (const uint8_t *)&v, sizeof(v)));
    TEST_ASSERT_EQUAL_UINT16(90, beacon_period());

    fake_rtos_set_scheduler_state(taskSCHEDULER_RUNNING);
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_ERROR, param_store_load(), "once the scheduler runs, only OBDH may load");
}

/** @brief After the health monitor deletes and recreates OBDH, the new task is accepted: the check asks the task manager at every call. */
void test_a_restarted_obdh_task_is_accepted(void)
{
    vTaskDelete(obdh_task_handle);
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(task_stub, "OBDH", 512, NULL, 5, &obdh_task_handle));
    fake_rtos_set_current_task(obdh_task_handle);

    TEST_ASSERT_EQUAL_INT(HAL_OK, param_store_load());
    TEST_ASSERT_EQUAL_INT(HAL_OK, set_beacon_period(60));
    TEST_ASSERT_EQUAL_UINT16(60, beacon_period());
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();

    // 1. Basics
    POCAT_RUN_TEST(test_crc32_check_value);
    POCAT_RUN_TEST(test_blank_flash_gives_the_defaults);
    POCAT_RUN_TEST(test_a_value_survives_a_reboot);
    POCAT_RUN_TEST(test_record_layout_matches_the_spec);
    POCAT_RUN_TEST(test_an_unchanged_value_is_not_written_again);
    POCAT_RUN_TEST(test_reset_returns_to_the_default);
    POCAT_RUN_TEST(test_wrong_size_or_unknown_id_is_refused);
    POCAT_RUN_TEST(test_a_stored_record_of_an_unknown_id_is_ignored);
    POCAT_RUN_TEST(test_requests_pass_the_block_time);
    POCAT_RUN_TEST(test_sequence_numbers_continue_after_a_reboot);

    // 2. Two copies
    POCAT_RUN_TEST(test_either_copy_alone_holds_every_value);
    POCAT_RUN_TEST(test_an_ecc_error_in_a_record_falls_back);
    POCAT_RUN_TEST(test_a_copy_that_missed_a_write_is_repaired_at_the_next_write);
    POCAT_RUN_TEST(test_invalid_halves_are_erased_before_reuse);
    POCAT_RUN_TEST(test_a_bad_half_header_crc_invalidates_the_half);

    // 3. Compaction and wear
    POCAT_RUN_TEST(test_compaction_keeps_every_value_and_erases_one_half);
    POCAT_RUN_TEST(test_a_half_header_that_reads_back_wrong_keeps_the_old_half);
    POCAT_RUN_TEST(test_wear_follows_the_formula);
    POCAT_RUN_TEST(test_a_reload_follows_flash_and_keeps_the_counters);

    // 4. Power cuts
    POCAT_RUN_TEST(test_power_cut_during_a_write);
    POCAT_RUN_TEST(test_power_cut_during_a_write_without_ecc_error);
    POCAT_RUN_TEST(test_power_cut_during_a_compaction);
    POCAT_RUN_TEST(test_power_cut_during_a_compaction_without_ecc_error);

    // 5. Callers
    POCAT_RUN_TEST(test_a_task_other_than_obdh_is_refused);
    POCAT_RUN_TEST(test_main_may_use_the_store_before_the_scheduler_starts);
    POCAT_RUN_TEST(test_a_restarted_obdh_task_is_accepted);

    return POCAT_END();
}
