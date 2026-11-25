/**
 * @file test_arq.c
 * @brief Host tests for the ARQ session engine (`arq.c`), uplink side: arming, DATA_BEGIN, block reassembly, window DATA_ACKs, DATA_END with CRC-32 and the flash write that follows.
 * @date 2026-11-12
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/arq.c, uplink (ground station → OBC) path:
 *   1. `arq_arm_ul(type)`: a TC (`TC_UPLOAD_TLE_BEGIN` / `_ADCS_CAL_BEGIN`)
 *      arms the receiver for one transfer type;
 *   2. `arq_rx(DATA_BEGIN)`: opens the session (session id, total blocks,
 *      block size, window size, total bytes) and queues a DATA_ACK;
 *   3. `arq_rx(DATA)`: stores each block in the 512-byte reassembly buffer,
 *      tracks the lowest missing block (`ul_rx_base`) plus a 64-bit bitmap of
 *      the blocks above it, and queues a DATA_ACK after each window;
 *   4. `arq_rx(DATA_END)`: checks the CRC-32/ISO-HDLC of the reassembled
 *      bytes, writes them to flash through OBDH and notifies ADCS.
 * - Also the session guards shared with the downlink: `arq_begin_dl()` is
 *   refused while an uplink is armed and `arq_arm_ul()` while a downlink runs
 *   (these return codes are the premise of KI-16 in test_tc_handler).
 * - The downlink loop `arq_run_dl()` is **not** covered here yet (phase-3
 *   spec B3.6 tests 20..24, priority P3).
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below: `arq.c` (under test, its `g_sess` and `crc32_*` are
 *   static), `lora_cfg.c`, `task_management.c`, `health.c` (they hold
 *   statics that arq.c reads: LoRa parameters, task handles, health bits).
 * - SOURCES: `notifications.c`, `timing.c`, `cad.c`, `frame.c`, `auth.c`
 *   (stateless helpers; DATA_ACK frames are really encoded and signed with the
 *   test PSK), `stub_task_entries.c`, `fake_obdh_requests.c`,
 *   `fake_comms_env.c` and `fake_link.c`.
 * - The real `arq_begin_dl()` / `arq_arm_ul()` override the weak fakes in
 *   `fake_comms_env.c`, so the `fake_arq_*` recorders stay at 0 in this suite.
 *
 * **Fake setup**
 * - `fake_obdh_requests`: the flash is a 1 MB RAM image. `arq.c` writes the
 *   finished upload with `obdh_write_request()`, so the test reads the bytes
 *   back with `fake_obdh_flash(addr)` and checks the call log.
 * - `fake_comms_env`: provides the comms TX queue (only `COMMS_QUEUE_LEN` = 2
 *   entries: every test pops each DATA_ACK as soon as it is queued) and
 *   `comms_next_seq()`.
 * - `fake_link`: linked only so the downlink radio calls in arq.c resolve;
 *   no uplink test touches the radio.
 * - The 7 subsystem tasks are created (never run) so ADCS and TRANSCEIVER
 *   have handles that can be checked for notifications.
 *
 * **Frame layouts.** `arq_rx()` takes already decoded and authenticated frames
 * (the transceiver did that). The builders below fill an `AirFrame_t` with the
 * byte layout the code reads, which differs per frame type: DATA_BEGIN starts
 * with BODY_VER, DATA and DATA_END start with the session id.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_arq
 * docker run --rm -v "${PWD}:/app" pocat-tests test_arq -l
 * docker run --rm -v "${PWD}:/app" pocat-tests test_arq -f UL_TLE
 * @endcode
 */
#include <stdio.h>
#include <stdlib.h>
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_obdh_requests.h"
#include "fake_comms_env.h"
#include "fake_link.h"
#include "pocat_test_main.h"

#include "arq.c"
#include "lora_cfg.c"
#include "task_management.c"
#include "health.c"

/** @brief Session id the fake ground station uses in every uplink. */
#define SID 0x5Au

/** @brief RX queue, created like the comms one (only used by the downlink path, kept for symmetry with the spec). */
static QueueHandle_t rx_q;

/** @brief Scratch upload data, filled by fill_pattern(). Larger than `ARQ_UL_BUF_MAX` is never needed. */
static uint8_t data[ARQ_UL_BUF_MAX];

/* =========================================================================
 * Helpers
 * ========================================================================= */

/**
 * @brief Independent reference CRC-32/ISO-HDLC (the zlib/Ethernet CRC), table driven.
 * @details Written differently from arq.c's bitwise loop on purpose, so a
 * shared mistake is unlikely. Reflected polynomial 0xEDB88320, init and final
 * XOR 0xFFFFFFFF; check value for "123456789" is 0xCBF43926.
 * @param buf Bytes to checksum.
 * @param n   Number of bytes.
 * @return The finished CRC.
 */
