/**
 * @file test_beacon.c
 * @brief Host tests for the beacon: 19-byte body layout, uptime, timer callback, period change, and the hand-over to historic telemetry.
 * @date 2026-10-29
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/beacon.c:
 *   - `build_beacon_body()`: the 19-byte instant-telemetry body (spec §6.6);
 *   - `beacon_timer_cb()`: asks the beacon task to transmit;
 *   - `beacon_set_period()`: stores the period and re-arms the timer;
 *   - `transmit_beacon()`: authenticated, un-ACKed AIR_BEACON frame into the
 *     TX queue, **and** every beacon is also fed to historic telemetry.
 * - The module level effect of the known issues KI-15 (period 0) and KI-17
 *   (uptime from the RTC).
 *
 * `beacon_task()` is an endless loop and is not driven; the tests call the
 * `static` helpers directly.
 *
 * **Beacon body layout** (byte index: field)
 * 0 body version, 1..4 epoch (big-endian), 5 OBC state, 6 health flags (0x3F
 * placeholder), 7 MCU temperature (int8), 8 battery temperature (dummy 0),
 * 9 VDDA in units of 100 mV, 10 battery current (dummy), 11 deployment
 * (dummy), 12 last TC id, 13 last TC result, 14 LoRa CFG_ID, 15..18 uptime in
 * seconds (big-endian).
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below: `beacon.c`, `ht_handling.c`, `lora_cfg.c`,
 *   `task_management.c`, `health.c`.
 * - SOURCES: `notifications.c`, `frame.c`, `auth.c` (stateless helpers),
 *   `stub_task_entries.c`, `fake_obdh_requests.c`, `fake_comms_env.c`,
 *   `mock_pocket_plus.c`.
 * - `fake_comms_env.c` defines `g_last_tc_id`, `g_beacon_period_ms`,
 *   `beacon_set_period()` weakly; the real ones from `beacon.c` win.
 *
 * **Fake setup**
 * - `mock_pocket_plus.c` compresses every beacon to exactly 20 bytes. A block
 *   therefore holds 1 raw reference + 4 compressed beacons (20 + 4*20 = 100
 *   bytes of the 116-byte payload) = 5 beacons, and the 6th beacon flushes
 *   the block to flash.
 * - The RTC, sensors and TX queue come from `fake_comms_env`; flash from
 *   `fake_obdh_requests`.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_beacon
 * docker run --rm -v "${PWD}:/app" pocat-tests test_beacon -f uptime
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_comms_env.h"
#include "fake_obdh_requests.h"
#include "pocat_test_main.h"

#include "beacon.c"
#include "ht_handling.c"
#include "lora_cfg.c"
#include "task_management.c"
#include "health.c"

/**
 * @brief Runs before every test: fresh fakes, the 7 tasks created, an empty historic-telemetry queue and no beacon timer.
 * @details Order matters:
 * 1. fake RTOS / HAL reset and reset of the task_management.c and health.c
 *    statics, then `health_init()`;
 * 2. `fake_comms_env_reset()` (RTC 1 700 000 000, `g_last_tc_*`,
 *    `g_beacon_period_ms`, sensors, TX queue);
 * 3. `beacon_timer = NULL` (static of beacon.c; a stale handle would point at a
 *    timer freed by `fake_rtos_reset()`);
 * 4. `lora_cfg_init()` (CFG_ID = 1);
 * 5. `fake_obdh_reset(); ht_init(); fake_obdh_reset();`: `ht_init()` zeroes the
 *    HT statics from erased flash, and the second reset clears the 64 reads it
 *    logged so tests see only their own flash calls;
 * 6. `tm_create_tasks(TM_TASK_ALL, 0)` creates BEACON and TRANSCEIVER, whose
 *    handles the beacon code looks up.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();

    for (size_t i = 0; i < TASK_TABLE_SIZE; i++) {
        task_table[i].handle = NULL;
        task_table[i].paused = false;
    }
    task_event_group_handle = NULL;

    health_eg = NULL;
    expected_bits = 0;
    period_ticks = 0;
    next_deadline = 0;
    iwdg_handle = NULL;
    health_init();

    fake_comms_env_reset();
    beacon_timer = NULL;
    lora_cfg_init();

    fake_obdh_reset();
    ht_init();
    fake_obdh_reset();

    TEST_ASSERT_EQUAL_INT(pdPASS, tm_create_tasks(TM_TASK_ALL, 0));
}

/**
 * @brief Runs after every test: checks that the RTOS fake is balanced.
 * @details An unclosed critical section or scheduler suspend would freeze the satellite.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/**
 * @brief Helper: presets the flash values the beacon reads (current OBC state byte and boot time).
 * @param state     OBC state stored at `CURRENT_STATE_ADDR`.
 * @param boot_time Unix boot time stored at `BOOT_TIME_ADDR` (host and target are both little-endian).
 */
