/**
 * @file test_tc_handler.c
 * @brief Telecommand catalogue: one host test per TC_ID, showing what the satellite does when the ground station sends it.
 * @date 2026-10-23
 *
 * @details
 * **What this suite is**: Layer 2 of the test design (docs/testing/test-design.md).
 * Every telecommand in the TC_ID catalogue (include/subsystems/comms/types.h,
 * §10) has a test named `test_TC_<NAME>_<effect>`. Each test sends that TC
 * to the real `tc_process()` and checks every visible effect:
 * - which task was notified, and with which bit (OBC, EPS, COMMS, OBDH,
 *   BEACON, PAYLOAD);
 * - what was written to flash through OBDH, and at which address;
 * - which TM or NACK frame was queued for transmission, decoded and checked
 *   field by field;
 * - which ARQ session was started or armed, the beacon period, and the RTC
 *   time.
 *
 * Use it as a list of "commands to try the satellite":
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_tc_handler -l                # the whole catalogue
 * docker run --rm -v "${PWD}:/app" pocat-tests test_tc_handler -f TC_REQUEST_HK  # one command
 * docker run --rm -v "${PWD}:/app" pocat-tests test_tc_handler -f LORA           # every LoRa-config case
 * @endcode
 *
 * **Where `tc_process()` sits**
 * Ground station → radio → transceiver_task (link layer: authentication,
 * ACK, dedup) → rx_queue → comms_task → **tc_process(frame)**. According to
 * the design described in the comments of tc_handler.c ("eager-ACK
 * design"), by the time `tc_process()` runs the frame is authenticated and
 * the air-level ACK has already been sent by the transceiver. This suite
 * doesn't check that part. So a TC that is rejected later answers with an
 * explicit **NACK frame** on the TX queue, and the transceiver is told with
 * `N_TRANSCEIVER_TX_READY_BIT`.
 *
 * **TC frame as built by send_tc()**: AIR_TC frame, seq `TC_SEQ`, payload =
 * `[BODY_VER, TC_ID, params...]`.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - Real code, `#include`d below:
 *   - `tc_handler.c`, under test;
 *   - `lora_cfg.c`, so the real range checks and the "busy" state of LoRa
 *     configuration run;
 *   - `tm_builder.c`, the real TM body layouts;
 *   - `task_management.c` + `health.c`, for the real task handles.
 * - Real code linked through SOURCES: `frame.c` + `auth.c` (NACK and TM
 *   frames are really encoded and signed with the test PSK), and
 *   `notifications.c`.
 * - Fakes:
 *   - `fake_comms_env.c`: comms queues, RTC time, beacon, ARQ, MCU sensors
 *     and radio; see its header for the table;
 *   - `fake_obdh_requests.c`: flash writes go to a RAM image;
 *   - `stub_task_entries.c`: the 7 task bodies, which never run.
 *
 * **Function-local statics**: `tc_process()` keeps three ARQ session counters
 * as `static` variables inside the function, which can't be reset. Tests
 * therefore check that the session id **increases by 1**, not its absolute
 * value.
 */
#include <stdio.h>
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_obdh_requests.h"
#include "fake_comms_env.h"
#include "pocat_test_main.h"

#include "tc_handler.c"
#include "lora_cfg.c"
#include "tm_builder.c"
#include "task_management.c"
#include "health.c"

/** @brief Sequence number of every TC sent by send_tc(). NACKs and TMs must refer back to it. */
#define TC_SEQ 0x42u

/** @brief The 1 700 000 000 s (Nov 2023) that fake_comms_env_reset() puts on the fake RTC, as the 4 big-endian bytes a TM header carries. */
static const uint8_t EPOCH_BE[4] = { 0x65, 0x53, 0xF1, 0x00 };

/* =========================================================================
 * Helpers
 * ========================================================================= */

/**
 * @brief Builds an authenticated-looking TC frame and passes it to `tc_process()`, just as comms_task does.
 * @param tc_id  Telecommand id (TC_* from types.h, or any byte to test unknown ids).
 * @param params Parameter bytes that follow BODY_VER and TC_ID (may be NULL if @p n is 0).
 * @param n      Number of parameter bytes. The frame length is 2 + n.
 */
static void send_tc(uint8_t tc_id, const uint8_t *params, uint8_t n)
{
    AirFrame_t f;
    memset(&f, 0, sizeof(f));
    f.ver = AIR_PROTOCOL_VER;
    f.type = AIR_TC;
    f.flags = AIR_FLAG_REQUIRES_ACK | AIR_FLAG_AUTH_TAG;
    f.seq = TC_SEQ;
    f.payload[0] = AIR_BODY_VER;
    f.payload[1] = tc_id;
    if (n > 0) memcpy(&f.payload[2], params, n);
    f.len = (uint8_t)(2u + n);
    tc_process(&f);
}

/** @brief Helper: returns the task handle for one `TM_TASK_*` bit. */
static TaskHandle_t task(uint32_t tm_bit) { return tm_get_task_handle(tm_bit); }

/**
 * @brief Helper: checks that @p target received exactly @p bits on index 0 and that no other task received anything.
 * @details It checks the 7 subsystem tasks and the OBC. Pass `target = NULL`
 * to check that nobody was notified.
 */
static void assert_only_notified(TaskHandle_t target, uint32_t bits)
{
    TaskHandle_t all[8];
    for (uint32_t i = 0; i < 7; i++) all[i] = task(1u << i);
    all[7] = fake_comms_env_obc_task();

    for (int i = 0; i < 8; i++) {
        uint32_t expected = (all[i] == target) ? bits : 0u;
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(expected, fake_rtos_bits_sent_to(all[i]), "unexpected notification");
    }
}

/**
 * @brief Helper: checks that the last TC had no visible effect at all.
 * @details It checks all of the following:
 * - no notification;
 * - no flash access;
 * - no frame queued;
 * - no ARQ activity;
 * - no beacon-period change;
 * - no RTC change;
 * - no LoRa candidate staged;
 * - result code 0.
 */
static void assert_no_effect(void)
{
    assert_only_notified(NULL, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_obdh_call_count(), "flash was accessed");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_rtos_queue_items(comms_get_tx_queue()), "a frame was queued");
    TEST_ASSERT_EQUAL_INT(0, fake_arq_begin_dl_count());
    TEST_ASSERT_EQUAL_INT(0, fake_arq_arm_ul_count());
    TEST_ASSERT_EQUAL_INT(0, fake_beacon_set_period_count());
    TEST_ASSERT_EQUAL_INT(0, fake_time_set_count());
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_IDLE, s_state);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_last_tc_rc);
}

/**
 * @brief Helper: for TCs whose only job is to notify one task. Sends the TC and checks that exactly that notification happened.
 * @param tc_id  Telecommand to send, with no parameters.
 * @param target Task that must be notified.
 * @param bits   Notification bits it must receive.
 */
static void check_notify_only(uint8_t tc_id, TaskHandle_t target, uint32_t bits)
{
    send_tc(tc_id, NULL, 0);

    assert_only_notified(target, bits);
    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    TEST_ASSERT_EQUAL_UINT(0, fake_rtos_queue_items(comms_get_tx_queue()));
    TEST_ASSERT_EQUAL_HEX8(tc_id, g_last_tc_id);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_last_tc_rc);
}

/**
 * @brief Helper: takes the next frame from the TX queue and decodes it, checking its authentication tag.
 * @param[out] needs_ack Set to the entry's needs_ack flag. Pass NULL to ignore it.
 * @return The decoded frame. The test fails if the queue is empty or the frame doesn't verify.
 */
