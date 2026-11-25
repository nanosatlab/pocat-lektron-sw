/**
 * @file test_ht_compression.c
 * @brief Host tests of historic telemetry with the real POCKET+ compressor: real compressed blocks must fit the 128-byte slot format, recover after a reboot, and follow the positive-mask update rate.
 * @date 2026-11-12
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/ht_handling.c `fill_ht_from_it()` / `ht_init()` fed
 *   with realistic, constant and random beacon bodies, but this time with the
 *   **real** POCKET+ code (src/utils/compression.c). The older suites
 *   test_ht_handling and test_beacon replace it with mocks/mock_pocket_plus.c
 *   (a fixed 20-byte "compressed" packet), so variable-length packets, the
 *   "does the next packet still fit" decision and the slot header values
 *   produced by real data are only exercised here.
 * - The slot format documented in include/subsystems/comms/ht_handling.h:
 *   `[12-byte ht_slot_header][raw 20-byte reference beacon][compressed
 *   packets back to back]`, `format == HT_FORMAT_POCKET_V3`,
 *   `HT_STORED_PACKET_SIZE <= data_len <= HT_SLOT_PAYLOAD_SIZE` (116),
 *   `1 <= count <= HT_SLOT_MAX_BEACONS` (97), epoch = epoch of the reference.
 * - compression.c's public API at the `POCKET_PLUS_UPDATE_RATE` (12)
 *   boundary: a bit that stopped changing becomes predictable again only at
 *   the second positive-mask update (packet 2 x 12 + 1).
 *
 * **Not covered: round trip.** The ground decompressor (`decompression.cpp`)
 * is not in this repository, so the suite can't decompress a slot and compare
 * it with the beacons. Instead, `test_slot_payload_is_reference_plus_compressed_packets`
 * replays the compressor from each slot's reference beacon and checks the
 * slot holds exactly those packets, back to back, with no framing.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below: `ht_handling.c` (under test; its queue and staging
 *   state are static).
 * - SOURCES: `compression.c` and `fake_obdh_requests.c`. compression.c has
 *   static state too but is linked, not included: it is third-party ESA code
 *   tested only through its public API, and `startPocket()` (called by
 *   ht_handling for every new block, and by the API tests here) resets the
 *   state that matters.
 * - The real `include/utils/flash.h` is used (no `stubs_project`): HT region
 *   0x080FE000, 8 KB, 64 slots of 128 bytes, 16 per 2 KB page.
 *
 * **Fakes**
 * - `fake_obdh_requests`: the whole flash as a 1 MB RAM image (erased =
 *   0xFF). Slots are read back with `fake_obdh_flash()`.
 * - `fake_rtos` / `fake_hal` are linked by default; nothing here blocks.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_ht_compression
 * docker run --rm -v "${PWD}:/app" pocat-tests test_ht_compression -f update_rate
 * @endcode
 */
#include <stdio.h>
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_obdh_requests.h"
#include "pocat_test_main.h"

#include "ht_handling.c"

/* compression.c defines these but compression.h doesn't declare them. */
void          getMask(uint32_t *output_Mask);
uint_fast16_t getNumberOfNpBits(void);

/** @brief Most beacons a test feeds (sizes the record of what was fed). */
#define MAX_FED 1200

/** @brief Every beacon body fed in the current test, in order, so stored slots can be compared with their inputs. */
static uint8_t fed[MAX_FED][HT_BEACON_SIZE];
/** @brief Number of entries used in #fed. */
static int     n_fed;

/** @brief State of the deterministic generator used by rnd(). */
static uint32_t lcg_state;

/* =========================================================================
 * Helpers
 * ========================================================================= */

/** @brief Deterministic pseudo-random byte (32-bit LCG, Numerical Recipes constants). Same sequence on every run. */
static uint8_t rnd(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (uint8_t)(lcg_state >> 24);
}

