/**
 * @file test_saw.c
 * @brief Host tests for the stop-and-wait sender (`saw_send`): ACK, NACK, retransmission, timeout, CAD give-up and forwarding of foreign frames.
 * @date 2026-10-29
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/saw.c `saw_send()`:
 *   1. listen-before-talk (`cad_precheck()`, up to 4 scans);
 *   2. transmit, wait for TX_DONE (5 s limit);
 *   3. receive for `ack_timeout_ms(len)`; an ACK/NACK with the right REF_SEQ
 *      ends the exchange (`SAW_OK` / `SAW_NACKED`);
 *   4. otherwise retransmit with the IS_RETX flag, up to `SAW_MAX_RETX` (3)
 *      times, then `SAW_TIMEOUT`;
 *   5. any other valid frame heard while waiting is forwarded to the comms
 *      RX queue; invalid frames are dropped.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below: `saw.c` and `lora_cfg.c`.
 * - SOURCES: `timing.c`, `cad.c`, `frame.c`, `auth.c` (stateless helpers),
 *   `fake_comms_env.c` (radio configuration, RTC, comms queues) and
 *   `fake_link.c`.
 *
 * **Fake setup**
 * - `fake_link` plays the SX1262 and the ground station. A *responder*
 *   callback answers each transmitted frame (ACK, NACK, silence, ...).
 *   Unsolicited frames are injected with `fake_link_inject_rx()`.
 * - The test task stays the current task: `saw_send()` waits on its
 *   notifications and `fake_link` notifies it.
 * - `cad.c` uses `rand()`: `srand(1)` in `setUp()` makes the back-off
 *   deterministic.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_saw
 * docker run --rm -v "${PWD}:/app" pocat-tests test_saw -f retransmits
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_comms_env.h"
#include "fake_link.h"
#include "timing.h"
#include "pocat_test_main.h"

#include "saw.c"
#include "lora_cfg.c"

/** @brief Sequence number of the frame the tests send. */
#define MY_SEQ 0x42u

/** @brief RX queue handed to `saw_send()` (foreign frames are forwarded here). */
static QueueHandle_t rx_q;

/** @brief Call counter of #ack_second_try_responder. */
static int second_try_calls;

/**
 * @brief Runs before every test: fresh fakes, empty link, default LoRa config, deterministic random numbers and an RX queue.
 * @details Resets the fake RTOS, HAL, comms environment and the link;
 * `lora_cfg_init()` restores the boot LoRa parameters (the timeouts depend on
 * them); `srand(1)` fixes the CAD back-off; #second_try_calls is zeroed; and
 * the RX queue is created after `fake_rtos_reset()` (a stale handle would
 * point at a freed queue).
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    fake_comms_env_reset();
    fake_link_reset();
    lora_cfg_init();
    srand(1);
    second_try_calls = 0;
    rx_q = xQueueCreate(COMMS_QUEUE_LEN, sizeof(RxAirFrame_t));
    TEST_ASSERT_NOT_NULL(rx_q);
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
 * @brief Helper: builds an authenticated AIR_TM frame that requires an ACK.
 * @param seq Sequence number.
 * @param buf Output buffer (at least 255 bytes).
 * @return Frame length (5 + 8 payload + 8 tag = 21).
 */
static uint8_t make_tm(uint8_t seq, uint8_t *buf)
{
    const uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    return air_encode(buf, AIR_TM, AIR_FLAG_REQUIRES_ACK, seq, payload, 8);
}

/**
 * @brief Helper: builds an authenticated ACK frame with an arbitrary reference type and sequence.
 * @param ref_type Type of the frame being acknowledged.
 * @param ref_seq  Sequence of the frame being acknowledged.
 * @param buf      Output buffer.
 * @return Frame length.
 */
static uint8_t encode_ack_raw(uint8_t ref_type, uint8_t ref_seq, uint8_t *buf)
{
    return air_encode_ack(buf, 0x90, ref_type, ref_seq);
}