static uint32_t ref_crc32(const uint8_t *buf, uint32_t n)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256u; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = 1;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) c = table[(c ^ buf[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/**
 * @brief Helper: fills the first @p n bytes of #data with a non-repeating pattern seeded by @p seed.
 * @details Each byte depends on its index, so a block stored at the wrong
 * offset or in the wrong order changes the content.
 */
static void fill_pattern(uint32_t n, uint8_t seed)
{
    for (uint32_t i = 0; i < n; i++) data[i] = (uint8_t)(seed + i * 7u + (i >> 8));
}

/**
 * @brief Helper: passes a DATA_BEGIN frame to `arq_rx()` with the layout `[BODY_VER, SID, TYPE, TOTAL_BLOCKS BE16, BLOCK_SIZE, WINDOW_SIZE, TOTAL_BYTES BE32]`.
 * @param sid          Session id.
 * @param type         TRANSFER_* code announced by the ground.
 * @param total_blocks Number of DATA blocks that will follow.
 * @param bsize        Block size in bytes.
 * @param wsz          Window size (blocks per DATA_ACK).
 * @param total_bytes  Total upload length.
 * @param len          Frame payload length (11 is the correct value; pass less to test the length check).
 * @return What `arq_rx()` returned.
 */
static int ul_begin_len(uint8_t sid, uint8_t type, uint16_t total_blocks, uint8_t bsize,
                        uint8_t wsz, uint32_t total_bytes, uint8_t len)
{
    AirFrame_t f;
    memset(&f, 0, sizeof f);
    f.ver = AIR_PROTOCOL_VER;
    f.type = AIR_DATA_BEGIN;
    f.flags = AIR_FLAG_REQUIRES_ACK | AIR_FLAG_AUTH_TAG;
    f.seq = 1;
    f.payload[0] = AIR_BODY_VER;
    f.payload[1] = sid;
    f.payload[2] = type;
    f.payload[3] = (uint8_t)(total_blocks >> 8);
    f.payload[4] = (uint8_t)total_blocks;
    f.payload[5] = bsize;
    f.payload[6] = wsz;
    f.payload[7] = (uint8_t)(total_bytes >> 24);
    f.payload[8] = (uint8_t)(total_bytes >> 16);
    f.payload[9] = (uint8_t)(total_bytes >> 8);
    f.payload[10] = (uint8_t)total_bytes;
    f.len = len;
    return arq_rx(&f);
}

/** @brief Helper: ul_begin_len() with the correct 11-byte payload. */
static int ul_begin(uint8_t sid, uint8_t type, uint16_t total_blocks, uint8_t bsize,
                    uint8_t wsz, uint32_t total_bytes)
{
    return ul_begin_len(sid, type, total_blocks, bsize, wsz, total_bytes, 11u);
}

/**
 * @brief Helper: passes one DATA frame `[SID, IDX BE16, data...]` to `arq_rx()`.
 * @param sid Session id.
 * @param idx Block index.
 * @param d   Block bytes.
 * @param n   Number of block bytes (at most 239 so the payload fits 242).
 * @return What `arq_rx()` returned (0 for a DATA frame).
 */
static int ul_data(uint8_t sid, uint16_t idx, const uint8_t *d, uint8_t n)
{
    AirFrame_t f;
    memset(&f, 0, sizeof f);
    f.ver = AIR_PROTOCOL_VER;
    f.type = AIR_DATA;
    f.flags = AIR_FLAG_AUTH_TAG;
    f.seq = 2;
    f.payload[0] = sid;
    f.payload[1] = (uint8_t)(idx >> 8);
    f.payload[2] = (uint8_t)idx;
    memcpy(&f.payload[3], d, n);
    f.len = (uint8_t)(3u + n);
    return arq_rx(&f);
}

/**
 * @brief Helper: passes a DATA_END frame `[SID, STATUS, CRC BE32]` to `arq_rx()`.
 * @return 1 if the upload completed and was written, 0 otherwise.
 */
static int ul_end(uint8_t sid, uint8_t status, uint32_t crc)
{
    AirFrame_t f;
    memset(&f, 0, sizeof f);
    f.ver = AIR_PROTOCOL_VER;
    f.type = AIR_DATA_END;
    f.flags = AIR_FLAG_REQUIRES_ACK | AIR_FLAG_AUTH_TAG;
    f.seq = 3;
    f.payload[0] = sid;
    f.payload[1] = status;
    f.payload[2] = (uint8_t)(crc >> 24);
    f.payload[3] = (uint8_t)(crc >> 16);
    f.payload[4] = (uint8_t)(crc >> 8);
    f.payload[5] = (uint8_t)crc;
    f.len = 6;
    return arq_rx(&f);
}

/**
 * @brief Helper: sends the first @p n bytes of #data as consecutive DATA blocks of @p bsize bytes (the last one shorter).
 * @details Pops a DATA_ACK whenever one is queued, so the 2-entry TX queue
 * never fills. Returns the number of DATA_ACKs popped.
 */
static int send_all_blocks(uint32_t n, uint8_t bsize)
{
    int acks = 0;
    uint16_t blocks = (uint16_t)((n + bsize - 1u) / bsize);
    for (uint16_t b = 0; b < blocks; b++) {
        uint32_t off = (uint32_t)b * bsize;
        uint8_t len = (uint8_t)((n - off) < bsize ? (n - off) : bsize);
        ul_data(SID, b, &data[off], len);
        TxQueueEntry_t e;
        while (xQueueReceive(comms_get_tx_queue(), &e, 0) == pdTRUE) acks++;
    }
    return acks;
}

/**
 * @brief Helper: takes the next entry off the comms TX queue, checks it is an authenticated 12-byte DATA_ACK, and returns the decoded frame.
 * @details DATA_ACK body = `[SID, BASE BE16, BITMAP_LEN = 8, BITMAP 8 B]`.
 * It also checks the entry is fire-and-forget (`needs_ack == 0`).
 */
static AirFrame_t pop_data_ack(void)
{
    TxQueueEntry_t e;
    AirFrame_t f;
    TEST_ASSERT_EQUAL_INT_MESSAGE(pdTRUE, xQueueReceive(comms_get_tx_queue(), &e, 0), "no DATA_ACK queued");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, air_decode(e.frame, e.frame_len, &f), "queued DATA_ACK does not verify");
    TEST_ASSERT_EQUAL_UINT8(AIR_DATA_ACK, f.type);
    TEST_ASSERT_EQUAL_UINT8(12, f.len);
    TEST_ASSERT_EQUAL_UINT8(0, e.needs_ack);
    return f;
}

/** @brief Helper: number of frames waiting in the comms TX queue. */
static unsigned tx_items(void) { return (unsigned)fake_rtos_queue_items(comms_get_tx_queue()); }

/** @brief Helper: number of `FAKE_OBDH_WRITE` entries in the flash call log. */
static int write_count(void)
{
    int n = 0;
    for (int i = 0; i < fake_obdh_call_count(); i++)
        if (fake_obdh_call_at(i).op == FAKE_OBDH_WRITE) n++;
    return n;
}

/** @brief Helper: the index-0 bits ever sent to the ADCS task. */
static uint32_t adcs_bits(void) { return fake_rtos_bits_sent_to(tm_get_task_handle(TM_TASK_ADCS)); }

/**
 * @brief Runs before every test: a freshly booted OBC with no ARQ session.
 * @details It does the following:
 * - resets the fake RTOS, HAL, flash image (all 0xFF), comms environment and
 *   link;
 * - resets the statics of the included modules: task table handles and
 *   `paused`, `task_event_group_handle`, the health statics, then
 *   `health_init()`;
 * - zeroes `g_sess` (arq.c's only static: state IDLE, empty reassembly
 *   buffer), so no session leaks from one test to the next;
 * - restores the boot LoRa configuration with `lora_cfg_init()`;
 * - `srand(1)` so `cad.c`'s back-off is deterministic;
 * - creates the 7 subsystem tasks (all running) and an RX queue.
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
    fake_link_reset();
    memset(&g_sess, 0, sizeof g_sess);
    memset(data, 0, sizeof data);
    lora_cfg_init();
    srand(1);
    TEST_ASSERT_EQUAL_INT(pdPASS, tm_create_tasks(TM_TASK_ALL, 0));
    rx_q = xQueueCreate(COMMS_QUEUE_LEN, sizeof(RxAirFrame_t));
    TEST_ASSERT_NOT_NULL(rx_q);
}

/** @brief Runs after every test: checks that the code left the RTOS balanced. */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/* =========================================================================
 * 1. CRC and session opening
 * ========================================================================= */

/**
 * @brief arq.c's CRC-32 gives the standard check value 0xCBF43926 for "123456789", and matches the reference CRC on a longer buffer.
 * @details The ground station computes CRC-32/ISO-HDLC over the upload; a
 * different variant on board would reject every upload (or, worse, accept
 * corrupted ones if both sides were wrong in a compatible way).
 */
void test_crc32_known_answer(void)
{
    const uint8_t check[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_finish(crc32_update(0xFFFFFFFFu, check, 9)));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, ref_crc32(check, 9));

    fill_pattern(300, 0x11);
    TEST_ASSERT_EQUAL_HEX32(ref_crc32(data, 300), crc32_finish(crc32_update(0xFFFFFFFFu, data, 300)));
}

/**
 * @brief A DATA_BEGIN that arrives without a prior arm TC is ignored: no session, no DATA_ACK.
 * @details Matters because only an authenticated TC (`TC_UPLOAD_*_BEGIN`) may
 * open an uplink that ends in a flash write.
 */
void test_data_begin_without_arm_is_ignored(void)
{
    TEST_ASSERT_EQUAL_INT(0, ul_begin(SID, TRANSFER_TLE_UPLOAD, 2, 100, 16, 138));
    TEST_ASSERT_EQUAL_INT(ARQ_IDLE, g_sess.state);
    TEST_ASSERT_FALSE(arq_active());
    TEST_ASSERT_EQUAL_UINT(0, tx_items());
}

/**
 * @brief After `arq_arm_ul()`, a DATA_BEGIN opens the session and queues an empty DATA_ACK `[SID, 0, 0, 8, 0 x 8]`, and the transceiver is told.
 * @details Expected: state `ARQ_UL_ACTIVE`, session parameters copied from
 * the frame, one authenticated DATA_ACK with base 0 and an empty bitmap,
 * `N_TRANSCEIVER_TX_READY_BIT` sent to the transceiver task. The ground waits
 * for this DATA_ACK before sending the first window.
 */
void test_data_begin_after_arm_activates_and_sends_data_ack(void)
{
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    TEST_ASSERT_EQUAL_INT(ARQ_UL_ARMED, g_sess.state);
    TEST_ASSERT_TRUE(arq_active());

    TEST_ASSERT_EQUAL_INT(0, ul_begin(SID, TRANSFER_TLE_UPLOAD, 2, 100, 16, 138));
    TEST_ASSERT_EQUAL_INT(ARQ_UL_ACTIVE, g_sess.state);
    TEST_ASSERT_EQUAL_UINT8(SID, g_sess.session_id);
    TEST_ASSERT_EQUAL_UINT16(2, g_sess.total_blocks);
    TEST_ASSERT_EQUAL_UINT8(100, g_sess.block_size);
    TEST_ASSERT_EQUAL_UINT8(16, g_sess.window_size);
    TEST_ASSERT_EQUAL_UINT32(138, g_sess.total_bytes);

    AirFrame_t a = pop_data_ack();
    const uint8_t expect[12] = { SID, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, a.payload, 12);
    TEST_ASSERT_BITS_HIGH(N_TRANSCEIVER_TX_READY_BIT,
                          fake_rtos_bits_sent_to(tm_get_task_handle(TM_TASK_TRANSCEIVER)));
}

/**
 * @brief A DATA_BEGIN shorter than 11 bytes is rejected and the receiver stays armed.
 * @details Expected: returns 0, state `ARQ_UL_ARMED`, nothing queued. The
 * ground can then resend a well-formed DATA_BEGIN.
 */
void test_data_begin_too_short_stays_armed(void)
{
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    TEST_ASSERT_EQUAL_INT(0, ul_begin_len(SID, TRANSFER_TLE_UPLOAD, 2, 100, 16, 138, 10));
    TEST_ASSERT_EQUAL_INT(ARQ_UL_ARMED, g_sess.state);
    TEST_ASSERT_EQUAL_UINT(0, tx_items());
}

/* =========================================================================
 * 2. Complete uploads
 * ========================================================================= */

/**
 * @brief A 138-byte TLE uploaded in 2 blocks (100 + 38) with the right CRC is written to `TLE_ADDR` and ADCS gets `N_ADCS_NEW_TLE`.
 * @details Arrange: arm TLE, DATA_BEGIN (2 blocks, bsize 100, window 16,
 * 138 B), both blocks, DATA_END with the reference CRC. Expected: `arq_rx`
 * returns 1, exactly one OBDH write of 138 bytes at `TLE_ADDR`, the flash
 * holds the uploaded bytes, ADCS was notified, the session is closed, and a
 * final DATA_ACK with base 2 was queued. This is the path that gives ADCS a
 * new orbit: if it breaks the satellite keeps propagating a stale TLE.
 */
void test_UL_TLE_complete_writes_tle_and_notifies_adcs(void)
{
    fill_pattern(138, 0x21);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 2, 100, 16, 138);
    (void)pop_data_ack();
    TEST_ASSERT_EQUAL_INT(0, send_all_blocks(138, 100));   /* window 16 never fills */

    TEST_ASSERT_EQUAL_INT(1, ul_end(SID, DATA_END_COMPLETE_OK, ref_crc32(data, 138)));

    TEST_ASSERT_EQUAL_INT(1, write_count());
    fake_obdh_call_t c = fake_obdh_call_at(fake_obdh_call_count() - 1);
    TEST_ASSERT_EQUAL_INT(FAKE_OBDH_WRITE, c.op);
    TEST_ASSERT_EQUAL_HEX32(TLE_ADDR, c.addr);
    TEST_ASSERT_EQUAL_UINT32(138, (uint32_t)c.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_obdh_flash(TLE_ADDR), 138);
    TEST_ASSERT_BITS_HIGH(N_ADCS_NEW_TLE, adcs_bits());
    TEST_ASSERT_FALSE(arq_active());

    AirFrame_t a = pop_data_ack();
    TEST_ASSERT_EQUAL_UINT8(SID, a.payload[0]);
    TEST_ASSERT_EQUAL_UINT16(2, (uint16_t)((a.payload[1] << 8) | a.payload[2]));
}