static AirFrame_t pop_tx(uint8_t *needs_ack)
{
    TxQueueEntry_t e;
    AirFrame_t f;
    TEST_ASSERT_EQUAL_INT_MESSAGE(pdTRUE, xQueueReceive(comms_get_tx_queue(), &e, 0), "no frame queued");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, air_decode(e.frame, e.frame_len, &f), "queued frame does not verify");
    if (needs_ack) *needs_ack = e.needs_ack;
    return f;
}

/**
 * @brief Helper: checks that exactly one NACK for the last TC was queued, with this reason, and that the transceiver was told.
 * @details NACK body = [ref_type = AIR_TC, ref_seq = TC_SEQ, reason]. A NACK
 * is fire-and-forget (`needs_ack = 0`).
 */
static void assert_nack(uint8_t reason)
{
    uint8_t needs_ack = 0xFF;
    AirFrame_t f = pop_tx(&needs_ack);

    TEST_ASSERT_EQUAL_UINT8(AIR_NACK, f.type);
    TEST_ASSERT_EQUAL_UINT8(AIR_TC, f.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(TC_SEQ, f.payload[1]);
    TEST_ASSERT_EQUAL_HEX8(reason, f.payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0, needs_ack);
    TEST_ASSERT_BITS_HIGH(N_TRANSCEIVER_TX_READY_BIT, fake_rtos_bits_sent_to(task(TM_TASK_TRANSCEIVER)));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_rtos_queue_items(comms_get_tx_queue()), "more than one frame queued");
    TEST_ASSERT_EQUAL_HEX8(reason, g_last_tc_rc);
}

/**
 * @brief Helper: takes the TM reply from the TX queue and checks its 8-byte header.
 * @details TM header = [BODY_VER, TM_ID, EPOCH (4 B BE), REF_TC_ID, REF_SEQ].
 * It also checks that the TM asks for an ACK and that the transceiver was told.
 * @param tm_id    Expected TM_ID.
 * @param tc_id    TC it replies to.
 * @param body_len Expected body length after the header.
 * @return The decoded frame, so the caller can check the body (at `payload + 8`).
 */
static AirFrame_t pop_tm(uint8_t tm_id, uint8_t tc_id, uint8_t body_len)
{
    uint8_t needs_ack = 0;
    AirFrame_t f = pop_tx(&needs_ack);

    TEST_ASSERT_EQUAL_UINT8(AIR_TM, f.type);
    TEST_ASSERT_BITS_HIGH(AIR_FLAG_REQUIRES_ACK, f.flags);
    TEST_ASSERT_EQUAL_UINT8(1, needs_ack);
    TEST_ASSERT_EQUAL_UINT8(8u + body_len, f.len);
    TEST_ASSERT_EQUAL_UINT8(AIR_BODY_VER, f.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(tm_id, f.payload[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EPOCH_BE, &f.payload[2], 4);
    TEST_ASSERT_EQUAL_UINT8(tc_id, f.payload[6]);
    TEST_ASSERT_EQUAL_UINT8(TC_SEQ, f.payload[7]);
    TEST_ASSERT_BITS_HIGH(N_TRANSCEIVER_TX_READY_BIT, fake_rtos_bits_sent_to(task(TM_TASK_TRANSCEIVER)));
    return f;
}

/**
 * @brief Helper: fills a valid 21-byte LoRa configuration (§9.2 order) for TC_LORA_CONFIG.
 * @details CFG_ID 2, 869 MHz, SF9, BW 125 kHz, CR 4/5, 14 dBm, preamble 16,
 * CRC on, no IQ inversion, sync word 0x12, apply now, revert after 30 s.
 * @param body Output, 21 bytes.
 * @param freq_hz Frequency to put in the struct (pass 869000000 for a valid one).
 */
static void make_lora_body(uint8_t body[21], uint32_t freq_hz)
{
    const uint8_t b[21] = {
        0x02, 0x01,
        (uint8_t)(freq_hz >> 24), (uint8_t)(freq_hz >> 16), (uint8_t)(freq_hz >> 8), (uint8_t)freq_hz,
        9, 0, 1, 14,
        0x00, 0x10,
        1, 0, 0x12,
        0, 0, 0, 0,
        0x00, 0x1E };
    memcpy(body, b, 21);
}

/**
 * @brief Runs before every test: a freshly booted satellite.
 * @details It does the following:
 * - resets every fake: RTOS, HAL, flash image, comms environment (clock at
 *   1 700 000 000, empty TX queue, fake OBC task);
 * - resets the statics of the included modules (task table, health);
 * - restores the LoRa configuration to its boot defaults with
 *   `lora_cfg_init()`;
 * - creates the 7 subsystem tasks, all running.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    fake_obdh_reset();

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
    lora_cfg_init();
    TEST_ASSERT_EQUAL_INT(pdPASS, tm_create_tasks(TM_TASK_ALL, 0));
}

/** @brief Runs after every test: checks that the code left the RTOS balanced. */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/* =========================================================================
 * 1. Basic and state-change TCs (0x01–0x05)
 * ========================================================================= */

/**
 * @brief TC_PING (0x01) does nothing beyond the air-level ACK the transceiver already sent.
 * @details It is a link check. Expected: no side effects at all, but
 * `g_last_tc_id` = 0x01, so the next beacon reports it.
 */
void test_TC_PING_has_no_side_effect(void)
{
    send_tc(TC_PING, NULL, 0);
    assert_no_effect();
    TEST_ASSERT_EQUAL_HEX8(TC_PING, g_last_tc_id);
}

/** @brief TC_TRANSIT_TO_NM (0x02) asks the OBC to go to Nominal mode (`N_OBC_EXIT_STATE_TO_OBC_STATE_NM`). The transition itself is test_state_machine's job. */
void test_TC_TRANSIT_TO_NM_notifies_obc(void)
{
    check_notify_only(TC_TRANSIT_TO_NM, fake_comms_env_obc_task(), N_OBC_EXIT_STATE_TO_OBC_STATE_NM);
}

/** @brief TC_TRANSIT_TO_CM (0x03) asks the OBC to go to Contingency mode. */
void test_TC_TRANSIT_TO_CM_notifies_obc(void)
{
    check_notify_only(TC_TRANSIT_TO_CM, fake_comms_env_obc_task(), N_OBC_EXIT_STATE_TO_OBC_STATE_CM);
}

/** @brief TC_TRANSIT_TO_SSM (0x04) asks the OBC to go to Sunsafe mode. */
void test_TC_TRANSIT_TO_SSM_notifies_obc(void)
{
    check_notify_only(TC_TRANSIT_TO_SSM, fake_comms_env_obc_task(), N_OBC_EXIT_STATE_TO_OBC_STATE_SSM);
}

/** @brief TC_TRANSIT_TO_SM (0x05) asks the OBC to go to Survival mode. */
void test_TC_TRANSIT_TO_SM_notifies_obc(void)
{
    check_notify_only(TC_TRANSIT_TO_SM, fake_comms_env_obc_task(), N_OBC_EXIT_STATE_TO_OBC_STATE_SM);
}

/* =========================================================================
 * 2. Radio configuration (0x08, 0x09)
 * ========================================================================= */

/**
 * @brief TC_LORA_CONFIG (0x08) with a valid configuration stages it as a candidate, and doesn't apply it yet.
 * @details Expected:
 * - no NACK;
 * - lora_cfg is now in PENDING_COMMIT with the candidate's CFG_ID 2;
 * - the radio wasn't touched: the transceiver applies the candidate later,
 *   in `lora_cfg_tick()`, so SPI has a single owner.
 */
void test_TC_LORA_CONFIG_valid_stages_candidate(void)
{
    uint8_t body[21];
    make_lora_body(body, 869000000u);

    send_tc(TC_LORA_CONFIG, body, sizeof(body));

    TEST_ASSERT_EQUAL_UINT(0, fake_rtos_queue_items(comms_get_tx_queue()));
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_PENDING_COMMIT, s_state);
    TEST_ASSERT_EQUAL_UINT8(2, s_candidate.cfg_id);
    TEST_ASSERT_EQUAL_UINT32(869000000u, s_candidate.frequency_hz);
    TEST_ASSERT_EQUAL_INT(0, fake_radio_config_count());
    TEST_ASSERT_EQUAL_HEX8(0x00, g_last_tc_rc);
}

/**
 * @brief TC_LORA_CONFIG shorter than 23 bytes is rejected with NACK_BAD_PARAMS, and nothing is staged.
 */
void test_TC_LORA_CONFIG_short_nacks_bad_params(void)
{
    uint8_t body[10] = { 0 };

    send_tc(TC_LORA_CONFIG, body, sizeof(body));

    assert_nack(NACK_BAD_PARAMS);
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_IDLE, s_state);
}