static void preset_flash(uint8_t state, uint32_t boot_time)
{
    fake_obdh_flash(CURRENT_STATE_ADDR)[0] = state;
    memcpy(fake_obdh_flash(BOOT_TIME_ADDR), &boot_time, sizeof(boot_time));
}

/**
 * @brief Helper: decodes a big-endian 32-bit value.
 * @param p Pointer to 4 bytes.
 * @return The value.
 */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/**
 * @brief Helper: pops one frame from the TX queue and returns its entry.
 * @details The queue holds only `COMMS_QUEUE_LEN` (2) entries; later frames
 * are silently dropped, so tests that send several beacons drain it each time.
 * @return The popped entry.
 */
static TxQueueEntry_t pop_tx(void)
{
    TxQueueEntry_t e;
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(comms_get_tx_queue(), &e, 0));
    return e;
}

/* =========================================================================
 * 1. Body layout and fields
 * ========================================================================= */

/**
 * @brief `build_beacon_body()` returns 19 bytes with the documented layout.
 * @details Arrange now = 1 700 000 000, state CM, boot = now - 3600, MCU
 * temperature 25 C, VDDA 3300 mV, last TC id 0x05, result 0x02. Expected the
 * bytes listed in the file header: version, big-endian epoch, state 2, 0x3F,
 * 25, 0, 33, 0, 0, 0x05, 0x02, CFG_ID 1, big-endian uptime 3600. The ground
 * station decodes beacons by these offsets, so any shift breaks all
 * telemetry.
 */
void test_body_is_19_bytes_with_spec_layout(void)
{
    uint32_t now = 1700000000u;
    fake_time_set_now(now);
    preset_flash(OBC_STATE_CM, now - 3600u);
    fake_sensors_set(25, 3300);
    g_last_tc_id = 0x05;
    g_last_tc_rc = 0x02;

    uint8_t b[19];
    TEST_ASSERT_EQUAL_UINT8(19, build_beacon_body(b));
    TEST_ASSERT_EQUAL_UINT8(AIR_BODY_VER, b[0]);
    TEST_ASSERT_EQUAL_UINT32(now, be32(&b[1]));
    TEST_ASSERT_EQUAL_UINT8(OBC_STATE_CM, b[5]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, b[6]);
    TEST_ASSERT_EQUAL_UINT8(25, b[7]);
    TEST_ASSERT_EQUAL_UINT8(0, b[8]);
    TEST_ASSERT_EQUAL_UINT8(33, b[9]);
    TEST_ASSERT_EQUAL_UINT8(0, b[10]);
    TEST_ASSERT_EQUAL_UINT8(0, b[11]);
    TEST_ASSERT_EQUAL_HEX8(0x05, b[12]);
    TEST_ASSERT_EQUAL_HEX8(0x02, b[13]);
    TEST_ASSERT_EQUAL_UINT8(1, b[14]);
    TEST_ASSERT_EQUAL_UINT32(3600u, be32(&b[15]));
}

/**
 * @brief When no boot time has been written yet (erased flash), the uptime is reported as 0.
 * @details Erased flash reads 0xFFFFFFFF, which is in the future, so the code
 * clamps the uptime to 0 instead of producing a huge number.
 */
void test_uptime_is_zero_when_boot_time_not_written(void)
{
    uint8_t b[19];
    build_beacon_body(b);
    TEST_ASSERT_EQUAL_UINT32(0u, be32(&b[15]));
}

/**
 * @brief A negative MCU temperature is sent as a two's-complement byte (-10 C is 0xF6).
 * @details The ground decodes the field as int8; a wrong cast would show a
 * cold satellite as +246 C.
 */
void test_negative_temperature_is_twos_complement(void)
{
    fake_sensors_set(-10, 3300);
    uint8_t b[19];
    build_beacon_body(b);
    TEST_ASSERT_EQUAL_HEX8(0xF6, b[7]);
}

/**
 * @brief VDDA is sent in units of 100 mV, truncated: 3399 mV becomes 33.
 */