/** @brief Helper: writes @p v big-endian into @p p[0..3]. */
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/**
 * @brief Helper: builds beacon number @p i with the layout of `build_beacon_body()` (beacon.c).
 * @details epoch = 1 700 000 000 + 30 i (one beacon every 30 s), state 1,
 * subsystem mask 0x3F, temperature 20 + (i mod 6) degC, VDDA 33 (x100 mV),
 * last TC id 0x05 / rc 0, CFG_ID 1, uptime 30 i. This is the kind of data
 * the satellite really archives: a few slowly changing fields.
 */
static void make_beacon(int i, uint8_t out[HT_BEACON_SIZE])
{
    memset(out, 0, HT_BEACON_SIZE);
    out[0] = 0x01;                                   /* AIR_BODY_VER */
    put_be32(&out[1], 1700000000u + 30u * (uint32_t)i);
    out[5] = 1;
    out[6] = 0x3F;
    out[7] = (uint8_t)(20 + (i % 6));
    out[9] = 33;
    out[12] = 0x05;
    out[14] = 1;
    put_be32(&out[15], 30u * (uint32_t)i);
}

/** @brief Helper: feeds one beacon body to `fill_ht_from_it()` and records it in #fed. */
static void feed(const uint8_t b[HT_BEACON_SIZE])
{
    TEST_ASSERT_TRUE_MESSAGE(n_fed < MAX_FED, "test fed too many beacons for its record");
    memcpy(fed[n_fed++], b, HT_BEACON_SIZE);
    fill_ht_from_it(b);
}

/** @brief Helper: feeds @p n realistic beacons (make_beacon(0..n-1)). */
static void feed_realistic(int n)
{
    uint8_t b[HT_BEACON_SIZE];
    for (int i = 0; i < n; i++) { make_beacon(i, b); feed(b); }
}

/** @brief Helper: feeds @p n beacons of 19 pseudo-random bytes each (worst case for POCKET+). */
static void feed_random(int n)
{
    uint8_t b[HT_BEACON_SIZE];
    for (int i = 0; i < n; i++) {
        for (unsigned k = 0; k < HT_BEACON_SIZE; k++) b[k] = rnd();
        feed(b);
    }
}

/** @brief Helper: copies the header of slot @p i out of the flash image (memcpy: the header is packed). */
static ht_slot_header slot_hdr(int i)
{
    ht_slot_header h;
    memcpy(&h, fake_obdh_flash(HT_BASE_ADDR + (uint32_t)i * HT_SLOT_SIZE), sizeof h);
    return h;
}

/** @brief Helper: pointer to the payload (after the 12-byte header) of slot @p i in the flash image. */
static const uint8_t *slot_payload(int i)
{
    return fake_obdh_flash(HT_BASE_ADDR + (uint32_t)i * HT_SLOT_SIZE) + HT_SLOT_HEADER_SIZE;
}

/** @brief Helper: the 20-byte stored form of a beacon (19 bytes + one zero pad byte) as the 5 big-endian words POCKET+ takes. */
static void to_words(const uint8_t b[HT_BEACON_SIZE], uint32_t w[HT_STORED_PACKET_SIZE / 4])
{
    uint8_t p[HT_STORED_PACKET_SIZE] = { 0 };
    memcpy(p, b, HT_BEACON_SIZE);
    for (unsigned k = 0; k < HT_STORED_PACKET_SIZE / 4; k++)
        w[k] = ((uint32_t)p[4 * k] << 24) | ((uint32_t)p[4 * k + 1] << 16) | ((uint32_t)p[4 * k + 2] << 8) | p[4 * k + 3];
}

/**
 * @brief Helper: checks every stored slot, from the reading pointer, has a header that obeys the documented format.
 * @return Sum of the beacon counts of the stored slots.
 */