/** @brief Responder: always answers with a valid ACK for the frame just transmitted. */
static bool ack_responder(const uint8_t *tx, uint8_t tx_len, uint8_t *reply, uint8_t *reply_len)
{
    AirFrame_t f;
    if (air_decode(tx, tx_len, &f) != 0) return false;
    *reply_len = air_encode_ack(reply, 0x90, f.type, f.seq);
    return true;
}

/** @brief Responder: always answers with a NACK (reason 1) for the frame just transmitted. */
static bool nack_responder(const uint8_t *tx, uint8_t tx_len, uint8_t *reply, uint8_t *reply_len)
{
    AirFrame_t f;
    if (air_decode(tx, tx_len, &f) != 0) return false;
    *reply_len = air_encode_nack(reply, 0x91, f.type, f.seq, 0x01);
    return true;
}

/** @brief Responder: stays silent for the first transmission and ACKs the second one (the ground missed the first). */
static bool ack_second_try_responder(const uint8_t *tx, uint8_t tx_len, uint8_t *reply, uint8_t *reply_len)
{
    second_try_calls++;
    if (second_try_calls < 2) return false;
    return ack_responder(tx, tx_len, reply, reply_len);
}

/* =========================================================================
 * 1. Outcomes of an exchange
 * ========================================================================= */

/**
 * @brief An ACK on the first attempt returns `SAW_OK` after exactly one transmission, without the retransmission flag.
 * @details Matters because a spurious retransmission wastes airtime and
 * makes the ground see duplicates.
 */