void test_vdda_is_truncated_to_100mv(void)
{
    fake_sensors_set(20, 3399);
    uint8_t b[19];
    build_beacon_body(b);
    TEST_ASSERT_EQUAL_UINT8(33, b[9]);
}

/**
 * @brief The CFG_ID field follows the committed LoRa configuration.
 * @details Commit a config with id 7. Expected byte 14 == 7. Matters because
 * the ground uses this field to know which radio parameters the satellite is
 * really using after a reconfiguration.
 */
void test_cfg_id_follows_committed_lora_config(void)
{
    LoraConfig_t c = {
        .cfg_id = 7, .target = 1, .frequency_hz = 869000000ul, .sf = 9, .bw = 0, .cr = 1,
        .tx_power_dbm = 14, .preamble_symbols = 16, .crc_on = 1, .iq_inverted = 0,
        .sync_word = 0x12, .commit_at_unix = 0, .revert_after_s = 0,
    };
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));
    TEST_ASSERT_TRUE(lora_cfg_tick());

    uint8_t b[19];
    build_beacon_body(b);
    TEST_ASSERT_EQUAL_UINT8(7, b[14]);
}

/**
 * @brief The uptime should keep counting correctly when the RTC is re-synchronised after boot.
 * @details Arrange: the RTC was at 2000-01-01 (946 684 800) at boot, so that
 * is the stored boot time. 100 s (100 000 ticks) pass, then a
 * `TC_UPLOAD_UNIX_TIME` sets the RTC to 1 700 000 000. Expected uptime: 100 s.
 * Actual: 753 315 200 s, because the uptime is `RTC now - boot_time` and both
 * values come from the RTC. Uptime is wrong by decades after the first time
 * sync (a backwards sync would give 0).
 * @bug KI-17 (test/README.md) expected 100, actual 753315200.
 */
void test_uptime_survives_rtc_resync(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-17: uptime is RTC-now minus RTC-boot-time, so an RTC sync after boot corrupts it");
    fake_time_set_now(946684800u);
    preset_flash(OBC_STATE_NM, 946684800u);
    fake_rtos_advance_ticks(100000u);
    fake_time_set_now(1700000000u);

    uint8_t b[19];
    build_beacon_body(b);
    TEST_ASSERT_EQUAL_UINT32(100u, be32(&b[15]));
}

/* =========================================================================
 * 2. Timer and period
 * ========================================================================= */

/**
 * @brief The timer callback asks the beacon task, and only it, to transmit.
 * @details Call `beacon_timer_cb(NULL)`. Expected: the BEACON task received
 * exactly `N_COMMS_TRANSMIT_BEACON` and no other notification was sent. The
 * callback runs in the FreeRTOS timer task, so it must not transmit itself.
 */
void test_timer_callback_requests_beacon_from_beacon_task(void)
{
    int before = fake_rtos_notify_log_count();
    beacon_timer_cb(NULL);

    TEST_ASSERT_EQUAL_HEX32(N_COMMS_TRANSMIT_BEACON, fake_rtos_bits_sent_to(tm_get_task_handle(TM_TASK_BEACON)));
    TEST_ASSERT_EQUAL_INT(before + 1, fake_rtos_notify_log_count());
}

/**
 * @brief If the beacon task does not exist (yet), the callback does nothing and does not crash.
 * @details Clear the task table first. Expected: no notification at all.
 */
void test_timer_callback_before_task_exists_is_safe(void)
{
    for (size_t i = 0; i < TASK_TABLE_SIZE; i++) task_table[i].handle = NULL;
    int before = fake_rtos_notify_log_count();
    beacon_timer_cb(NULL);
    TEST_ASSERT_EQUAL_INT(before, fake_rtos_notify_log_count());
}

/**
 * @brief `beacon_set_period()` re-arms a running timer and the timer still fires into the beacon task.
 * @details Create the timer as `beacon_task()` does, call
 * `beacon_set_period(60000)`. Expected: timer period 60 000 ticks, timer
 * active, `g_beacon_period_ms == 60000`; firing the timer notifies the
 * beacon task. Matters because TC_SET_BEACON_PERIOD must take effect without
 * a reboot.
 */