static int assert_all_slots_valid(void)
{
    int total = 0;
    char msg[64];
    for (int k = 0; k < ht_queue.current_ht_count; k++) {
        int s = (ht_queue.reading_pointer + k) % (int)HT_NUM_SLOTS;
        ht_slot_header h = slot_hdr(s);
        snprintf(msg, sizeof msg, "slot %d", s);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(HT_FORMAT_POCKET_V3, h.format, msg);
        TEST_ASSERT_TRUE_MESSAGE(h.data_len >= HT_STORED_PACKET_SIZE, msg);
        TEST_ASSERT_TRUE_MESSAGE(h.data_len <= HT_SLOT_PAYLOAD_SIZE, msg);
        TEST_ASSERT_TRUE_MESSAGE(h.count >= 1u, msg);
        TEST_ASSERT_TRUE_MESSAGE(h.count <= HT_SLOT_MAX_BEACONS, msg);
        TEST_ASSERT_TRUE_MESSAGE(ht_slot_header_valid(&h), msg);
        total += h.count;
    }
    return total;
}

/**
 * @brief Runs before every test: erased flash and an empty HT queue.
 * @details Resets the fake RTOS/HAL and the flash image (all 0xFF), then
 * `ht_init()`, which on erased flash zeroes the queue pointers, `next_seq`
 * and the staging state (`ht_block_used`, `ht_block_count`,
 * `ht_block_compressing`); the flash log is reset again to drop its 64 header
 * reads. The record of fed beacons and the random generator are reset too.
 * compression.c needs no reset: the first beacon of every block calls
 * `startPocket()`.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    fake_obdh_reset();
    ht_init();
    fake_obdh_reset();
    n_fed = 0;
    lcg_state = 12345u;
}

/** @brief Runs after every test: checks that the RTOS fake is balanced. */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/* =========================================================================
 * 1. Real blocks and the slot format
 * ========================================================================= */

/**
 * @brief 300 realistic beacons give slots whose headers all obey the documented format, and no beacon is lost or counted twice.
 * @details Expected: every stored slot has format 0x03, 20 <= data_len <= 116,
 * 1 <= count <= 97, and (sum of stored counts) + (beacons still in the open
 * RAM block) == 300. ASan/UBSan also check the real compressor writes inside
 * its buffers. Matters because a header outside these bounds is dropped by
 * `ht_init()` after a reboot, i.e. history silently lost.
 */
void test_real_pocket_block_fits_slot(void)
{
    feed_realistic(300);
    TEST_ASSERT_TRUE_MESSAGE(ht_queue.current_ht_count >= 1, "no block was flushed");
    TEST_ASSERT_TRUE_MESSAGE(ht_queue.next_seq <= HT_NUM_SLOTS, "queue wrapped: this test assumes it doesn't");
    int stored = assert_all_slots_valid();
    TEST_ASSERT_EQUAL_INT(300, stored + ht_block_count);
}

/**
 * @brief With realistic beacons, real POCKET+ packs several beacons into every block.
 * @details Expected: every stored block has count >= 2 (otherwise compression
 * gives no benefit over raw storage). The counts are printed for the report.
 * Each block also holds more than `2 x POCKET_PLUS_UPDATE_RATE + 1` beacons
 * only if the data compresses well; that is printed, not asserted.
 */
void test_real_pocket_packs_several_beacons_per_block(void)
{
    feed_realistic(300);
    printf("realistic beacons per block:");
    for (int k = 0; k < ht_queue.current_ht_count; k++) {
        ht_slot_header h = slot_hdr(k);
        printf(" %u(%uB)", (unsigned)h.count, (unsigned)h.data_len);
        TEST_ASSERT_TRUE_MESSAGE(h.count >= 2u, "a realistic block holds a single beacon");
    }
    printf("\n");
}

/**
 * @brief Slot 0 starts with beacon 0 stored raw (19 bytes + one zero pad byte), and its header epoch is that beacon's epoch.
 * @details The ground decompressor must start each block from an
 * uncompressed reference, and orders blocks by the header epoch, so both must
 * be exact.
 */
void test_reference_beacon_is_stored_raw(void)
{
    feed_realistic(300);
    const uint8_t *p = slot_payload(0);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fed[0], p, HT_BEACON_SIZE);
    TEST_ASSERT_EQUAL_HEX8(0x00, p[HT_BEACON_SIZE]);
    TEST_ASSERT_EQUAL_UINT32(1700000000u, slot_hdr(0).epoch);
    TEST_ASSERT_EQUAL_UINT32(0, slot_hdr(0).seq);
}