/**
 * @brief TC_LORA_CONFIG with an out-of-range value (here 100 MHz) is rejected with NACK_CONFIG_REJECTED.
 * @details The §9.4 safety limits (860–928 MHz, SF 7–12, ≤ 22 dBm, ...)
 * protect the satellite from being configured onto a frequency or power it
 * must never use. Nothing is staged.
 */
void test_TC_LORA_CONFIG_out_of_range_nacks_config_rejected(void)
{
    uint8_t body[21];
    make_lora_body(body, 100000000u);

    send_tc(TC_LORA_CONFIG, body, sizeof(body));

    assert_nack(NACK_CONFIG_REJECTED);
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_IDLE, s_state);
}

/**
 * @brief A second TC_LORA_CONFIG while one is still pending is refused with NACK_BUSY, and the first candidate is kept.
 */
void test_TC_LORA_CONFIG_while_pending_nacks_busy(void)
{
    uint8_t body[21];
    make_lora_body(body, 869000000u);
    send_tc(TC_LORA_CONFIG, body, sizeof(body));

    body[0] = 0x03;   /* different CFG_ID */
    send_tc(TC_LORA_CONFIG, body, sizeof(body));

    assert_nack(NACK_BUSY);
    TEST_ASSERT_EQUAL_UINT8(2, s_candidate.cfg_id);
}

/**
 * @brief Helper for the LoRa range test: sends one configuration and checks that it is accepted or rejected.
 * @details Accepted means no NACK and a staged candidate. Rejected means
 * NACK_CONFIG_REJECTED and nothing staged. The LoRa state is reset
 * afterwards, so each case starts clean.
 */
static void expect_lora(const uint8_t body[21], bool accepted, const char *what)
{
    send_tc(TC_LORA_CONFIG, body, 21);
    if (accepted) {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_rtos_queue_items(comms_get_tx_queue()), what);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SLOT_STATE_PENDING_COMMIT, s_state, what);
    } else {
        assert_nack(NACK_CONFIG_REJECTED);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SLOT_STATE_IDLE, s_state, what);
    }
    lora_cfg_init();
}

/**
 * @brief TC_LORA_CONFIG enforces the §9.4 limits exactly at their edges.
 * @details Each value is changed on its own, starting from a valid
 * configuration:
 * - frequency: 860 MHz and 928 MHz are accepted; 859 999 999 Hz and
 *   928 000 001 Hz are rejected;
 * - TX power: 22 dBm is accepted; 23 dBm is rejected;
 * - spreading factor: 12 is accepted; 6 and 13 are rejected;
 * - coding rate: 4 is accepted; 0 and 5 are rejected;
 * - bandwidth code: 2 is accepted; 3 is rejected.
 * An off-by-one in any limit, for example `<` instead of `<=`, fails here.
 */
void test_TC_LORA_CONFIG_range_limits(void)
{
    uint8_t b[21];

    make_lora_body(b, 860000000u);  expect_lora(b, true,  "860 MHz");
    make_lora_body(b, 928000000u);  expect_lora(b, true,  "928 MHz");
    make_lora_body(b, 859999999u);  expect_lora(b, false, "859.999999 MHz");
    make_lora_body(b, 928000001u);  expect_lora(b, false, "928.000001 MHz");

    make_lora_body(b, 869000000u); b[9] = 22;  expect_lora(b, true,  "22 dBm");
    make_lora_body(b, 869000000u); b[9] = 23;  expect_lora(b, false, "23 dBm");
    make_lora_body(b, 869000000u); b[6] = 12;  expect_lora(b, true,  "SF12");
    make_lora_body(b, 869000000u); b[6] = 6;   expect_lora(b, false, "SF6");
    make_lora_body(b, 869000000u); b[6] = 13;  expect_lora(b, false, "SF13");
    make_lora_body(b, 869000000u); b[8] = 4;   expect_lora(b, true,  "CR 4");
    make_lora_body(b, 869000000u); b[8] = 0;   expect_lora(b, false, "CR 0");
    make_lora_body(b, 869000000u); b[8] = 5;   expect_lora(b, false, "CR 5");
    make_lora_body(b, 869000000u); b[7] = 2;   expect_lora(b, true,  "BW code 2");
    make_lora_body(b, 869000000u); b[7] = 3;   expect_lora(b, false, "BW code 3");
}

/** @brief TC_LORA_CONFIG one byte short (22 bytes, 20 of configuration) is rejected with NACK_BAD_PARAMS, the length limit is exact. */
void test_TC_LORA_CONFIG_one_byte_short_nacks_bad_params(void)
{
    uint8_t body[21];
    make_lora_body(body, 869000000u);

    send_tc(TC_LORA_CONFIG, body, 20);

    assert_nack(NACK_BAD_PARAMS);
}

/**
 * @brief TC_COMMS_PARAMS (0x09) notifies the COMMS task (`N_COMMS_NEW_PARAMS`).
 * @note The parameters aren't stored yet ("TODO Phase 4: persist params to
 * flash" in tc_handler.c), and the COMMS task doesn't act on the bit yet.
 */
void test_TC_COMMS_PARAMS_notifies_comms(void)
{
    check_notify_only(TC_COMMS_PARAMS, task(TM_TASK_COMMS), N_COMMS_NEW_PARAMS);
}

/* =========================================================================
 * 3. Uploads (0x0A–0x0C)
 * ========================================================================= */

/**
 * @brief TC_UPLOAD_UNIX_TIME (0x0A) sets the RTC to the 4-byte big-endian time and tells the OBC (`N_OBC_UPDATE_TIME`).
 * @details Sends 0x66000000 (1 711 276 032 s, March 2024).
 */
void test_TC_UPLOAD_UNIX_TIME_sets_rtc_and_notifies_obc(void)
{
    const uint8_t t[4] = { 0x66, 0x00, 0x00, 0x00 };

    send_tc(TC_UPLOAD_UNIX_TIME, t, sizeof(t));

    TEST_ASSERT_EQUAL_INT(1, fake_time_set_count());
    TEST_ASSERT_EQUAL_UINT32(0x66000000u, fake_time_last_set());
    assert_only_notified(fake_comms_env_obc_task(), N_OBC_UPDATE_TIME);
}

/** @brief TC_UPLOAD_UNIX_TIME with fewer than 4 time bytes is ignored: the RTC isn't touched and nobody is notified. */
void test_TC_UPLOAD_UNIX_TIME_short_is_ignored(void)
{
    const uint8_t t[3] = { 0x66, 0x00, 0x00 };

    send_tc(TC_UPLOAD_UNIX_TIME, t, sizeof(t));

    assert_no_effect();
}

/**
 * @brief TC_UPLOAD_EPS_TH (0x0B) stores the 3 thresholds at `EPS_THRESHOLDS_ADDR` and tells EPS to reload them (`N_EPS_NEW_THRESHOLDS`).
 * @details The write goes through OBDH (a WRITE of 3 bytes); EPS then reads
 * the same 3 bytes (nominal, contingency, sunsafe) in eps.c.
 */