void test_set_period_changes_running_timer(void)
{
    beacon_timer = xTimerCreate("beacon", pdMS_TO_TICKS(g_beacon_period_ms), pdTRUE, NULL, beacon_timer_cb);
    TEST_ASSERT_NOT_NULL(beacon_timer);

    beacon_set_period(60000u);
    TEST_ASSERT_EQUAL_UINT32(60000u, (uint32_t)fake_rtos_timer_period(beacon_timer));
    TEST_ASSERT_TRUE(fake_rtos_timer_active(beacon_timer));
    TEST_ASSERT_EQUAL_UINT32(60000u, g_beacon_period_ms);

    fake_rtos_fire_timer(beacon_timer);
    TEST_ASSERT_EQUAL_HEX32(N_COMMS_TRANSMIT_BEACON, fake_rtos_bits_sent_to(tm_get_task_handle(TM_TASK_BEACON)));
}

/**
 * @brief Before the timer exists, `beacon_set_period()` only stores the value for `beacon_task()` to use at start-up.
 * @details Expected: `g_beacon_period_ms` changes and nothing else happens.
 */
void test_set_period_before_timer_only_stores_value(void)
{
    TEST_ASSERT_NULL(beacon_timer);
    beacon_set_period(45000u);
    TEST_ASSERT_EQUAL_UINT32(45000u, g_beacon_period_ms);
    TEST_ASSERT_NULL(beacon_timer);
}

/**
 * @brief A period of 0 must be refused and the previous period kept.
 * @details Arrange the running timer at 30 s, call `beacon_set_period(0)`.
 * Expected: period still 30 000 ms. Actual: `g_beacon_period_ms` becomes 0
 * and `xTimerChangePeriod(timer, 0)` is called. FreeRTOS asserts on a zero
 * period and this project's `configASSERT` loops forever, so one
 * authenticated telecommand would hang the OBC until the IWDG resets it.
 * @bug KI-15 (test/README.md) expected 30000, actual 0.
 */
void test_set_period_zero_keeps_previous_period(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-15: beacon_set_period(0) is accepted and passed to xTimerChangePeriod");
    beacon_timer = xTimerCreate("beacon", pdMS_TO_TICKS(g_beacon_period_ms), pdTRUE, NULL, beacon_timer_cb);
    beacon_set_period(0u);
    TEST_ASSERT_EQUAL_UINT32(BEACON_PERIOD_MS, g_beacon_period_ms);
    TEST_ASSERT_EQUAL_UINT32(BEACON_PERIOD_MS, (uint32_t)fake_rtos_timer_period(beacon_timer));
}

/* =========================================================================
 * 3. Transmit and historic telemetry
 * ========================================================================= */

/**
 * @brief `transmit_beacon()` queues one authenticated, un-ACKed AIR_BEACON frame carrying the body, and wakes the transceiver.
 * @details Expected: one TX queue entry; `air_decode()` accepts it (so the
 * HMAC tag is valid); type AIR_BEACON, length 19, payload equal to
 * `build_beacon_body()`; `needs_ack == 0`; the transceiver task got
 * `N_TRANSCEIVER_TX_READY_BIT`. A beacon with ACK would make the satellite
 * wait for ground replies that never come.
 */
void test_transmit_queues_unacked_beacon_frame(void)
{
    preset_flash(OBC_STATE_NM, 1700000000u - 10u);
    transmit_beacon();

    QueueHandle_t q = comms_get_tx_queue();
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)fake_rtos_queue_items(q));
    TxQueueEntry_t e = pop_tx();

    AirFrame_t f;
    TEST_ASSERT_EQUAL_INT(0, air_decode(e.frame, e.frame_len, &f));
    TEST_ASSERT_EQUAL_UINT8(AIR_BEACON, f.type);
    TEST_ASSERT_EQUAL_UINT8(19, f.len);
    uint8_t expect[19];
    build_beacon_body(expect);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, f.payload, 19);
    TEST_ASSERT_EQUAL_UINT8(0, e.needs_ack);
    TEST_ASSERT_EQUAL_HEX32(N_TRANSCEIVER_TX_READY_BIT,
                            fake_rtos_bits_sent_to(tm_get_task_handle(TM_TASK_TRANSCEIVER)));
}

/**
 * @brief Every transmitted beacon is also staged in the historic-telemetry block.
 * @details After one `transmit_beacon()`: the block has 1 beacon and 20 used
 * bytes; the raw body sits at `ht_block[12..30]` and the pad byte
 * `ht_block[31]` is 0. Matters because the historic telemetry the ground
 * downloads later is built exclusively from these calls.
 */