/**
 * @brief Each slot holds exactly: its raw reference, then the POCKET+ packets of its other beacons back to back, with no framing.
 * @details For every stored slot, the test restarts the compressor from that
 * slot's reference beacon (`startPocket` with `POCKET_PLUS_UPDATE_RATE`,
 * protection level 1, as ht_handling does), compresses the next count - 1
 * fed beacons and checks that the concatenated output equals the slot payload
 * after the reference, and that data_len == 20 + the sum of the packet
 * lengths. This is the documented V3 format the ground decompressor walks;
 * with no decompressor in the repo it is the closest check to a round trip.
 * It also shows the compressor output depends only on the block's own
 * reference (each block can be decoded on its own).
 */
void test_slot_payload_is_reference_plus_compressed_packets(void)
{
    feed_realistic(300);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ht_queue.reading_pointer, "queue wrapped: this test assumes it doesn't");
    int first = 0;
    for (int s = 0; s < ht_queue.current_ht_count; s++) {
        ht_slot_header h = slot_hdr(s);
        const uint8_t *p = slot_payload(s);
        char msg[48];
        snprintf(msg, sizeof msg, "slot %d", s);

        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(fed[first], p, HT_BEACON_SIZE, msg);
        uint32_t w[HT_STORED_PACKET_SIZE / 4];
        to_words(fed[first], w);
        TEST_ASSERT_NOT_EQUAL(0, safeStartPocket(w, HT_STORED_PACKET_SIZE, POCKET_PLUS_UPDATE_RATE));

        unsigned used = HT_STORED_PACKET_SIZE;
        for (int j = 1; j < h.count; j++) {
            uint8_t c[HT_STORED_PACKET_SIZE * 4];
            to_words(fed[first + j], w);
            int_fast16_t n = compressPacket(c, w);
            TEST_ASSERT_TRUE_MESSAGE(n > 0, msg);
            TEST_ASSERT_TRUE_MESSAGE(used + (unsigned)n <= HT_SLOT_PAYLOAD_SIZE, msg);
            TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(c, p + used, (uint32_t)n, msg);
            used += (unsigned)n;
        }
        TEST_ASSERT_EQUAL_UINT_MESSAGE(used, h.data_len, msg);
        first += h.count;
    }
}

/**
 * @brief 200 identical beacons fill the first block to the physical maximum: 97 beacons in exactly 116 bytes.
 * @details An unchanged beacon compresses to one byte (the 3-bit packet
 * header, no deltas, no non-predictable bits), so the block is the 20-byte
 * reference + 96 one-byte packets = 116 = `HT_SLOT_PAYLOAD_SIZE`, and the
 * 98th beacon starts a new block. Matters because it is the edge of the "does
 * it still fit" check: one byte more would overflow the slot. The block also
 * spans several `POCKET_PLUS_UPDATE_RATE` periods (positive-mask updates
 * at packets 13, 25, ...), which must not add bytes when nothing changes.
 */
void test_identical_beacons_compress_to_minimum(void)
{
    uint8_t b[HT_BEACON_SIZE];
    make_beacon(0, b);
    for (int i = 0; i < 200; i++) feed(b);

    TEST_ASSERT_TRUE(ht_queue.current_ht_count >= 2);
    ht_slot_header h = slot_hdr(0);
    TEST_ASSERT_EQUAL_UINT8(HT_SLOT_MAX_BEACONS, h.count);
    TEST_ASSERT_EQUAL_UINT16(HT_SLOT_PAYLOAD_SIZE, h.data_len);
    TEST_ASSERT_EQUAL_INT(200, assert_all_slots_valid() + ht_block_count);
}