void test_ack_on_first_attempt_returns_ok(void)
{
    uint8_t f[255], tx[255];
    uint8_t n = make_tm(MY_SEQ, f);
    fake_link_set_responder(ack_responder);

    TEST_ASSERT_EQUAL_INT(SAW_OK, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_EQUAL_INT(1, fake_link_tx_count());
    fake_link_tx_frame(0, tx);
    TEST_ASSERT_EQUAL_UINT8(0, tx[2] & AIR_FLAG_IS_RETX);
}

/**
 * @brief A NACK for our sequence number ends the exchange with `SAW_NACKED` and no retransmission.
 */
void test_nack_returns_nacked(void)
{
    uint8_t f[255];
    uint8_t n = make_tm(MY_SEQ, f);
    fake_link_set_responder(nack_responder);

    TEST_ASSERT_EQUAL_INT(SAW_NACKED, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_EQUAL_INT(1, fake_link_tx_count());
}

/**
 * @brief With a silent ground, the frame is sent 4 times (1 + 3 retransmissions) and the result is `SAW_TIMEOUT`.
 * @details Expected: 4 transmissions, frame 0 without IS_RETX, frames 1..3
 * with it, and at least 4 x `ack_timeout_ms(len)` ticks elapsed. Matters
 * because this bounds how long the transceiver is blocked on a dead link.
 */
void test_no_reply_retransmits_3_times_then_times_out(void)
{
    uint8_t f[255], tx[255];
    uint8_t n = make_tm(MY_SEQ, f);
    TickType_t t0 = xTaskGetTickCount();

    TEST_ASSERT_EQUAL_INT(SAW_TIMEOUT, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_EQUAL_INT(4, fake_link_tx_count());
    fake_link_tx_frame(0, tx);
    TEST_ASSERT_EQUAL_UINT8(0, tx[2] & AIR_FLAG_IS_RETX);
    for (int i = 1; i < 4; i++) {
        fake_link_tx_frame(i, tx);
        TEST_ASSERT_TRUE_MESSAGE((tx[2] & AIR_FLAG_IS_RETX) != 0, "retransmission must carry IS_RETX");
    }
    TEST_ASSERT_TRUE((uint32_t)(xTaskGetTickCount() - t0) >= 4u * ack_timeout_ms(n));
}

/**
 * @brief If only the second transmission is ACKed, `saw_send()` returns `SAW_OK` after 2 transmissions.
 */
void test_ack_on_second_attempt_returns_ok(void)
{
    uint8_t f[255];
    uint8_t n = make_tm(MY_SEQ, f);
    fake_link_set_responder(ack_second_try_responder);

    TEST_ASSERT_EQUAL_INT(SAW_OK, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_EQUAL_INT(2, fake_link_tx_count());
}

/**
 * @brief A retransmitted frame still authenticates.
 * @details IS_RETX is set after the tag was computed, and `auth_tag()`
 * excludes that flag. Expected: all 4 transmitted frames pass `air_decode()`.
 * If this broke, the ground would reject every retransmission.
 */
void test_retransmitted_frame_still_authenticates(void)
{
    uint8_t f[255], tx[255];
    uint8_t n = make_tm(MY_SEQ, f);
    (void)saw_send(f, n, MY_SEQ, AIR_TM, rx_q);

    for (int i = 0; i < fake_link_tx_count(); i++) {
        AirFrame_t d;
        uint8_t len = fake_link_tx_frame(i, tx);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, air_decode(tx, len, &d), "frame failed authentication");
    }
}

/* =========================================================================
 * 2. Frames heard while waiting
 * ========================================================================= */

/**
 * @brief A valid foreign frame heard while waiting is forwarded to the comms RX queue, with its RSSI/SNR, and the wait continues.
 * @details Inject an authenticated AIR_TC, then let the responder ACK.
 * Expected: `SAW_OK`; the RX queue holds the TC with RSSI -80 and SNR 5.
 * Matters because a telecommand that arrives while the satellite is
 * downlinking must not be lost.
 */
void test_foreign_frame_during_wait_is_forwarded_to_comms(void)
{
    uint8_t f[255], tc[255];
    uint8_t n = make_tm(MY_SEQ, f);
    const uint8_t p[4] = { 9, 8, 7, 6 };
    uint8_t tn = air_encode(tc, AIR_TC, 0, 5, p, 4);
    fake_link_inject_rx(tc, tn);
    fake_link_set_responder(ack_responder);

    TEST_ASSERT_EQUAL_INT(SAW_OK, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)fake_rtos_queue_items(rx_q));
    RxAirFrame_t pkt;
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(rx_q, &pkt, 0));
    TEST_ASSERT_EQUAL_UINT8(AIR_TC, pkt.air.type);
    TEST_ASSERT_EQUAL_INT16(-80, pkt.rssi);
    TEST_ASSERT_EQUAL_INT8(5, pkt.snr);
}

/**
 * @brief An ACK for a different sequence number is forwarded, not accepted as ours.
 * @details Inject an ACK for seq+1, then the right ACK arrives from the
 * responder. Expected: `SAW_OK` after one transmission, one frame in the RX
 * queue.
 */
void test_ack_for_other_seq_is_forwarded_not_accepted(void)
{
    uint8_t f[255], a[255];
    uint8_t n = make_tm(MY_SEQ, f);
    uint8_t an = encode_ack_raw(AIR_TM, MY_SEQ + 1u, a);
    fake_link_inject_rx(a, an);
    fake_link_set_responder(ack_responder);

    TEST_ASSERT_EQUAL_INT(SAW_OK, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_EQUAL_INT(1, fake_link_tx_count());
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)fake_rtos_queue_items(rx_q));
}

/**
 * @brief A frame with a bad authentication tag is dropped silently, not forwarded.
 * @details Inject a TC with one payload byte flipped, then the real ACK.
 * Expected: `SAW_OK` and an empty RX queue. Matters because forwarding
 * unauthenticated frames would let anyone inject telecommands.
 */
void test_corrupted_reply_is_dropped(void)
{
    uint8_t f[255], tc[255];
    uint8_t n = make_tm(MY_SEQ, f);
    const uint8_t p[4] = { 9, 8, 7, 6 };
    uint8_t tn = air_encode(tc, AIR_TC, 0, 5, p, 4);
    tc[5] ^= 0x01u;
    fake_link_inject_rx(tc, tn);
    fake_link_set_responder(ack_responder);

    TEST_ASSERT_EQUAL_INT(SAW_OK, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)fake_rtos_queue_items(rx_q));
}