void test_TC_UPLOAD_EPS_TH_writes_flash_and_notifies_eps(void)
{
    const uint8_t th[3] = { 10, 20, 30 };

    send_tc(TC_UPLOAD_EPS_TH, th, sizeof(th));

    TEST_ASSERT_EQUAL_INT(1, fake_obdh_call_count());
    fake_obdh_call_t c = fake_obdh_call_at(0);
    TEST_ASSERT_EQUAL_INT(FAKE_OBDH_WRITE, c.op);
    TEST_ASSERT_EQUAL_HEX32(EPS_THRESHOLDS_ADDR, c.addr);
    TEST_ASSERT_EQUAL_UINT(3, c.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(th, fake_obdh_flash(EPS_THRESHOLDS_ADDR), 3);
    assert_only_notified(task(TM_TASK_EPS), N_EPS_NEW_THRESHOLDS);
}

/**
 * @brief TC_UPLOAD_EPS_TH without the 3 threshold bytes must be rejected, not stored.
 * @details
 * - Expected: no flash write, and EPS isn't told to reload.
 * - Actual: tc_handler doesn't check `frame->len`. It writes 3 bytes from
 *   past the end of the received parameters, here zeros, as the new power
 *   thresholds, and tells EPS to use them.
 * @bug KI-8 (test/README.md).
 */
void test_TC_UPLOAD_EPS_TH_short_is_rejected(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-8: TC_UPLOAD_EPS_TH does not check frame->len; writes 3 stale bytes as thresholds");
    send_tc(TC_UPLOAD_EPS_TH, NULL, 0);

    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
    assert_only_notified(NULL, 0);
}

/**
 * @brief TC_UPLOAD_PL_CONFIG (0x0C) stores the 8-byte payload configuration at `RFI_CONFIG_ADDR`.
 * @details Nobody is notified: the payload reads it when it next runs.
 */
void test_TC_UPLOAD_PL_CONFIG_writes_flash(void)
{
    const uint8_t cfg[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    send_tc(TC_UPLOAD_PL_CONFIG, cfg, sizeof(cfg));

    TEST_ASSERT_EQUAL_INT(1, fake_obdh_call_count());
    fake_obdh_call_t c = fake_obdh_call_at(0);
    TEST_ASSERT_EQUAL_INT(FAKE_OBDH_WRITE, c.op);
    TEST_ASSERT_EQUAL_HEX32(RFI_CONFIG_ADDR, c.addr);
    TEST_ASSERT_EQUAL_UINT(8, c.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cfg, fake_obdh_flash(RFI_CONFIG_ADDR), 8);
    assert_only_notified(NULL, 0);
}

/**
 * @brief TC_UPLOAD_PL_CONFIG without the 8 configuration bytes must be rejected, not stored.
 * @details Same missing length check as TC_UPLOAD_EPS_TH: 8 bytes from past
 * the end of the parameters are written as the payload configuration.
 * @bug KI-8 (test/README.md).
 */
void test_TC_UPLOAD_PL_CONFIG_short_is_rejected(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-8: TC_UPLOAD_PL_CONFIG does not check frame->len; writes 8 stale bytes as config");
    send_tc(TC_UPLOAD_PL_CONFIG, NULL, 0);

    TEST_ASSERT_EQUAL_INT(0, fake_obdh_call_count());
}

/* =========================================================================
 * 4. Requests: beacon and telemetry (0x0D–0x12)
 * ========================================================================= */

/**
 * @brief TC_REQUEST_BEACON_NOW (0x0D) wakes the BEACON task with `N_COMMS_TRANSMIT_BEACON`, so it sends a beacon at once.
 * @details beacon.c handles that bit in `beacon_task()`, the same bit its
 * periodic timer sends. The "deprecated" note on the bit in notifications.h
 * doesn't apply here: the bit is still in use.
 */
void test_TC_REQUEST_BEACON_NOW_wakes_beacon_task(void)
{
    check_notify_only(TC_REQUEST_BEACON_NOW, task(TM_TASK_BEACON), N_COMMS_TRANSMIT_BEACON);
}

/**
 * @brief TC_REQUEST_DOWNLINK_CONFIG (0x0E) replies with a TM_DOWNLINK_CONFIG holding the active LoRa configuration.
 * @details After boot, the active configuration is the default from
 * `lora_cfg_init()`. The 16-byte body is checked byte by byte:
 * - BODY_VER 1, CFG_ID 1;
 * - 868 MHz = 0x33BCA100, big-endian;
 * - SF10, BW 125 kHz (code 0), CR 4/5 (code 1), 18 dBm;
 * - preamble 64, CRC on, no IQ inversion;
 * - sync word 0x12, then one reserved byte.
 */
void test_TC_REQUEST_DOWNLINK_CONFIG_replies_active_config(void)
{
    static const uint8_t expected[16] = {
        0x01, 0x01, 0x33, 0xBC, 0xA1, 0x00, 10, 0, 1, 18, 0x00, 0x40, 1, 0, 0x12, 0 };

    send_tc(TC_REQUEST_DOWNLINK_CONFIG, NULL, 0);

    AirFrame_t f = pop_tm(TM_DOWNLINK_CONFIG, TC_REQUEST_DOWNLINK_CONFIG, 16);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, &f.payload[8], 16);
}

/**
 * @brief TC_REQUEST_HK_LIVE (0x0F) replies with a 24-byte TM_HK_LIVE built from live data.
 * @details
 * - Arrange: the stored OBC state is CM (2), the MCU is at 23 °C, and Vdda
 *   is 3300 mV.
 * - Expected body:
 *   - [0] OBC state = 2, read from `CURRENT_STATE_ADDR` through OBDH;
 *   - [1] health flags = 0x3F (a placeholder for now);
 *   - [2] MCU temperature = 23;
 *   - [6] Vdda / 100 = 33;
 *   - the other bytes are 0 (sensors not implemented yet).
 */
void test_TC_REQUEST_HK_LIVE_replies_housekeeping(void)
{
    fake_obdh_flash(CURRENT_STATE_ADDR)[0] = OBC_STATE_CM;
    fake_sensors_set(23, 3300);

    send_tc(TC_REQUEST_HK_LIVE, NULL, 0);

    AirFrame_t f = pop_tm(TM_HK_LIVE, TC_REQUEST_HK_LIVE, 24);
    const uint8_t *body = &f.payload[8];
    TEST_ASSERT_EQUAL_UINT8(OBC_STATE_CM, body[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, body[1]);
    TEST_ASSERT_EQUAL_INT8(23, (int8_t)body[2]);
    TEST_ASSERT_EQUAL_UINT8(33, body[6]);
    for (int i = 0; i < 24; i++) {
        if (i == 0 || i == 1 || i == 2 || i == 6) continue;
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, body[i], "unimplemented HK field is not 0");
    }
    TEST_ASSERT_EQUAL_INT(FAKE_OBDH_READ, fake_obdh_call_at(0).op);
    TEST_ASSERT_EQUAL_HEX32(CURRENT_STATE_ADDR, fake_obdh_call_at(0).addr);
}

/**
 * @brief TC_REQUEST_HK_HISTORY (0x10) starts an ARQ downlink of historic telemetry, passing the 4-byte SINCE_UNIX parameter.
 * @details Sent twice. Each time an ARQ downlink of type
 * TRANSFER_HK_HISTORY starts with the same 4 parameter bytes, and the second
 * session id is the first plus 1.
 */
void test_TC_REQUEST_HK_HISTORY_starts_arq_downlink(void)
{
    const uint8_t since[4] = { 0x65, 0x00, 0x00, 0x00 };

    send_tc(TC_REQUEST_HK_HISTORY, since, sizeof(since));
    fake_arq_dl_call_t first = fake_arq_last_dl();
    send_tc(TC_REQUEST_HK_HISTORY, since, sizeof(since));
    fake_arq_dl_call_t second = fake_arq_last_dl();

    TEST_ASSERT_EQUAL_INT(2, fake_arq_begin_dl_count());
    TEST_ASSERT_EQUAL_INT(TRANSFER_HK_HISTORY, second.type);
    TEST_ASSERT_EQUAL_UINT8(4, second.params_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(since, second.params, 4);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(first.session_id + 1u), second.session_id);
}

/** @brief TC_REQUEST_HK_HISTORY without SINCE_UNIX still starts the downlink, with no parameters (NULL, length 0): "send everything". */
void test_TC_REQUEST_HK_HISTORY_without_since_sends_no_params(void)
{
    send_tc(TC_REQUEST_HK_HISTORY, NULL, 0);

    fake_arq_dl_call_t c = fake_arq_last_dl();
    TEST_ASSERT_EQUAL_INT(1, fake_arq_begin_dl_count());
    TEST_ASSERT_TRUE(c.params_was_null);
    TEST_ASSERT_EQUAL_UINT8(0, c.params_len);
}

/** @brief TC_REQUEST_HK_HISTORY with a partial SINCE_UNIX (3 of 4 bytes) starts the downlink with no parameters instead of passing a truncated time. */
void test_TC_REQUEST_HK_HISTORY_partial_since_sends_no_params(void)
{
    const uint8_t since[3] = { 0x65, 0x00, 0x00 };

    send_tc(TC_REQUEST_HK_HISTORY, since, sizeof(since));

    TEST_ASSERT_TRUE(fake_arq_last_dl().params_was_null);
    TEST_ASSERT_EQUAL_UINT8(0, fake_arq_last_dl().params_len);
}

/**
 * @brief A download request while another ARQ transfer is running must be refused with NACK_BUSY.
 * @details
 * - Arrange: the fake ARQ reports "session already active", as the real
 *   `arq_begin_dl()` does (returns -1, arq.c:390).
 * - Expected: a NACK_BUSY frame, and `g_last_tc_rc` = NACK_BUSY, so the
 *   ground knows its request was not started.
 * - Actual: `tc_process()` ignores the return value. The TC was already
 *   ACKed at air level, nothing is sent, and the beacon reports success
 *   (rc 0), so the ground waits for a download that never comes. The same
 *   applies to TC_REQUEST_PAYLOAD_DATA and TC_REQUEST_OBC_LOG, and
 *   `arq_arm_ul()` (TLE / ADCS uploads) refuses silently in the same
 *   situation.
 * @bug KI-16 (test/README.md). Verified by reading the code.
 */
void test_TC_REQUEST_HK_HISTORY_while_arq_busy_nacks_busy(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-16: arq_begin_dl() == -1 (session active) is ignored; no NACK, rc stays 0");
    fake_arq_set_begin_dl_result(-1);

    send_tc(TC_REQUEST_HK_HISTORY, NULL, 0);

    assert_nack(NACK_BUSY);
}

/**
 * @brief TC_REQUEST_PAYLOAD_DATA (0x11) starts an ARQ downlink of payload data with its 5 parameter bytes.
 * @details The parameters are MEASURE_NUMBER (1 B), FIRST_BLOCK (2 B) and
 * BLOCK_COUNT (2 B).
 */
void test_TC_REQUEST_PAYLOAD_DATA_starts_arq_downlink(void)
{
    const uint8_t p[5] = { 3, 0x00, 0x01, 0x00, 0x04 };

    send_tc(TC_REQUEST_PAYLOAD_DATA, p, sizeof(p));

    fake_arq_dl_call_t c = fake_arq_last_dl();
    TEST_ASSERT_EQUAL_INT(1, fake_arq_begin_dl_count());
    TEST_ASSERT_EQUAL_INT(TRANSFER_PAYLOAD_DATA, c.type);
    TEST_ASSERT_EQUAL_UINT8(5, c.params_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p, c.params, 5);
}

/** @brief TC_REQUEST_PAYLOAD_DATA with fewer than 5 parameter bytes starts the downlink with no parameters instead of passing a partial set. */
void test_TC_REQUEST_PAYLOAD_DATA_short_sends_no_params(void)
{
    const uint8_t p[4] = { 3, 0x00, 0x01, 0x00 };

    send_tc(TC_REQUEST_PAYLOAD_DATA, p, sizeof(p));

    TEST_ASSERT_TRUE(fake_arq_last_dl().params_was_null);
    TEST_ASSERT_EQUAL_UINT8(0, fake_arq_last_dl().params_len);
}

/** @brief TC_REQUEST_OBC_LOG (0x12) starts an ARQ downlink of the OBC log, with no parameters. */
void test_TC_REQUEST_OBC_LOG_starts_arq_downlink(void)
{
    send_tc(TC_REQUEST_OBC_LOG, NULL, 0);

    fake_arq_dl_call_t c = fake_arq_last_dl();
    TEST_ASSERT_EQUAL_INT(1, fake_arq_begin_dl_count());
    TEST_ASSERT_EQUAL_INT(TRANSFER_OBC_LOG, c.type);
    TEST_ASSERT_TRUE(c.params_was_null);
}

/* =========================================================================
 * 5. ARQ uploads (0x18, 0x19)
 * ========================================================================= */

/**
 * @brief TC_UPLOAD_TLE_BEGIN (0x18) arms the ARQ receiver for a TLE upload.
 * @details The data itself arrives later, in DATA_BEGIN/DATA frames handled
 * by `arq_rx()`. This TC only says what to expect.
 */
void test_TC_UPLOAD_TLE_BEGIN_arms_arq_uplink(void)
{
    send_tc(TC_UPLOAD_TLE_BEGIN, NULL, 0);

    TEST_ASSERT_EQUAL_INT(1, fake_arq_arm_ul_count());
    TEST_ASSERT_EQUAL_INT(TRANSFER_TLE_UPLOAD, fake_arq_last_ul_type());
}

/** @brief TC_UPLOAD_ADCS_CAL_BEGIN (0x19) arms the ARQ receiver for an ADCS calibration upload. */
void test_TC_UPLOAD_ADCS_CAL_BEGIN_arms_arq_uplink(void)
{
    send_tc(TC_UPLOAD_ADCS_CAL_BEGIN, NULL, 0);

    TEST_ASSERT_EQUAL_INT(1, fake_arq_arm_ul_count());
    TEST_ASSERT_EQUAL_INT(TRANSFER_ADCS_CALIBRATION, fake_arq_last_ul_type());
}

/* =========================================================================
 * 6. EPS heater and power rails (0x20–0x37)
 * ========================================================================= */

/** @brief TC_EPS_HEATER_ENABLE (0x20) tells EPS to turn on automatic battery heating (`N_EPS_ENABLE_AUTO_HEAT`). */
void test_TC_EPS_HEATER_ENABLE_notifies_eps(void)
{
    check_notify_only(TC_EPS_HEATER_ENABLE, task(TM_TASK_EPS), N_EPS_ENABLE_AUTO_HEAT);
}

/** @brief TC_EPS_HEATER_DISABLE (0x21) tells EPS to turn off automatic battery heating (`N_EPS_DISABLE_AUTO_HEAT`). */
void test_TC_EPS_HEATER_DISABLE_notifies_eps(void)
{
    check_notify_only(TC_EPS_HEATER_DISABLE, task(TM_TASK_EPS), N_EPS_DISABLE_AUTO_HEAT);
}

/**
 * @brief The 8 point-of-load rail TCs (0x30–0x37) are accepted but do nothing yet.
 * @details Each one is recognised, so it gets no NACK_UNKNOWN_TC_ID and the
 * result code is 0, but rail control is still a TODO in tc_handler.c. When
 * it is implemented, replace this test with one test per rail.
 */
void test_TC_POL_rails_are_accepted_but_not_implemented(void)
{
    const uint8_t pol[8] = { TC_POL_PAYLOAD_SHUT, TC_POL_PAYLOAD_ENABLE, TC_POL_ADCS_SHUT, TC_POL_ADCS_ENABLE,
                             TC_POL_BURNCOMMS_SHUT, TC_POL_BURNCOMMS_ENABLE, TC_POL_HEATER_SHUT, TC_POL_HEATER_ENABLE };
    for (int i = 0; i < 8; i++) {
        send_tc(pol[i], NULL, 0);
        assert_no_effect();
        TEST_ASSERT_EQUAL_HEX8(pol[i], g_last_tc_id);
    }
}

/* =========================================================================
 * 7. Memory management (0x40–0x42)
 * ========================================================================= */

/**
 * @brief TC_CLEAR_PL_DATA (0x40) notifies OBDH (`N_OBDH_CLEAR_PAYLOAD`).
 * @warning The TC side works, but OBDH currently drops this bit (KI-12 in
 * test/README.md), so no payload data is actually erased.
 */
void test_TC_CLEAR_PL_DATA_notifies_obdh(void)
{
    check_notify_only(TC_CLEAR_PL_DATA, task(TM_TASK_OBDH), N_OBDH_CLEAR_PAYLOAD);
}

/** @brief TC_CLEAR_FLASH (0x41) notifies OBDH (`N_OBDH_CLEAR_FLASH`). @warning Dropped by OBDH today (KI-12). */
void test_TC_CLEAR_FLASH_notifies_obdh(void)
{
    check_notify_only(TC_CLEAR_FLASH, task(TM_TASK_OBDH), N_OBDH_CLEAR_FLASH);
}

/** @brief TC_CLEAR_HT (0x42) notifies OBDH (`N_OBDH_CLEAR_HT`). @warning Dropped by OBDH today (KI-12). */
void test_TC_CLEAR_HT_notifies_obdh(void)
{
    check_notify_only(TC_CLEAR_HT, task(TM_TASK_OBDH), N_OBDH_CLEAR_HT);
}

/* =========================================================================
 * 8. Communications control (0x50–0x52)
 * ========================================================================= */

/** @brief TC_COMMS_STOP_TX (0x50) tells COMMS to stop transmitting (`N_COMMS_STOP_RF`). The TX gating in comms.c is still a TODO. */
void test_TC_COMMS_STOP_TX_notifies_comms(void)
{
    check_notify_only(TC_COMMS_STOP_TX, task(TM_TASK_COMMS), N_COMMS_STOP_RF);
}

/** @brief TC_COMMS_RESUME_TX (0x51) tells COMMS to transmit again (`N_COMMS_RESUME_RF`). */
void test_TC_COMMS_RESUME_TX_notifies_comms(void)
{
    check_notify_only(TC_COMMS_RESUME_TX, task(TM_TASK_COMMS), N_COMMS_RESUME_RF);
}

/**
 * @brief TC_SET_BEACON_PERIOD (0x52) sets the beacon period from a 2-byte big-endian number of seconds.
 * @details 0x003C = 60 s, so `beacon_set_period(60000)` (ms) is called.
 */
void test_TC_SET_BEACON_PERIOD_sets_period_in_ms(void)
{
    const uint8_t s[2] = { 0x00, 0x3C };

    send_tc(TC_SET_BEACON_PERIOD, s, sizeof(s));

    TEST_ASSERT_EQUAL_INT(1, fake_beacon_set_period_count());
    TEST_ASSERT_EQUAL_UINT32(60000u, fake_beacon_last_period_ms());
    TEST_ASSERT_EQUAL_UINT32(60000u, g_beacon_period_ms);
}

/**
 * @brief TC_SET_BEACON_PERIOD with the largest value (0xFFFF s, about 18.2 h) gives 65 535 000 ms without overflow.
 * @details The multiplication by 1000 is done in 32 bits (`(uint32_t)period_s * 1000u`). If it were done in 16 bits, it would wrap to a much shorter period.
 */
void test_TC_SET_BEACON_PERIOD_max_does_not_overflow(void)
{
    const uint8_t s[2] = { 0xFF, 0xFF };

    send_tc(TC_SET_BEACON_PERIOD, s, sizeof(s));

    TEST_ASSERT_EQUAL_UINT32(65535000u, fake_beacon_last_period_ms());
}

/** @brief TC_SET_BEACON_PERIOD with fewer than 2 bytes is ignored: the period is unchanged. */
void test_TC_SET_BEACON_PERIOD_short_is_ignored(void)
{
    const uint8_t s[1] = { 0x3C };

    send_tc(TC_SET_BEACON_PERIOD, s, sizeof(s));

    assert_no_effect();
}

/**
 * @brief TC_SET_BEACON_PERIOD with 0 seconds must be rejected, because a 0 period freezes the satellite.
 * @details
 * - Expected: `beacon_set_period()` isn't called. Ideally a NACK
 *   PARAM_OUT_OF_RANGE is sent too.
 * - Actual: `beacon_set_period(0)` is called, and on the target it runs
 *   `xTimerChangePeriod(beacon_timer, 0, ...)`. FreeRTOS checks
 *   `configASSERT(xTimerPeriodInTicks > 0)` in the timer task
 *   (external/FreeRTOS/Source/timers.c). This project's `configASSERT`
 *   disables interrupts and loops forever, so the whole OBC hangs until the
 *   IWDG resets it. A single authenticated TC from the ground triggers it.
 * @bug KI-15 (test/README.md). Verified by reading the code; the timer
 * itself can't run on the host.
 */
void test_TC_SET_BEACON_PERIOD_zero_is_rejected(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-15: period 0 reaches xTimerChangePeriod(0) -> configASSERT hangs the OBC");
    const uint8_t s[2] = { 0x00, 0x00 };

    send_tc(TC_SET_BEACON_PERIOD, s, sizeof(s));

    TEST_ASSERT_EQUAL_INT(0, fake_beacon_set_period_count());
}

/* =========================================================================
 * 9. Payload and OBC control (0x60–0x73)
 * ========================================================================= */

/**
 * @brief TC_PAYLOAD_SCHEDULE (0x60) activates the payload (`N_PAYLOAD_ACTIVATE`).
 * @note The schedule parameters aren't stored yet ("TODO: OBDH write for
 * schedule parameters").
 */
void test_TC_PAYLOAD_SCHEDULE_activates_payload(void)
{
    check_notify_only(TC_PAYLOAD_SCHEDULE, task(TM_TASK_PAYLOAD), N_PAYLOAD_ACTIVATE);
}

/** @brief TC_PAYLOAD_DEACTIVATE (0x61) deactivates the payload (`N_PAYLOAD_DEACTIVATE`). */
void test_TC_PAYLOAD_DEACTIVATE_deactivates_payload(void)
{
    check_notify_only(TC_PAYLOAD_DEACTIVATE, task(TM_TASK_PAYLOAD), N_PAYLOAD_DEACTIVATE);
}

/** @brief TC_OBC_HARD_REBOOT (0x70) asks the OBC for a hard reboot (`N_OBC_HARD_REBOOT`). The handling in obc.c is still a TODO. */
void test_TC_OBC_HARD_REBOOT_notifies_obc(void)
{
    check_notify_only(TC_OBC_HARD_REBOOT, fake_comms_env_obc_task(), N_OBC_HARD_REBOOT);
}

/** @brief TC_OBC_SOFT_REBOOT (0x71) asks the OBC for a soft reboot (`N_OBC_SOFT_REBOOT`). */
void test_TC_OBC_SOFT_REBOOT_notifies_obc(void)
{
    check_notify_only(TC_OBC_SOFT_REBOOT, fake_comms_env_obc_task(), N_OBC_SOFT_REBOOT);
}

/** @brief TC_OBC_PERIPH_REBOOT (0x72) asks the OBC to reboot the peripherals (`N_OBC_PERIPHERALS_REBOOT`). */
void test_TC_OBC_PERIPH_REBOOT_notifies_obc(void)
{
    check_notify_only(TC_OBC_PERIPH_REBOOT, fake_comms_env_obc_task(), N_OBC_PERIPHERALS_REBOOT);
}

/**
 * @brief TC_OBC_DEBUG_MODE (0x73) is accepted but does nothing yet.
 * @details This is the planned hook for an on-board test console (Layer 4 in
 * docs/testing/test-design.md).
 */
void test_TC_OBC_DEBUG_MODE_is_accepted_but_not_implemented(void)
{
    send_tc(TC_OBC_DEBUG_MODE, NULL, 0);
    assert_no_effect();
}

/* =========================================================================
 * 10. Unknown, malformed and edge cases
 * ========================================================================= */

/**
 * @brief An unknown TC_ID (0x99) is answered with NACK_UNKNOWN_TC_ID, and the result code is recorded for the beacon.
 */
void test_unknown_tc_id_nacks_unknown(void)
{
    send_tc(0x99, NULL, 0);

    assert_nack(NACK_UNKNOWN_TC_ID);
    TEST_ASSERT_EQUAL_HEX8(0x99, g_last_tc_id);
}

/**
 * @brief Every one of the 256 possible TC_ID values is either in the catalogue (types.h) or NACKed as unknown, never both and never neither.
 * @details `known[]` below is a hand-written copy of the catalogue in
 * types.h. The test keeps `known[]` and `tc_process()` in sync:
 * - an id in `known[]` that has no `case` in `tc_process()` fails here,
 *   because it is NACKed as unknown;
 * - a `case` for an id that isn't in `known[]` fails here too.
 *
 * It **can't** detect an id added to types.h and forgotten in both places.
 * That is why the README procedure says to update `known[]` whenever you
 * touch the catalogue. The TX queue is emptied after each id.
 * @note When you add a TC to types.h, add it to `known[]` below and give it
 * its own `test_TC_<NAME>_...` test.
 */
void test_every_tc_id_is_either_handled_or_nacked(void)
{
    static const uint8_t known[] = {
        TC_PING, TC_TRANSIT_TO_NM, TC_TRANSIT_TO_CM, TC_TRANSIT_TO_SSM, TC_TRANSIT_TO_SM,
        TC_LORA_CONFIG, TC_COMMS_PARAMS, TC_UPLOAD_UNIX_TIME, TC_UPLOAD_EPS_TH, TC_UPLOAD_PL_CONFIG,
        TC_REQUEST_BEACON_NOW, TC_REQUEST_DOWNLINK_CONFIG, TC_REQUEST_HK_LIVE, TC_REQUEST_HK_HISTORY,
        TC_REQUEST_PAYLOAD_DATA, TC_REQUEST_OBC_LOG, TC_UPLOAD_TLE_BEGIN, TC_UPLOAD_ADCS_CAL_BEGIN,
        TC_EPS_HEATER_ENABLE, TC_EPS_HEATER_DISABLE,
        TC_POL_PAYLOAD_SHUT, TC_POL_PAYLOAD_ENABLE, TC_POL_ADCS_SHUT, TC_POL_ADCS_ENABLE,
        TC_POL_BURNCOMMS_SHUT, TC_POL_BURNCOMMS_ENABLE, TC_POL_HEATER_SHUT, TC_POL_HEATER_ENABLE,
        TC_CLEAR_PL_DATA, TC_CLEAR_FLASH, TC_CLEAR_HT, TC_COMMS_STOP_TX, TC_COMMS_RESUME_TX,
        TC_SET_BEACON_PERIOD, TC_PAYLOAD_SCHEDULE, TC_PAYLOAD_DEACTIVATE,
        TC_OBC_HARD_REBOOT, TC_OBC_SOFT_REBOOT, TC_OBC_PERIPH_REBOOT, TC_OBC_DEBUG_MODE };
    TxQueueEntry_t drain;

    for (unsigned id = 0; id < 256u; id++) {
        bool is_known = false;
        for (size_t k = 0; k < sizeof(known); k++) {
            if (known[k] == id) { is_known = true; break; }
        }

        send_tc((uint8_t)id, NULL, 0);

        char msg[48];
        snprintf(msg, sizeof(msg), "TC_ID 0x%02X", id);
        if (is_known) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(NACK_UNKNOWN_TC_ID, g_last_tc_rc, msg);
        } else {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(NACK_UNKNOWN_TC_ID, g_last_tc_rc, msg);
        }
        while (xQueueReceive(comms_get_tx_queue(), &drain, 0) == pdTRUE) { }
        lora_cfg_init();   /* undo any LoRa candidate so later ids start clean */
    }
}