/**
 * @brief 100 random beacons (worst case: POCKET+ expands them) never produce a slot that overflows or breaks the header rules.
 * @details Random data makes almost every bit unpredictable, so a compressed
 * packet can be bigger than the raw beacon, or the compressor can give up
 * (-1). ht_handling must then close the block and start a new one (blocks of
 * 2 or 3 beacons). Expected: the queue has not wrapped yet (so nothing was
 * evicted), every stored slot is valid, and nothing is lost (sum of counts +
 * open block == 100). Wrapping with random data is covered by
 * test_queue_wraps_with_real_blocks.
 */
void test_random_beacons_never_overflow_slot(void)
{
    feed_random(100);
    TEST_ASSERT_TRUE_MESSAGE(ht_queue.next_seq <= HT_NUM_SLOTS, "queue wrapped: lower the feed so nothing is evicted");
    int stored = assert_all_slots_valid();
    TEST_ASSERT_EQUAL_INT(100, stored + ht_block_count);
}

/* =========================================================================
 * 2. Recovery and wrap with real blocks
 * ========================================================================= */

/**
 * @brief After real blocks were stored, `ht_init()` (what runs after a reboot) rebuilds exactly the same queue state.
 * @details Feed 300 realistic beacons, save `ht_queue`, call `ht_init()`.
 * Expected: same count, reading pointer, writing pointer and next sequence
 * number. With the mock compressor every block had data_len 116; real blocks
 * have varying data_len, which `ht_slot_header_valid()` must accept.
 */
void test_ht_init_recovers_queue_after_real_blocks(void)
{
    feed_realistic(300);
    int count = ht_queue.current_ht_count, rp = ht_queue.reading_pointer, wp = ht_queue.writing_pointer;
    uint32_t seq = ht_queue.next_seq;

    ht_init();
    TEST_ASSERT_EQUAL_INT(count, ht_queue.current_ht_count);
    TEST_ASSERT_EQUAL_INT(rp, ht_queue.reading_pointer);
    TEST_ASSERT_EQUAL_INT(wp, ht_queue.writing_pointer);
    TEST_ASSERT_EQUAL_UINT32(seq, ht_queue.next_seq);
    TEST_ASSERT_EQUAL_INT(0, ht_block_count);
}

/**
 * @brief When more than 64 real blocks have been flushed, the queue holds 64 slots whose sequence numbers increase by 1 from the reading pointer, and `ht_init()` agrees.
 * @details Random beacons give small blocks, so 600 of them wrap the 8 KB
 * region several times (each page is erased once per lap). Expected: count
 * 64, the sequence numbers are consecutive starting at the reading pointer,
 * and the queue rebuilt by `ht_init()` is identical. Matters because the
 * ground orders downloaded history by these sequence numbers.
 */
void test_queue_wraps_with_real_blocks(void)
{
    feed_random(600);
    TEST_ASSERT_TRUE_MESSAGE(ht_queue.next_seq > HT_NUM_SLOTS, "feed did not wrap the queue");
    /* A page erase may drop up to 16 blocks, so the queue is between 48 and 64 full */
    TEST_ASSERT_TRUE(ht_queue.current_ht_count > (int)(HT_NUM_SLOTS - HT_SLOTS_PER_PAGE));
    assert_all_slots_valid();

    uint32_t prev = slot_hdr(ht_queue.reading_pointer).seq;
    for (int k = 1; k < ht_queue.current_ht_count; k++) {
        uint32_t s = slot_hdr((ht_queue.reading_pointer + k) % (int)HT_NUM_SLOTS).seq;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(prev + 1u, s, "sequence numbers not consecutive");
        prev = s;
    }
    TEST_ASSERT_EQUAL_UINT32(ht_queue.next_seq - 1u, prev);

    int count = ht_queue.current_ht_count, rp = ht_queue.reading_pointer, wp = ht_queue.writing_pointer;
    ht_init();
    TEST_ASSERT_EQUAL_INT(count, ht_queue.current_ht_count);
    TEST_ASSERT_EQUAL_INT(rp, ht_queue.reading_pointer);
    TEST_ASSERT_EQUAL_INT(wp, ht_queue.writing_pointer);
}

/* =========================================================================
 * 3. POCKET_PLUS_UPDATE_RATE boundary (compression.c API)
 * ========================================================================= */

