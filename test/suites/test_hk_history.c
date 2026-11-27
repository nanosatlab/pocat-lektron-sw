/**
 * @file test_hk_history.c
 * @brief Host tests for the HK history copy: the stored blocks from FROM_SEQ on, oldest first, as the downlink sends them.
 * @date 2026-11-21
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/hk_history.c: hk_history_request(), its layout
 *   (format byte, then SEQ, LEN and the block for each record), where it
 *   stops, and its block time.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - hk_history.c is `#include`d below.
 * - ht_read_request() is faked below over a list of stored blocks, as the
 *   real HT store answers: the oldest block numbered from_seq or later, or
 *   HAL_ERROR if there is none. Each call is logged with its from_seq and
 *   block time; a test can make a given call fail with HAL_BUSY or
 *   HAL_TIMEOUT, or make each call take some ticks.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_hk_history
 * docker run --rm -v "${PWD}:/app" pocat-tests test_hk_history -f deadline
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "pocat_test_main.h"

#include "hk_history.c"

#include <string.h>

#define STORE_MAX  128u   /**< Most blocks the fake store holds. */
#define READS_MAX  160u   /**< Most reads logged. */
#define FULL       HT_BLOCK_MAX   /**< Length of a full block. */

/* ---- Fake of the HT store, as the OBDH task answers it ---- */

typedef struct {
    uint32_t seq;
    uint32_t len;
    uint8_t  data[HT_BLOCK_MAX];
} stored_t;

static stored_t store[STORE_MAX];   /**< Stored blocks, by increasing number. */
static uint32_t stored;

static uint32_t   reads;                   /**< Calls of ht_read_request(). */
static uint32_t   read_from[READS_MAX];    /**< from_seq of each call. */
static TickType_t read_ticks[READS_MAX];   /**< Block time of each call. */
static uint32_t   failing_read;            /**< Index of the call that fails; READS_MAX for none. */
static HAL_StatusTypeDef failure;          /**< What that call returns. */
static TickType_t ticks_per_read;          /**< Ticks each call takes. */

/** @brief The bytes of stored block seq: every block can be told apart. */
static uint8_t pattern(uint32_t seq, uint32_t i)
{
    return (uint8_t)(seq * 13u + i + 1u);
}

/** @brief Stores a block numbered seq, of len bytes. Blocks must be added in increasing order. */
static void add_block(uint32_t seq, uint32_t len)
{
    TEST_ASSERT_LESS_THAN_UINT32(STORE_MAX, stored);
    stored_t *b = &store[stored++];
    b->seq = seq;
    b->len = len;
    for (uint32_t i = 0; i < len; i++) b->data[i] = pattern(seq, i);
}

/** @brief Stores count blocks numbered first, first + 1, ... of len bytes. */
static void add_blocks(uint32_t first, uint32_t count, uint32_t len)
{
    for (uint32_t n = 0; n < count; n++) add_block(first + n, len);
}

HAL_StatusTypeDef ht_read_request(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq, TickType_t ticks_to_wait)
{
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(READS_MAX, reads, "too many reads");
    TEST_ASSERT_TRUE((block != NULL) && (len != NULL) && (seq != NULL));
    uint32_t n = reads++;
    read_from[n] = from_seq;
    read_ticks[n] = ticks_to_wait;
    fake_rtos_advance_ticks(ticks_per_read);

    if (n == failing_read) {
        return failure;
    }
    for (uint32_t i = 0; i < stored; i++) {
        if (store[i].seq >= from_seq) {
            memcpy(block, store[i].data, store[i].len);
            *len = store[i].len;
            *seq = store[i].seq;
            return HAL_OK;
        }
    }
    return HAL_ERROR;
}

/* ---- Helpers ---- */

static uint8_t  buf[HK_HISTORY_MAX + 16u];   /**< The copy, with room after it to see that nothing is written there. */
static uint32_t copy_len;

/** @brief Returns the stored block numbered seq; fails the test if there is none. */
static const stored_t *stored_block(uint32_t seq)
{
    for (uint32_t i = 0; i < stored; i++) {
        if (store[i].seq == seq) return &store[i];
    }
    TEST_FAIL_MESSAGE("a record names a block that is not stored");
    return NULL;
}