/**
 * @brief A DATA_END whose CRC doesn't match the reassembled bytes discards the upload: no flash write, no ADCS notification, session closed.
 * @details Same upload as the complete-TLE test, but the CRC has one bit
 * flipped. Matters because the CRC is the only end-to-end check that the
 * reassembled TLE is the one the ground sent; a corrupted TLE written to
 * flash would mis-point the satellite.
 */
void test_UL_crc_mismatch_discards_upload(void)
{
    fill_pattern(138, 0x22);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 2, 100, 16, 138);
    (void)pop_data_ack();
    send_all_blocks(138, 100);

    TEST_ASSERT_EQUAL_INT(0, ul_end(SID, DATA_END_COMPLETE_OK, ref_crc32(data, 138) ^ 1u));
    TEST_ASSERT_EQUAL_INT(0, write_count());
    TEST_ASSERT_EQUAL_HEX32(0, adcs_bits());
    TEST_ASSERT_EQUAL_INT(ARQ_IDLE, g_sess.state);
}

/**
 * @brief Blocks received out of order (2, 1, 0) are reassembled in index order; the base advances to 3 once block 0 arrives.
 * @details 3 blocks of 10 bytes. After blocks 2 and 1 the base stays 0 and
 * both are marked in the bitmap; block 0 makes the base jump to 3. The CRC
 * then matches and the flash holds the bytes in the original order. LoRa
 * frames are lost and retransmitted, so out-of-order arrival is normal.
 */