/* =========================================================================
 * 3. Channel access and TX_DONE
 * ========================================================================= */

/**
 * @brief If the channel stays busy for 4 CAD scans, `saw_send()` gives up without transmitting.
 * @details Expected: `SAW_CAD_FAIL`, 0 transmissions, 4 scans. Matters
 * because transmitting over another user violates the listen-before-talk
 * rule.
 */
void test_cad_busy_gives_up_without_transmitting(void)
{
    uint8_t f[255];
    uint8_t n = make_tm(MY_SEQ, f);
    fake_link_set_scan_result(RADIOLIB_LORA_DETECTED);

    TEST_ASSERT_EQUAL_INT(SAW_CAD_FAIL, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_EQUAL_INT(0, fake_link_tx_count());
    TEST_ASSERT_EQUAL_INT(4, fake_link_scan_count());
}

/**
 * @brief If TX_DONE never fires, `saw_send()` waits the 5 s limit and still listens for the ACK.
 * @details Expected: `SAW_OK` and at least 5000 ticks elapsed. Matters
 * because a lost DIO1 interrupt must not prevent the exchange from completing.
 */
void test_missing_tx_done_still_waits_for_ack(void)
{
    uint8_t f[255];
    uint8_t n = make_tm(MY_SEQ, f);
    fake_link_set_tx_done(false);
    fake_link_set_responder(ack_responder);
    TickType_t t0 = xTaskGetTickCount();

    TEST_ASSERT_EQUAL_INT(SAW_OK, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
    TEST_ASSERT_TRUE((uint32_t)(xTaskGetTickCount() - t0) >= 5000u);
}

/**
 * @brief An ACK whose referenced frame type differs from the frame we sent should not be accepted.
 * @details Inject an ACK with the right sequence number but REF_TYPE =
 * AIR_BEACON, no responder. Expected: `SAW_TIMEOUT`. Actual: `SAW_OK`,
 * because `saw_send()` ignores `own_type` and matches on REF_SEQ only (the
 * ARQ code checks both). An ACK meant for another frame type that happens to
 * share the sequence number would be taken as ours. Low impact.
 * @bug KI-24 (test/README.md) expected SAW_TIMEOUT, actual SAW_OK.
 */
void test_ack_with_wrong_ref_type_is_not_accepted(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-24: saw_send() ignores own_type and matches ACK/NACK on REF_SEQ only");
    uint8_t f[255], a[255];
    uint8_t n = make_tm(MY_SEQ, f);
    uint8_t an = encode_ack_raw(AIR_BEACON, MY_SEQ, a);
    fake_link_inject_rx(a, an);

    TEST_ASSERT_EQUAL_INT(SAW_TIMEOUT, saw_send(f, n, MY_SEQ, AIR_TM, rx_q));
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
    POCAT_RUN_TEST(test_ack_on_first_attempt_returns_ok);
    POCAT_RUN_TEST(test_nack_returns_nacked);
    POCAT_RUN_TEST(test_no_reply_retransmits_3_times_then_times_out);
    POCAT_RUN_TEST(test_ack_on_second_attempt_returns_ok);
    POCAT_RUN_TEST(test_retransmitted_frame_still_authenticates);
    POCAT_RUN_TEST(test_foreign_frame_during_wait_is_forwarded_to_comms);
    POCAT_RUN_TEST(test_ack_for_other_seq_is_forwarded_not_accepted);
    POCAT_RUN_TEST(test_corrupted_reply_is_dropped);
    POCAT_RUN_TEST(test_cad_busy_gives_up_without_transmitting);
    POCAT_RUN_TEST(test_missing_tx_done_still_waits_for_ack);
    POCAT_RUN_TEST(test_ack_with_wrong_ref_type_is_not_accepted);
    return POCAT_END();
}