/**
 * @brief Walks the copy: checks the format byte and each record against the store.
 * @param seqs Receives the numbers of the records, in order.
 * @return The number of records.
 */
static uint32_t walk_copy(uint32_t seqs[])
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(HK_HISTORY_MAX, copy_len);
    TEST_ASSERT_EQUAL_HEX8(HT_FORMAT, buf[0]);
    uint32_t at = 1u;
    uint32_t n = 0u;
    while (at < copy_len) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(copy_len, at + HK_HISTORY_RECORD_HEADER, "a record header is cut");
        uint32_t seq = ((uint32_t)buf[at] << 24) | ((uint32_t)buf[at + 1u] << 16) |
                       ((uint32_t)buf[at + 2u] << 8) | (uint32_t)buf[at + 3u];
        uint32_t len = buf[at + 4u];
        const stored_t *b = stored_block(seq);
        TEST_ASSERT_EQUAL_UINT32(b->len, len);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(copy_len, at + HK_HISTORY_RECORD_HEADER + len, "a block is cut");
        TEST_ASSERT_EQUAL_HEX8_ARRAY(b->data, &buf[at + HK_HISTORY_RECORD_HEADER], len);
        seqs[n++] = seq;
        at += HK_HISTORY_RECORD_HEADER + len;
    }
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, &buf[HK_HISTORY_MAX], 16u);
    return n;
}

/* ---- Setup ---- */

void setUp(void)
{
    fake_rtos_reset();
    stored = 0u;
    reads = 0u;
    failing_read = READS_MAX;
    failure = HAL_OK;
    ticks_per_read = 0u;
    memset(buf, 0xEE, sizeof(buf));
    copy_len = 0xC0FFEEu;
}

void tearDown(void) {}

/* =========================================================================
 * 1. The layout
 * ========================================================================= */

/** @brief With nothing stored from from_seq on, the copy gives HAL_ERROR and writes nothing: the ground gets NACK_NO_DATA. */
void test_nothing_stored_gives_error_and_writes_nothing(void)
{
    add_block(3, 20);
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, hk_history_request(4, buf, &copy_len, 100));

    TEST_ASSERT_EQUAL_UINT32(0xC0FFEEu, copy_len);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(1, reads);
    TEST_ASSERT_EQUAL_UINT32(4, read_from[0]);
}

/** @brief One block: the format byte, SEQ big-endian, LEN, then the block as stored; then one more read finds nothing. */
void test_one_block_gives_the_format_and_one_record(void)
{
    add_block(0x0A0B0C0Du, 20);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(0, buf, &copy_len, 100));

    TEST_ASSERT_EQUAL_UINT32(1u + HK_HISTORY_RECORD_HEADER + 20u, copy_len);
    const uint8_t head[] = { HT_FORMAT, 0x0A, 0x0B, 0x0C, 0x0D, 20 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(head, buf, sizeof(head));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(store[0].data, &buf[sizeof(head)], 20);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, &buf[copy_len], sizeof(buf) - copy_len);

    TEST_ASSERT_EQUAL_UINT32(2, reads);
    TEST_ASSERT_EQUAL_UINT32(0, read_from[0]);
    TEST_ASSERT_EQUAL_UINT32(0x0A0B0C0Eu, read_from[1]);
}

/** @brief The blocks come oldest first, from from_seq on; each read starts after the number last read. */
void test_blocks_come_oldest_first_from_from_seq(void)
{
    add_blocks(10, 5, 30);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(12, buf, &copy_len, 100));

    uint32_t seqs[STORE_MAX];
    TEST_ASSERT_EQUAL_UINT32(3, walk_copy(seqs));
    const uint32_t expected[] = { 12, 13, 14 };
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, seqs, 3);
    const uint32_t from[] = { 12, 13, 14, 15 };
    TEST_ASSERT_EQUAL_UINT32(4, reads);
    TEST_ASSERT_EQUAL_UINT32_ARRAY(from, read_from, 4);
}