void test_transmit_feeds_historic_telemetry(void)
{
    transmit_beacon();
    uint8_t body[19];
    build_beacon_body(body);

    TEST_ASSERT_EQUAL_UINT8(1, ht_block_count);
    TEST_ASSERT_EQUAL_UINT16(20, ht_block_used);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, &ht_block[HT_SLOT_HEADER_SIZE], 19);
    TEST_ASSERT_EQUAL_UINT8(0, ht_block[HT_SLOT_HEADER_SIZE + 19]);
}

/**
 * @brief The sixth beacon flushes a full block (5 beacons) to HT slot 0 with one erase+program request.
 * @details With the fake compressor a block holds 5 beacons. Send 6, draining
 * the 2-entry TX queue after each. Expected: exactly one ERASE_PROGRAM at
 * `HT_BASE_ADDR` of 128 bytes, and the slot header says format 0x03, count 5.
 */
void test_sixth_beacon_flushes_block_to_ht_slot0(void)
{
    for (int i = 0; i < 6; i++) {
        transmit_beacon();
        (void)pop_tx();
    }

    int n = 0;
    for (int i = 0; i < fake_obdh_call_count(); i++) {
        fake_obdh_call_t c = fake_obdh_call_at(i);
        if (c.op == FAKE_OBDH_ERASE_PROGRAM) {
            TEST_ASSERT_EQUAL_HEX32(HT_BASE_ADDR, c.addr);
            TEST_ASSERT_EQUAL_UINT32(128u, (uint32_t)c.len);
            n++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, n);

    ht_slot_header h;
    memcpy(&h, fake_obdh_flash(HT_BASE_ADDR), sizeof(h));
    TEST_ASSERT_EQUAL_HEX8(HT_FORMAT_POCKET_V3, h.format);
    TEST_ASSERT_EQUAL_UINT8(5, h.count);
}

/**
 * @brief The beacon is archived in historic telemetry even if the downlink queue is full.
 * @details Fill the 2-entry TX queue, then transmit. Expected: the queue
 * still has 2 entries (the new frame is dropped), the call waited 100 ticks
 * for room, the transceiver was not woken, but `ht_block_count == 1`. The
 * history must not depend on whether the radio had room.
 */
void test_beacon_archived_even_when_tx_queue_full(void)
{
    QueueHandle_t q = comms_get_tx_queue();
    TxQueueEntry_t filler;
    memset(&filler, 0, sizeof(filler));
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(q, &filler, 0));
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(q, &filler, 0));
    TickType_t t0 = xTaskGetTickCount();

    transmit_beacon();

    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)fake_rtos_queue_items(q));
    TEST_ASSERT_EQUAL_UINT32(100u, (uint32_t)(xTaskGetTickCount() - t0));
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rtos_bits_sent_to(tm_get_task_handle(TM_TASK_TRANSCEIVER)));
    TEST_ASSERT_EQUAL_UINT8(1, ht_block_count);
}

/**
 * @brief Test runner: registers and runs every test of the suite.
 * @param argc Argument count (the `-l`, `-f`, `-n`, `-x` options are handled by pocat_parse_args()).
 * @param argv Arguments.
 * @return Number of failed tests (0 = all pass).
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    POCAT_RUN_TEST(test_body_is_19_bytes_with_spec_layout);
    POCAT_RUN_TEST(test_uptime_is_zero_when_boot_time_not_written);
    POCAT_RUN_TEST(test_negative_temperature_is_twos_complement);
    POCAT_RUN_TEST(test_vdda_is_truncated_to_100mv);
    POCAT_RUN_TEST(test_cfg_id_follows_committed_lora_config);
    POCAT_RUN_TEST(test_uptime_survives_rtc_resync);
    POCAT_RUN_TEST(test_timer_callback_requests_beacon_from_beacon_task);
    POCAT_RUN_TEST(test_timer_callback_before_task_exists_is_safe);
    POCAT_RUN_TEST(test_set_period_changes_running_timer);
    POCAT_RUN_TEST(test_set_period_before_timer_only_stores_value);
    POCAT_RUN_TEST(test_set_period_zero_keeps_previous_period);
    POCAT_RUN_TEST(test_transmit_queues_unacked_beacon_frame);
    POCAT_RUN_TEST(test_transmit_feeds_historic_telemetry);
    POCAT_RUN_TEST(test_sixth_beacon_flushes_block_to_ht_slot0);
    POCAT_RUN_TEST(test_beacon_archived_even_when_tx_queue_full);
    return POCAT_END();
}