/**
 * @brief A frame shorter than 2 bytes (no TC_ID) is ignored completely, and it isn't even recorded as the last TC.
 */
void test_frame_without_tc_id_is_ignored(void)
{
    AirFrame_t f;
    memset(&f, 0, sizeof(f));
    f.type = AIR_TC;
    f.seq = TC_SEQ;
    f.len = 1;
    f.payload[0] = AIR_BODY_VER;

    tc_process(&f);

    assert_no_effect();
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_last_tc_id);
}

/**
 * @brief The last TC and its result code (sent in every beacon) are updated by each TC: an error is cleared by the next successful TC.
 * @details An unknown TC sets the result code to NACK_UNKNOWN_TC_ID. A
 * following TC_PING sets id 0x01 and result 0x00. The ground can then see in
 * the beacon whether its last command was accepted.
 */
void test_last_tc_and_result_are_tracked_for_beacon(void)
{
    send_tc(0x99, NULL, 0);
    TEST_ASSERT_EQUAL_HEX8(NACK_UNKNOWN_TC_ID, g_last_tc_rc);

    send_tc(TC_PING, NULL, 0);
    TEST_ASSERT_EQUAL_HEX8(TC_PING, g_last_tc_id);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_last_tc_rc);
}

/**
 * @brief A TC that targets a task that doesn't exist yet (NULL handle) is dropped safely, without a crash.
 * @details This simulates a TC arriving before `tm_create_tasks()`, or while
 * the target task is being recreated after a health fault. `notify()` in
 * tc_handler.c checks for NULL.
 */