void test_UL_out_of_order_blocks_are_reassembled(void)
{
    fill_pattern(30, 0x33);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 3, 10, 16, 30);
    (void)pop_data_ack();

    ul_data(SID, 2, &data[20], 10);
    ul_data(SID, 1, &data[10], 10);
    TEST_ASSERT_EQUAL_UINT16(0, g_sess.ul_rx_base);
    TEST_ASSERT_EQUAL_HEX8(0x03, g_sess.ul_bitmap[0]);
    ul_data(SID, 0, &data[0], 10);
    TEST_ASSERT_EQUAL_UINT16(3, g_sess.ul_rx_base);
    TEST_ASSERT_EQUAL_UINT16(3, g_sess.ul_blocks_received);

    TEST_ASSERT_EQUAL_INT(1, ul_end(SID, DATA_END_COMPLETE_OK, ref_crc32(data, 30)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_obdh_flash(TLE_ADDR), 30);
}

/**
 * @brief A block received twice is counted once.
 * @details Block 1 (above the base) is sent twice. Expected:
 * `ul_blocks_received == 1`. The counter drives the per-window DATA_ACK, so
 * counting duplicates would make the OBC acknowledge windows early.
 */
void test_UL_duplicate_block_is_counted_once(void)
{
    fill_pattern(30, 0x44);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 3, 10, 16, 30);
    (void)pop_data_ack();

    ul_data(SID, 1, &data[10], 10);
    ul_data(SID, 1, &data[10], 10);
    TEST_ASSERT_EQUAL_UINT16(1, g_sess.ul_blocks_received);
    TEST_ASSERT_EQUAL_UINT16(0, g_sess.ul_rx_base);
}