/** @brief Numbers missing from the store are passed over: the records show the gap, and a from_seq older than every block starts at the oldest. */
void test_missing_numbers_are_passed_over(void)
{
    add_block(3, 25);
    add_block(5, FULL);
    add_block(9, 1);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(0, buf, &copy_len, 100));

    uint32_t seqs[STORE_MAX];
    TEST_ASSERT_EQUAL_UINT32(3, walk_copy(seqs));
    const uint32_t expected[] = { 3, 5, 9 };
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, seqs, 3);
    const uint32_t from[] = { 0, 4, 6, 10 };
    TEST_ASSERT_EQUAL_UINT32_ARRAY(from, read_from, 4);
}

/* =========================================================================
 * 2. Where a copy stops
 * ========================================================================= */

/**
 * @brief Full blocks: the copy reads a block only while a full one still fits, and never passes HK_HISTORY_MAX.
 * @details 1 + 17 × 117 = 1,990 bytes; an 18th full record would end at 2,107.
 * The copy stops without reading the 18th block.
 */
void test_full_blocks_stop_before_the_copy_would_overflow(void)
{
    add_blocks(100, 40, FULL);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(0, buf, &copy_len, 100));

    uint32_t seqs[STORE_MAX];
    TEST_ASSERT_EQUAL_UINT32(17, walk_copy(seqs));
    TEST_ASSERT_EQUAL_UINT32(1990, copy_len);
    TEST_ASSERT_EQUAL_UINT32(17, reads);
}

/**
 * @brief Short blocks: the copy goes on while a full block still fits after them.
 * @details Records of 25 bytes. After 77 of them the copy holds 1,926 bytes,
 * and a full block would still fit (2,043): a 78th block is read. After it, at
 * 1,951 bytes, a full block would not (2,068): the copy stops.
 */
void test_short_blocks_fill_the_copy_until_a_full_block_might_not_fit(void)
{
    add_blocks(0, 100, 20);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(0, buf, &copy_len, 100));

    uint32_t seqs[STORE_MAX];
    TEST_ASSERT_EQUAL_UINT32(78, walk_copy(seqs));
    TEST_ASSERT_EQUAL_UINT32(1951, copy_len);
    TEST_ASSERT_EQUAL_UINT32(78, reads);
}

/**
 * @brief The copy uses all its room, and not a byte more.
 * @details Sixteen full records and one of 58 bytes end at 1,931: exactly a
 * full record is left, so an 18th block is read and the copy ends at 2,048.
 * With one of 59 bytes instead, the copy ends at 1,932, one byte short of a
 * full record: it stops there, as the next block might be full (and is).
 */
void test_the_copy_uses_all_its_room_and_no_more(void)
{
    add_blocks(0, 16, FULL);
    add_block(16, 58u - HK_HISTORY_RECORD_HEADER);
    add_blocks(17, 5, FULL);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(0, buf, &copy_len, 100));
    uint32_t seqs[STORE_MAX];
    TEST_ASSERT_EQUAL_UINT32(18, walk_copy(seqs));
    TEST_ASSERT_EQUAL_UINT32(HK_HISTORY_MAX, copy_len);

    setUp();
    add_blocks(0, 16, FULL);
    add_block(16, 59u - HK_HISTORY_RECORD_HEADER);
    add_blocks(17, 5, FULL);
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(0, buf, &copy_len, 100));
    TEST_ASSERT_EQUAL_UINT32(17, walk_copy(seqs));
    TEST_ASSERT_EQUAL_UINT32(1932, copy_len);
    TEST_ASSERT_EQUAL_UINT32(17, reads);
}