void test_tc_for_missing_task_is_dropped_safely(void)
{
    for (size_t i = 0; i < TASK_TABLE_SIZE; i++) task_table[i].handle = NULL;

    send_tc(TC_COMMS_STOP_TX, NULL, 0);

    TEST_ASSERT_EQUAL_INT(0, fake_rtos_notify_log_count());
}

/**
 * @brief When the TX queue is full, a NACK is dropped without any error: the transceiver isn't notified, and the queue keeps its 2 earlier frames.
 * @details This test records current behaviour; it is not a bug report. The
 * TX queue holds only `COMMS_QUEUE_LEN` (2) frames, and `enqueue_nack()` sends
 * with timeout 0. If the queue is full, the NACK is lost, so the ground sees
 * the eager ACK and never learns the TC was rejected. Only the result code
 * in the next beacon reports the error.
 */
void test_nack_is_dropped_when_tx_queue_full(void)
{
    TxQueueEntry_t filler;
    memset(&filler, 0, sizeof(filler));
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(comms_get_tx_queue(), &filler, 0));
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(comms_get_tx_queue(), &filler, 0));

    send_tc(0x99, NULL, 0);

    TEST_ASSERT_EQUAL_UINT(COMMS_QUEUE_LEN, fake_rtos_queue_items(comms_get_tx_queue()));
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rtos_bits_sent_to(task(TM_TASK_TRANSCEIVER)));
    TEST_ASSERT_EQUAL_HEX8(NACK_UNKNOWN_TC_ID, g_last_tc_rc);
}