/**
 * @brief A DATA frame with a different session id is ignored.
 * @details Expected: nothing stored, nothing counted, nothing queued. A
 * stale frame from an earlier session must not leak into the current upload.
 */
void test_UL_data_for_other_session_is_ignored(void)
{
    fill_pattern(30, 0x55);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 3, 10, 16, 30);
    (void)pop_data_ack();

    TEST_ASSERT_EQUAL_INT(0, ul_data(SID + 1u, 0, data, 10));
    TEST_ASSERT_EQUAL_UINT16(0, g_sess.ul_blocks_received);
    TEST_ASSERT_EQUAL_UINT16(0, g_sess.ul_rx_base);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_sess.ul_buf[0]);
    TEST_ASSERT_EQUAL_UINT(0, tx_items());
}

/**
 * @brief A DATA frame whose block index is >= TOTAL_BLOCKS is ignored.
 * @details Expected: not counted, the base doesn't move, nothing is written
 * into the reassembly buffer beyond the announced size.
 */
void test_UL_block_index_beyond_total_is_ignored(void)
{
    fill_pattern(30, 0x66);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 3, 10, 16, 30);
    (void)pop_data_ack();

    TEST_ASSERT_EQUAL_INT(0, ul_data(SID, 3, data, 10));
    TEST_ASSERT_EQUAL_UINT16(0, g_sess.ul_blocks_received);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_sess.ul_buf[30]);
}

/**
 * @brief A DATA_END with status ABORTED_BY_SENDER closes the session without writing flash or notifying ADCS.
 * @details All blocks arrived, but the ground aborts. Expected: returns 0,
 * IDLE, no OBDH write, ADCS not notified, no DATA_ACK for the abort.
 */