/** @brief Asked again from the last number + 1, the next copies go on where the last one stopped: every block comes once, in order. */
void test_the_next_copy_goes_on_after_the_last_record(void)
{
    add_blocks(100, 40, FULL);
    uint32_t all[STORE_MAX];
    uint32_t n = 0u;
    uint32_t from = 0u;
    uint32_t copies = 0u;

    while (hk_history_request(from, buf, &copy_len, 100) == HAL_OK) {
        uint32_t seqs[STORE_MAX];
        uint32_t got = walk_copy(seqs);
        memcpy(&all[n], seqs, got * sizeof(seqs[0]));
        n += got;
        from = seqs[got - 1u] + 1u;
        copies++;
        memset(buf, 0xEE, sizeof(buf));
    }

    TEST_ASSERT_EQUAL_UINT32(3, copies);   /* 17 + 17 + 6 */
    TEST_ASSERT_EQUAL_UINT32(40, n);
    for (uint32_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_UINT32(100u + i, all[i]);
}

/* =========================================================================
 * 3. OBDH busy, and the block time
 * ========================================================================= */

/** @brief If the first read gets no answer, the copy returns its status and writes nothing: the ground gets NACK_BUSY and asks again. */
void test_a_first_read_without_answer_gives_its_status(void)
{
    const HAL_StatusTypeDef statuses[] = { HAL_BUSY, HAL_TIMEOUT };
    for (uint32_t i = 0; i < 2u; i++) {
        setUp();
        add_blocks(1, 5, 30);
        failing_read = 0u;
        failure = statuses[i];
        TEST_ASSERT_EQUAL_INT(statuses[i], hk_history_request(0, buf, &copy_len, 100));
        TEST_ASSERT_EQUAL_UINT32(0xC0FFEEu, copy_len);
        TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof(buf));
        TEST_ASSERT_EQUAL_UINT32(1, reads);
    }
}

/** @brief A later read without answer ends the copy with the blocks already read; the ground goes on from the last of them. */
void test_a_later_read_without_answer_keeps_what_was_read(void)
{
    add_blocks(1, 5, 30);
    failing_read = 2u;
    failure = HAL_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(0, buf, &copy_len, 100));

    uint32_t seqs[STORE_MAX];
    TEST_ASSERT_EQUAL_UINT32(2, walk_copy(seqs));
    TEST_ASSERT_EQUAL_UINT32(1, seqs[0]);
    TEST_ASSERT_EQUAL_UINT32(2, seqs[1]);
    TEST_ASSERT_EQUAL_UINT32(3, reads);
}

/**
 * @brief The block time counts once, for all the reads: each read gets what is left, and none starts once it has run out.
 * @details Each read takes 30 ticks and the block time is 100: the reads get
 * 100, 70, 40 and 10 ticks. After the fourth, 120 ticks have passed: the copy
 * stops with four blocks, though more are stored and fit.
 */
void test_one_deadline_for_all_the_reads(void)
{
    add_blocks(1, 10, 30);
    ticks_per_read = 30u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, hk_history_request(0, buf, &copy_len, 100));

    uint32_t seqs[STORE_MAX];
    TEST_ASSERT_EQUAL_UINT32(4, walk_copy(seqs));
    const TickType_t ticks[] = { 100, 70, 40, 10 };
    TEST_ASSERT_EQUAL_UINT32(4, reads);
    TEST_ASSERT_EQUAL_UINT32_ARRAY(ticks, read_ticks, 4);
    TEST_ASSERT_EQUAL_UINT32(120, xTaskGetTickCount());
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();

    // 1. The layout
    POCAT_RUN_TEST(test_nothing_stored_gives_error_and_writes_nothing);
    POCAT_RUN_TEST(test_one_block_gives_the_format_and_one_record);
    POCAT_RUN_TEST(test_blocks_come_oldest_first_from_from_seq);
    POCAT_RUN_TEST(test_missing_numbers_are_passed_over);

    // 2. Where a copy stops
    POCAT_RUN_TEST(test_full_blocks_stop_before_the_copy_would_overflow);
    POCAT_RUN_TEST(test_short_blocks_fill_the_copy_until_a_full_block_might_not_fit);
    POCAT_RUN_TEST(test_the_copy_uses_all_its_room_and_no_more);
    POCAT_RUN_TEST(test_the_next_copy_goes_on_after_the_last_record);

    // 3. OBDH busy, and the block time
    POCAT_RUN_TEST(test_a_first_read_without_answer_gives_its_status);
    POCAT_RUN_TEST(test_a_later_read_without_answer_keeps_what_was_read);
    POCAT_RUN_TEST(test_one_deadline_for_all_the_reads);

    return POCAT_END();
}