/**
 * @brief A TM reply is also dropped without error when the TX queue is full: the ground never gets the telemetry it asked for.
 * @details This test records current behaviour, like
 * test_nack_is_dropped_when_tx_queue_full: `enqueue_tm()` sends with
 * timeout 0. TC_REQUEST_HK_LIVE with a full queue leaves the queue at 2
 * frames and doesn't notify the transceiver. The result code stays 0,
 * because the TC itself was valid.
 */
void test_tm_is_dropped_when_tx_queue_full(void)
{
    TxQueueEntry_t filler;
    memset(&filler, 0, sizeof(filler));
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(comms_get_tx_queue(), &filler, 0));
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(comms_get_tx_queue(), &filler, 0));

    send_tc(TC_REQUEST_HK_LIVE, NULL, 0);

    TEST_ASSERT_EQUAL_UINT(COMMS_QUEUE_LEN, fake_rtos_queue_items(comms_get_tx_queue()));
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rtos_bits_sent_to(task(TM_TASK_TRANSCEIVER)));
    TEST_ASSERT_EQUAL_HEX8(0x00, g_last_tc_rc);
}

/**
 * @brief Test runner. Lists the whole catalogue, in TC_ID order, followed by the edge cases.
 * @details A test function that isn't listed here never runs. See
 * test/support/pocat_test_main.h for the `-l` / `-f` / `-n` / `-x` options.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    /* 1. Basic and state change */
    POCAT_RUN_TEST(test_TC_PING_has_no_side_effect);
    POCAT_RUN_TEST(test_TC_TRANSIT_TO_NM_notifies_obc);
    POCAT_RUN_TEST(test_TC_TRANSIT_TO_CM_notifies_obc);
    POCAT_RUN_TEST(test_TC_TRANSIT_TO_SSM_notifies_obc);
    POCAT_RUN_TEST(test_TC_TRANSIT_TO_SM_notifies_obc);
    /* 2. Radio configuration */
    POCAT_RUN_TEST(test_TC_LORA_CONFIG_valid_stages_candidate);
    POCAT_RUN_TEST(test_TC_LORA_CONFIG_short_nacks_bad_params);
    POCAT_RUN_TEST(test_TC_LORA_CONFIG_out_of_range_nacks_config_rejected);
    POCAT_RUN_TEST(test_TC_LORA_CONFIG_while_pending_nacks_busy);
    POCAT_RUN_TEST(test_TC_LORA_CONFIG_range_limits);
    POCAT_RUN_TEST(test_TC_LORA_CONFIG_one_byte_short_nacks_bad_params);
    POCAT_RUN_TEST(test_TC_COMMS_PARAMS_notifies_comms);
    /* 3. Uploads */
    POCAT_RUN_TEST(test_TC_UPLOAD_UNIX_TIME_sets_rtc_and_notifies_obc);
    POCAT_RUN_TEST(test_TC_UPLOAD_UNIX_TIME_short_is_ignored);
    POCAT_RUN_TEST(test_TC_UPLOAD_EPS_TH_writes_flash_and_notifies_eps);
    POCAT_RUN_TEST(test_TC_UPLOAD_EPS_TH_short_is_rejected);
    POCAT_RUN_TEST(test_TC_UPLOAD_PL_CONFIG_writes_flash);
    POCAT_RUN_TEST(test_TC_UPLOAD_PL_CONFIG_short_is_rejected);
    /* 4. Requests */
    POCAT_RUN_TEST(test_TC_REQUEST_BEACON_NOW_wakes_beacon_task);
    POCAT_RUN_TEST(test_TC_REQUEST_DOWNLINK_CONFIG_replies_active_config);
    POCAT_RUN_TEST(test_TC_REQUEST_HK_LIVE_replies_housekeeping);
    POCAT_RUN_TEST(test_TC_REQUEST_HK_HISTORY_starts_arq_downlink);
    POCAT_RUN_TEST(test_TC_REQUEST_HK_HISTORY_without_since_sends_no_params);
    POCAT_RUN_TEST(test_TC_REQUEST_HK_HISTORY_partial_since_sends_no_params);
    POCAT_RUN_TEST(test_TC_REQUEST_HK_HISTORY_while_arq_busy_nacks_busy);
    POCAT_RUN_TEST(test_TC_REQUEST_PAYLOAD_DATA_starts_arq_downlink);
    POCAT_RUN_TEST(test_TC_REQUEST_PAYLOAD_DATA_short_sends_no_params);
    POCAT_RUN_TEST(test_TC_REQUEST_OBC_LOG_starts_arq_downlink);
    /* 5. ARQ uploads */
    POCAT_RUN_TEST(test_TC_UPLOAD_TLE_BEGIN_arms_arq_uplink);
    POCAT_RUN_TEST(test_TC_UPLOAD_ADCS_CAL_BEGIN_arms_arq_uplink);
    /* 6. EPS heater and power rails */
    POCAT_RUN_TEST(test_TC_EPS_HEATER_ENABLE_notifies_eps);
    POCAT_RUN_TEST(test_TC_EPS_HEATER_DISABLE_notifies_eps);
    POCAT_RUN_TEST(test_TC_POL_rails_are_accepted_but_not_implemented);
    /* 7. Memory management */
    POCAT_RUN_TEST(test_TC_CLEAR_PL_DATA_notifies_obdh);
    POCAT_RUN_TEST(test_TC_CLEAR_FLASH_notifies_obdh);
    POCAT_RUN_TEST(test_TC_CLEAR_HT_notifies_obdh);
    /* 8. Communications control */
    POCAT_RUN_TEST(test_TC_COMMS_STOP_TX_notifies_comms);
    POCAT_RUN_TEST(test_TC_COMMS_RESUME_TX_notifies_comms);
    POCAT_RUN_TEST(test_TC_SET_BEACON_PERIOD_sets_period_in_ms);
    POCAT_RUN_TEST(test_TC_SET_BEACON_PERIOD_max_does_not_overflow);
    POCAT_RUN_TEST(test_TC_SET_BEACON_PERIOD_short_is_ignored);
    POCAT_RUN_TEST(test_TC_SET_BEACON_PERIOD_zero_is_rejected);
    /* 9. Payload and OBC control */
    POCAT_RUN_TEST(test_TC_PAYLOAD_SCHEDULE_activates_payload);
    POCAT_RUN_TEST(test_TC_PAYLOAD_DEACTIVATE_deactivates_payload);
    POCAT_RUN_TEST(test_TC_OBC_HARD_REBOOT_notifies_obc);
    POCAT_RUN_TEST(test_TC_OBC_SOFT_REBOOT_notifies_obc);
    POCAT_RUN_TEST(test_TC_OBC_PERIPH_REBOOT_notifies_obc);
    POCAT_RUN_TEST(test_TC_OBC_DEBUG_MODE_is_accepted_but_not_implemented);
    /* 10. Unknown, malformed and edge cases */
    POCAT_RUN_TEST(test_unknown_tc_id_nacks_unknown);
    POCAT_RUN_TEST(test_every_tc_id_is_either_handled_or_nacked);
    POCAT_RUN_TEST(test_frame_without_tc_id_is_ignored);
    POCAT_RUN_TEST(test_last_tc_and_result_are_tracked_for_beacon);
    POCAT_RUN_TEST(test_tc_for_missing_task_is_dropped_safely);
    POCAT_RUN_TEST(test_nack_is_dropped_when_tx_queue_full);
    POCAT_RUN_TEST(test_tm_is_dropped_when_tx_queue_full);
    return POCAT_END();
}