void test_UL_abort_by_sender_returns_idle_without_writing(void)
{
    fill_pattern(30, 0x77);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 3, 10, 16, 30);
    (void)pop_data_ack();
    send_all_blocks(30, 10);

    TEST_ASSERT_EQUAL_INT(0, ul_end(SID, DATA_END_ABORTED_BY_SENDER, ref_crc32(data, 30)));
    TEST_ASSERT_EQUAL_INT(ARQ_IDLE, g_sess.state);
    TEST_ASSERT_EQUAL_INT(0, write_count());
    TEST_ASSERT_EQUAL_HEX32(0, adcs_bits());
    TEST_ASSERT_EQUAL_UINT(0, tx_items());
}

/**
 * @brief With window size 2 and 4 blocks, a DATA_ACK is queued after blocks 2 and 4 only, with base 2 and then 4.
 * @details Checks the queue after every block: empty after blocks 1 and 3.
 * The ground uses these DATA_ACKs to slide its window; one sent too early or
 * too late stalls the upload or triggers needless retransmissions.
 */
void test_UL_data_ack_sent_after_each_window(void)
{
    fill_pattern(40, 0x88);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 4, 10, 2, 40);
    (void)pop_data_ack();

    ul_data(SID, 0, &data[0], 10);
    TEST_ASSERT_EQUAL_UINT(0, tx_items());
    ul_data(SID, 1, &data[10], 10);
    AirFrame_t a = pop_data_ack();
    TEST_ASSERT_EQUAL_UINT16(2, (uint16_t)((a.payload[1] << 8) | a.payload[2]));

    ul_data(SID, 2, &data[20], 10);
    TEST_ASSERT_EQUAL_UINT(0, tx_items());
    ul_data(SID, 3, &data[30], 10);
    a = pop_data_ack();
    TEST_ASSERT_EQUAL_UINT16(4, (uint16_t)((a.payload[1] << 8) | a.payload[2]));
    TEST_ASSERT_EQUAL_UINT(0, tx_items());
}

/**
 * @brief A complete ADCS calibration upload is written to `CALIBRATION_ADDR` and ADCS gets `N_ADCS_NEW_CALIBRATION`.
 * @details 81 bytes (fits the calibration region 0x080300AB..0x080300FE) in
 * one block. Expected: one write of 81 bytes at `CALIBRATION_ADDR`, flash
 * content equal to the upload, ADCS notified with the calibration bit (and
 * not the TLE bit).
 */
void test_UL_ADCS_CAL_writes_calibration_and_notifies_adcs(void)
{
    fill_pattern(81, 0x99);
    arq_arm_ul(TRANSFER_ADCS_CALIBRATION);
    ul_begin(SID, TRANSFER_ADCS_CALIBRATION, 1, 100, 16, 81);
    (void)pop_data_ack();
    send_all_blocks(81, 100);

    TEST_ASSERT_EQUAL_INT(1, ul_end(SID, DATA_END_COMPLETE_OK, ref_crc32(data, 81)));
    TEST_ASSERT_EQUAL_INT(1, write_count());
    fake_obdh_call_t c = fake_obdh_call_at(fake_obdh_call_count() - 1);
    TEST_ASSERT_EQUAL_HEX32(CALIBRATION_ADDR, c.addr);
    TEST_ASSERT_EQUAL_UINT32(81, (uint32_t)c.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_obdh_flash(CALIBRATION_ADDR), 81);
    TEST_ASSERT_EQUAL_HEX32(N_ADCS_NEW_CALIBRATION, adcs_bits());
}

/**
 * @brief A flight-parameters upload of 20 bytes is truncated to the 8-byte `RFI_CONFIG_ADDR` region.
 * @details Expected: exactly one write at `RFI_CONFIG_ADDR` with length 8,
 * the 8 bytes equal the first 8 uploaded, the byte after stays erased, ADCS is
 * not notified. This is the only transfer type whose length is bounded by its
 * region (compare KI-22).
 */
void test_UL_flight_params_truncated_to_8_bytes(void)
{
    fill_pattern(20, 0xAA);
    arq_arm_ul(TRANSFER_FLIGHT_PARAMS);
    ul_begin(SID, TRANSFER_FLIGHT_PARAMS, 1, 100, 16, 20);
    (void)pop_data_ack();
    send_all_blocks(20, 100);

    TEST_ASSERT_EQUAL_INT(1, ul_end(SID, DATA_END_COMPLETE_OK, ref_crc32(data, 20)));
    TEST_ASSERT_EQUAL_INT(1, write_count());
    fake_obdh_call_t c = fake_obdh_call_at(fake_obdh_call_count() - 1);
    TEST_ASSERT_EQUAL_HEX32(RFI_CONFIG_ADDR, c.addr);
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)c.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_obdh_flash(RFI_CONFIG_ADDR), 8);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake_obdh_flash(RFI_CONFIG_ADDR + 8u)[0]);
    TEST_ASSERT_EQUAL_HEX32(0, adcs_bits());
}