/**
 * @brief With `POCKET_PLUS_UPDATE_RATE` = 12, a bit that changed once becomes predictable again exactly at packet 25 (the second positive-mask update), not earlier.
 * @details Start POCKET+ like ht_handling does (update rate 12, protection
 * level 1). Packet 1 flips one bit relative to the reference; packets 2..30
 * repeat packet 1. Expected:
 * - packet 1: header has the negative-update flag (bit 0 of the 3-bit
 *   header); the mask holds the flipped bit; 1 non-predictable bit;
 * - packets 2..24: the mask still holds the bit (the positive update at
 *   packet 13 sees the change of packet 1 inside its window, so keeps it);
 *   each packet carries that 1 non-predictable bit;
 * - packet 25: header has the positive-update flag (bit 1); the mask is
 *   empty and there are 0 non-predictable bits;
 * - packets 26..30: one byte each (nothing to send).
 * Matters because the ground decompressor must apply positive updates at the
 * same packet numbers: `POCKET_PLUS_UPDATE_RATE` has to match on both sides,
 * and this test pins the on-board behaviour for that rate.
 */
void test_positive_mask_update_follows_update_rate(void)
{
    TEST_ASSERT_EQUAL_INT(12, POCKET_PLUS_UPDATE_RATE);
    uint32_t ref[5] = { 0x01654A3Bu, 0x00013F14u, 0x00210000u, 0x05000100u, 0x00000000u };
    uint32_t pkt[5], cur[5], m[5];
    uint8_t c[HT_STORED_PACKET_SIZE * 4];
    const uint32_t flip = 0x01000000u;   /* one bit of word 1 */
    const int boundary = 2 * POCKET_PLUS_UPDATE_RATE + 1;

    TEST_ASSERT_EQUAL_UINT(HT_STORED_PACKET_SIZE, safeStartPocket(ref, HT_STORED_PACKET_SIZE, POCKET_PLUS_UPDATE_RATE));
    memcpy(pkt, ref, sizeof pkt);
    pkt[1] ^= flip;

    for (int k = 1; k <= 30; k++) {
        char msg[32];
        snprintf(msg, sizeof msg, "packet %d", k);
        memcpy(cur, pkt, sizeof cur);           /* compressPacket must not depend on the caller keeping the input */
        int_fast16_t n = compressPacket(c, cur);
        TEST_ASSERT_TRUE_MESSAGE(n >= 1, msg);
        uint8_t hdr = (uint8_t)(c[0] >> 5);
        getMask(m);

        if (k == 1) {
            TEST_ASSERT_TRUE_MESSAGE(hdr & 1u, "packet 1 must carry a negative mask update");
        }
        if (k < boundary) {
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(flip, m[1], msg);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(1, getNumberOfNpBits(), msg);
            TEST_ASSERT_FALSE_MESSAGE(hdr & 2u, msg);
        } else if (k == boundary) {
            TEST_ASSERT_TRUE_MESSAGE(hdr & 2u, "packet 25 must carry the positive mask update");
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, m[1], msg);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, getNumberOfNpBits(), msg);
        } else {
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)n, msg);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, m[1], msg);
        }
        for (int w = 0; w < 5; w++) if (w != 1) TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, m[w], msg);
    }
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
    POCAT_RUN_TEST(test_real_pocket_block_fits_slot);
    POCAT_RUN_TEST(test_real_pocket_packs_several_beacons_per_block);
    POCAT_RUN_TEST(test_reference_beacon_is_stored_raw);
    POCAT_RUN_TEST(test_slot_payload_is_reference_plus_compressed_packets);
    POCAT_RUN_TEST(test_identical_beacons_compress_to_minimum);
    POCAT_RUN_TEST(test_random_beacons_never_overflow_slot);
    POCAT_RUN_TEST(test_ht_init_recovers_queue_after_real_blocks);
    POCAT_RUN_TEST(test_queue_wraps_with_real_blocks);
    POCAT_RUN_TEST(test_positive_mask_update_follows_update_rate);
    return POCAT_END();
}