/* =========================================================================
 * 3. One session at a time
 * ========================================================================= */

/**
 * @brief While an uplink is armed, `arq_begin_dl()` returns -1 and changes nothing.
 * @details Expected: -1, state still `ARQ_UL_ARMED` with the armed type, and
 * the transceiver is not told to start a downlink. This confirms the premise
 * of KI-16: tc_handler gets -1 here but doesn't NACK the TC.
 */
void test_begin_dl_refused_while_ul_armed(void)
{
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    TEST_ASSERT_EQUAL_INT(-1, arq_begin_dl(7, TRANSFER_HK_HISTORY, NULL, 0));
    TEST_ASSERT_EQUAL_INT(ARQ_UL_ARMED, g_sess.state);
    TEST_ASSERT_EQUAL_INT(TRANSFER_TLE_UPLOAD, g_sess.transfer_type);
    TEST_ASSERT_BITS_LOW(N_TRANSCEIVER_ARQ_DL_BIT,
                         fake_rtos_bits_sent_to(tm_get_task_handle(TM_TASK_TRANSCEIVER)));
}

/**
 * @brief While a downlink is active, `arq_arm_ul()` is refused: the downlink session is left intact.
 * @details `arq_begin_dl(7, HK_HISTORY)` first (it notifies the transceiver
 * with `N_TRANSCEIVER_ARQ_DL_BIT`), then `arq_arm_ul(TLE)`. Expected: state
 * `ARQ_DL_ACTIVE`, session id 7, type HK_HISTORY, total 2048 bytes in 11
 * blocks. Arming over a running downlink would corrupt the downlink state
 * that the transceiver task is using.
 */
void test_arm_ul_refused_while_dl_active(void)
{
    TEST_ASSERT_EQUAL_INT(0, arq_begin_dl(7, TRANSFER_HK_HISTORY, NULL, 0));
    TEST_ASSERT_BITS_HIGH(N_TRANSCEIVER_ARQ_DL_BIT,
                          fake_rtos_bits_sent_to(tm_get_task_handle(TM_TASK_TRANSCEIVER)));
    arq_arm_ul(TRANSFER_TLE_UPLOAD);

    TEST_ASSERT_EQUAL_INT(ARQ_DL_ACTIVE, g_sess.state);
    TEST_ASSERT_EQUAL_UINT8(7, g_sess.session_id);
    TEST_ASSERT_EQUAL_INT(TRANSFER_HK_HISTORY, g_sess.transfer_type);
    TEST_ASSERT_EQUAL_UINT32(2048, g_sess.total_bytes);
    TEST_ASSERT_EQUAL_UINT16(11, g_sess.total_blocks);
}

/* =========================================================================
 * 4. Known bugs
 * ========================================================================= */

/**
 * @brief A DATA_BEGIN with WINDOW_SIZE = 0 should be rejected (receiver stays armed), and a following DATA must be harmless.
 * @details Arrange: arm TLE, DATA_BEGIN with window 0, then one DATA block.
 * Expected: the BEGIN is refused (state `ARQ_UL_ARMED`), so the DATA is
 * ignored. Actual: the BEGIN is accepted, and the DATA computes
 * `ul_blocks_received % 0` (arq.c:539): undefined behaviour. On the host
 * UBSan aborts the whole suite binary ("division by zero"); on the Cortex-M4
 * an integer divide by zero returns 0 unless DIV_0_TRP is set, so the
 * DATA_ACK logic silently misbehaves. The frame is authenticated, so this is
 * a robustness issue against a ground mistake.
 * The IGNORE line must stay first, otherwise this test crashes the binary.
 * @bug KI-20 (test/README.md) expected state ARMED after the BEGIN, actual
 * ARQ_UL_ACTIVE and a modulo by zero on the next DATA.
 */
void test_UL_window_size_zero_is_rejected(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-20: DATA_BEGIN with WINDOW_SIZE=0 is accepted; the next DATA does a modulo by zero");
    fill_pattern(10, 0xBB);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 1, 10, 0, 10);
    TEST_ASSERT_EQUAL_INT(0, ul_data(SID, 0, data, 10));
    TEST_ASSERT_EQUAL_INT(ARQ_UL_ARMED, g_sess.state);
}

/**
 * @brief A DATA_BEGIN whose TRANSFER_TYPE differs from the armed one should be rejected (receiver stays armed, no DATA_ACK).
 * @details Arrange: arm TLE, DATA_BEGIN announcing ADCS_CALIBRATION.
 * Expected: state `ARQ_UL_ARMED`, nothing queued. Actual: the mismatch is
 * only printed; the session goes active and a DATA_ACK is queued. Worse,
 * `g_sess.transfer_type` keeps the armed type, so calibration bytes would be
 * written to `TLE_ADDR` and ADCS told it has a new TLE.
 * @bug KI-21 (test/README.md) expected ARMED and no DATA_ACK, actual
 * ARQ_UL_ACTIVE and one DATA_ACK queued.
 */
void test_UL_type_mismatch_is_rejected(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-21: DATA_BEGIN with a different TRANSFER_TYPE than armed is only logged");
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_ADCS_CALIBRATION, 1, 100, 16, 81);
    TEST_ASSERT_EQUAL_INT(ARQ_UL_ARMED, g_sess.state);
    TEST_ASSERT_EQUAL_UINT(0, tx_items());
}

/**
 * @brief A TLE upload longer than the 138-byte TLE region must not overwrite the neighbouring configuration.
 * @details Arrange: preset the calibration region and the boot time with
 * known non-erased values; arm TLE and upload 500 bytes (blocks of 200, 200,
 * 100; still within `ARQ_UL_BUF_MAX` = 512) with a valid CRC. Expected: the
 * bytes at `CALIBRATION_ADDR`, `ANTENNA_DEPLOYED_ADDR` and `BOOT_TIME_ADDR`
 * are unchanged (the upload is rejected or truncated to its region). Actual:
 * all 500 bytes are written from `TLE_ADDR` (0x08030020) up to 0x08030213,
 * overwriting calibration, the legacy telemetry area, the comms counters,
 * `ANTENNA_DEPLOYED_ADDR` (0x08030205) and `BOOT_TIME_ADDR` (0x08030206).
 * A wrong antenna flag can re-trigger deployment; a wrong boot time breaks
 * the beacon uptime.
 * @bug KI-22 (test/README.md) expected neighbours untouched, actual
 * overwritten with upload bytes.
 */
void test_UL_TLE_longer_than_region_does_not_overwrite_neighbours(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-22: UL length is bounded only by ARQ_UL_BUF_MAX, not by the TLE/calibration region");
    const uint8_t cal[4] = { 0xC1, 0xC2, 0xC3, 0xC4 };
    const uint32_t boot = 1700000000u;
    memcpy(fake_obdh_flash(CALIBRATION_ADDR), cal, 4);
    fake_obdh_flash(ANTENNA_DEPLOYED_ADDR)[0] = 0x01;
    memcpy(fake_obdh_flash(BOOT_TIME_ADDR), &boot, 4);

    fill_pattern(500, 0xCC);
    arq_arm_ul(TRANSFER_TLE_UPLOAD);
    ul_begin(SID, TRANSFER_TLE_UPLOAD, 3, 200, 16, 500);
    (void)pop_data_ack();
    send_all_blocks(500, 200);
    (void)ul_end(SID, DATA_END_COMPLETE_OK, ref_crc32(data, 500));

    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(cal, fake_obdh_flash(CALIBRATION_ADDR), 4, "calibration overwritten");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, fake_obdh_flash(ANTENNA_DEPLOYED_ADDR)[0], "antenna flag overwritten");
    uint32_t got;
    memcpy(&got, fake_obdh_flash(BOOT_TIME_ADDR), 4);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(boot, got, "boot time overwritten");
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
    POCAT_RUN_TEST(test_crc32_known_answer);
    POCAT_RUN_TEST(test_data_begin_without_arm_is_ignored);
    POCAT_RUN_TEST(test_data_begin_after_arm_activates_and_sends_data_ack);
    POCAT_RUN_TEST(test_data_begin_too_short_stays_armed);
    POCAT_RUN_TEST(test_UL_TLE_complete_writes_tle_and_notifies_adcs);
    POCAT_RUN_TEST(test_UL_crc_mismatch_discards_upload);
    POCAT_RUN_TEST(test_UL_out_of_order_blocks_are_reassembled);
    POCAT_RUN_TEST(test_UL_duplicate_block_is_counted_once);
    POCAT_RUN_TEST(test_UL_data_for_other_session_is_ignored);
    POCAT_RUN_TEST(test_UL_block_index_beyond_total_is_ignored);
    POCAT_RUN_TEST(test_UL_abort_by_sender_returns_idle_without_writing);
    POCAT_RUN_TEST(test_UL_data_ack_sent_after_each_window);
    POCAT_RUN_TEST(test_UL_ADCS_CAL_writes_calibration_and_notifies_adcs);
    POCAT_RUN_TEST(test_UL_flight_params_truncated_to_8_bytes);
    POCAT_RUN_TEST(test_begin_dl_refused_while_ul_armed);
    POCAT_RUN_TEST(test_arm_ul_refused_while_dl_active);
    POCAT_RUN_TEST(test_UL_window_size_zero_is_rejected);
    POCAT_RUN_TEST(test_UL_type_mismatch_is_rejected);
    POCAT_RUN_TEST(test_UL_TLE_longer_than_region_does_not_overwrite_neighbours);
    return POCAT_END();
}
