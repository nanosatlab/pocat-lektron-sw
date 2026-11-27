/**
 * @file test_ring.c
 * @brief Host tests for the ring store: blocks in fixed slots, one erase per page per lap, safe against power cuts.
 * @date 2026-11-21
 *
 * @details
 * **What is under test**
 * - src/subsystems/obdh/ring.c: ring_load() (the scan each time OBDH starts),
 *   ring_append(), ring_read() and ring_read_oldest().
 * - src/utils/crc32.c, which the header checks and the trailers use.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `mocks/fake_flash_driver.c` replaces the flash driver with a RAM copy of
 *   the data area. It programs and erases like the STM32L4 (only 1 to 0, 8
 *   bytes at a time, whole 2 KB pages). It can cut the power at any program or
 *   erase step, inject ECC errors, program a doubleword wrong without an ECC
 *   error, and make a page fail to erase.
 * - A "reboot" is fake_flashdrv_power_on() followed by ring_load(), which
 *   rebuilds the ring's position from flash alone.
 *
 * **Ring under test**: the HT geometry, 128-byte slots in the 64 KB HT region:
 * 512 slots, 16 per page, 32 pages, up to 112 bytes per block. One test uses
 * 1 KB and 2 KB slots in the payload region. Block n is made from seed n
 * (length_for(), fill()), so most tests check block n under number n.
 *
 * **Findings covered**: F10 / KI-11 (a page that fails to erase stopped HT),
 * F14 (a slot cut short looked valid), KI-10 (order lost across the wrap of
 * the sequence number).
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_ring
 * docker run --rm -v "${PWD}:/app" pocat-tests test_ring -f power_cut
 * @endcode
 */
#include "unity.h"
#include "fake_flash_driver.h"
#include "ring.h"
#include "crc32.h"
#include "pocat_test_main.h"
#include <string.h>

#define SLOT      128u
#define DATA_MAX  RING_DATA_MAX(SLOT)                      /* 112 */
#define SLOTS     (FLASH_HT_SIZE / SLOT)                   /* 512 */
#define PER_PAGE  (FLASH_MAP_PAGE_SIZE / SLOT)             /* 16 */
#define PAGES     (FLASH_HT_SIZE / FLASH_MAP_PAGE_SIZE)    /* 32 */

_Static_assert(RING_SLOT_SIZE_VALID(SLOT), "the ring under test must be valid");
_Static_assert(RING_SLOT_SIZE_VALID(1024u) && RING_SLOT_SIZE_VALID(2048u), "half-page and whole-page slots must be valid");

static const ring_config_t HT_LIKE = { FLASH_REGION_HT, SLOT };
static ring_t ring;

void setUp(void)
{
    fake_flashdrv_reset();
    memset(&ring, 0, sizeof(ring));
    ring.config = HT_LIKE;
    ring_load(&ring);
}

void tearDown(void) {}

/* ---- Helpers ---- */

/** @brief Length of block seed: 1 to 112 bytes, different from one block to the next. */
static uint32_t length_for(uint32_t seed)
{
    return 1u + (seed * 37u) % DATA_MAX;
}

static void fill(uint8_t *buf, uint32_t len, uint32_t seed)
{
    for (uint32_t i = 0; i < len; i++) buf[i] = (uint8_t)(seed * 31u + i * 7u + 1u);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint8_t *slot_bytes(uint32_t slot)
{
    return fake_flashdrv_bytes(FLASH_REGION_HT, slot * SLOT);
}

/** @brief The sequence number in a slot's header. */
static uint32_t slot_seq(uint32_t slot)
{
    return le32(slot_bytes(slot) + 4);
}

static uint32_t page_erases(uint32_t page)
{
    return fake_flashdrv_erases(FLASH_REGION_HT, page * FLASH_MAP_PAGE_SIZE);
}

/** @brief Power back on and find the ring's position from flash, as when OBDH starts. */
static void reboot(void)
{
    fake_flashdrv_power_on();
    ring_load(&ring);
}

/** @brief Appends block seed and checks that it is stored. */
static void append(uint32_t seed)
{
    uint8_t data[DATA_MAX];
    uint32_t len = length_for(seed);
    fill(data, len, seed);
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_OK, ring_append(&ring, data, len), "an append failed");
}

/** @brief Appends blocks first, first + 1, ... (count blocks). */
static void append_blocks(uint32_t first, uint32_t count)
{
    for (uint32_t k = 0; k < count; k++) append(first + k);
}

/** @brief Checks that reading from number from_seq gives block seed under number seq. */
static void assert_read(uint32_t from_seq, uint32_t seq, uint32_t seed)
{
    uint8_t got[DATA_MAX], expected[DATA_MAX];
    uint32_t len = 0, got_seq = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_OK, ring_read(&ring, from_seq, got, &len, &got_seq), "no block read");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(seq, got_seq, "wrong block read");
    TEST_ASSERT_EQUAL_UINT32(length_for(seed), len);
    fill(expected, len, seed);
    TEST_ASSERT_EQUAL_MEMORY(expected, got, len);
}

/** @brief The check of a header (ring.h): the low 16 bits of the CRC-32 of its length and number, little-endian. */
static uint16_t check_of(uint32_t len, uint32_t seq)
{
    const uint8_t bytes[6] = { (uint8_t)len, (uint8_t)(len >> 8),
                               (uint8_t)seq, (uint8_t)(seq >> 8), (uint8_t)(seq >> 16), (uint8_t)(seq >> 24) };
    return (uint16_t)crc32_finish(crc32_update(CRC32_INIT, bytes, sizeof(bytes)));
}

/** @brief Writes a header straight into flash, with the check it should carry. */
static void put_header(uint32_t slot, uint32_t len, uint32_t seq)
{
    uint8_t *s = slot_bytes(slot);
    uint16_t check = check_of(len, seq);
    s[0] = (uint8_t)check;
    s[1] = (uint8_t)(check >> 8);
    s[2] = (uint8_t)len;
    s[3] = (uint8_t)(len >> 8);
    put_le32(s + 4, seq);
}

/** @brief Writes a valid block straight into flash, as an earlier lap would have left it. */
static void put_block(uint32_t slot, uint32_t seq, uint32_t seed)
{
    uint8_t *s = slot_bytes(slot);
    uint32_t len = length_for(seed);
    put_header(slot, len, seq);
    fill(s + 8, len, seed);
    uint32_t crc = crc32_finish(crc32_update(crc32_update(CRC32_INIT, s, 8), s + 8, len));
    put_le32(s + SLOT - 8, crc);
    put_le32(s + SLOT - 4, ~crc);
}

/* ---- 1. Layout and basics ---- */

/** @brief Header, data, erased gap and trailer sit where ring.h says. */
void test_slot_layout_matches_the_spec(void)
{
    const uint8_t data[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    TEST_ASSERT_EQUAL_INT(HAL_OK, ring_append(&ring, data, sizeof(data)));

    const uint8_t *s = slot_bytes(0);
    /* check 0x82C5: the low 16 bits of the CRC-32 of 0A 00 00 00 00 00 (length 10, number 0) */
    const uint8_t header[8] = { 0xC5, 0x82, 10, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(header, s, 8);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, s + 8, sizeof(data));
    for (uint32_t i = 8 + sizeof(data); i < SLOT - 8; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, s[i], "the slot between data and trailer must stay erased");
    }
    uint32_t crc = crc32_finish(crc32_update(crc32_update(CRC32_INIT, header, 8), data, sizeof(data)));
    TEST_ASSERT_EQUAL_HEX32(crc, le32(s + SLOT - 8));
    TEST_ASSERT_EQUAL_HEX32(~crc, le32(s + SLOT - 4));
}

/** @brief An erased region holds no block; the first one goes to slot 0 with number 0. */
void test_blank_region_holds_no_block(void)
{
    uint8_t data[DATA_MAX];
    uint32_t len, seq;

    TEST_ASSERT_EQUAL_UINT32(SLOTS, ring.slots);
    TEST_ASSERT_EQUAL_UINT32(0, ring.write);
    TEST_ASSERT_EQUAL_UINT32(0, ring.next_seq);
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ring_read_oldest(&ring, data, &len, &seq));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ring_read(&ring, 0, data, &len, &seq));
}

/** @brief Each block reads back under its number; nothing is found from the next number on. */
void test_blocks_read_back_by_sequence_number(void)
{
    append_blocks(0, 20);
    for (uint32_t seq = 0; seq < 20; seq++) assert_read(seq, seq, seq);

    uint8_t data[DATA_MAX];
    uint32_t len, seq;
    TEST_ASSERT_EQUAL_INT(HAL_OK, ring_read_oldest(&ring, data, &len, &seq));
    TEST_ASSERT_EQUAL_UINT32(0, seq);
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_ERROR, ring_read(&ring, 20, data, &len, &seq), "the next number has no block yet");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_ERROR, ring_read(&ring, 25, data, &len, &seq), "a later number has no block");
}

/** @brief Blocks of 0 bytes up to a full slot, with and without a partial last doubleword. */
void test_lengths_from_empty_to_a_full_slot(void)
{
    const uint32_t lengths[] = { 0, 1, 7, 8, 9, 15, 16, DATA_MAX - 1, DATA_MAX };
    const uint32_t count = sizeof(lengths) / sizeof(lengths[0]);
    uint8_t data[DATA_MAX], got[DATA_MAX];

    for (uint32_t i = 0; i < count; i++) {
        fill(data, lengths[i], 100 + i);
        TEST_ASSERT_EQUAL_INT(HAL_OK, ring_append(&ring, data, lengths[i]));
    }
    reboot();
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len = 0xFFFF, seq = 0xFFFF;
        TEST_ASSERT_EQUAL_INT(HAL_OK, ring_read(&ring, i, got, &len, &seq));
        TEST_ASSERT_EQUAL_UINT32(i, seq);
        TEST_ASSERT_EQUAL_UINT32(lengths[i], len);
        if (len > 0) {
            fill(data, len, 100 + i);
            TEST_ASSERT_EQUAL_MEMORY(data, got, len);
        }
    }
}

/** @brief A block longer than a slot holds is refused and nothing is written. */
void test_a_block_too_long_for_a_slot_is_refused(void)
{
    uint8_t data[DATA_MAX + 1];
    memset(data, 0x5A, sizeof(data));

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ring_append(&ring, data, DATA_MAX + 1));
    TEST_ASSERT_EQUAL_UINT32(0, fake_flashdrv_programs());
    TEST_ASSERT_EQUAL_UINT32(0, page_erases(0));
    TEST_ASSERT_EQUAL_UINT32(0, ring.next_seq);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ring.stats.dropped, "a refused block is not a dropped one");
}

/** @brief After a reload the next block goes after the newest one, with the next number. */
void test_a_reload_continues_after_the_newest_block(void)
{
    append_blocks(0, 20);
    reboot();
    TEST_ASSERT_EQUAL_UINT32(20, ring.next_seq);
    TEST_ASSERT_EQUAL_UINT32(20, ring.write);

    append(20);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(20, slot_seq(20), "block 20 is not in slot 20");
    for (uint32_t seq = 0; seq <= 20; seq++) assert_read(seq, seq, seq);
}

/** @brief A zeroed slot counts as blank, as an erased one does: its header fails the check. */
void test_a_zeroed_slot_counts_as_blank(void)
{
    memset(slot_bytes(0), 0x00, SLOT);
    reboot();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ring.next_seq, "a zeroed header was taken for block 0");
    TEST_ASSERT_EQUAL_UINT32(0, ring.write);
}

/** @brief A header whose length does not fit the slot counts as blank, even with the right check, so no read can overrun the caller's buffer. */
void test_a_length_that_does_not_fit_counts_as_blank(void)
{
    put_header(0, DATA_MAX + 1u, 7);
    put_block(1, 3, 3);
    reboot();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, ring.next_seq, "the header of slot 0 was taken for a block");
    TEST_ASSERT_EQUAL_UINT32(2, ring.write);
    assert_read(0, 3, 3);
}

/** @brief Fills two pages of a ring of slot_size-byte slots in the payload region and starts a third, then reads every block back. */
static void check_slot_size(uint32_t slot_size)
{
    static uint8_t data[2048], got[2048];
    ring_t big = { .config = { FLASH_REGION_PAYLOAD, slot_size } };
    uint32_t max = RING_DATA_MAX(slot_size);
    uint32_t per_page = FLASH_MAP_PAGE_SIZE / slot_size;
    uint32_t blocks = 2u * per_page + 1u;   /* fills two pages and starts a third */

    fake_flashdrv_reset();
    ring_load(&big);
    TEST_ASSERT_EQUAL_UINT32(FLASH_PAYLOAD_SIZE / slot_size, big.slots);
    for (uint32_t b = 0; b < blocks; b++) {
        fill(data, max, b);
        TEST_ASSERT_EQUAL_INT(HAL_OK, ring_append(&big, data, max));
    }

    ring_load(&big);
    TEST_ASSERT_EQUAL_UINT32(blocks, big.next_seq);
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t len = 0, seq = 0;
        TEST_ASSERT_EQUAL_INT(HAL_OK, ring_read(&big, b, got, &len, &seq));
        TEST_ASSERT_EQUAL_UINT32(b, seq);
        TEST_ASSERT_EQUAL_UINT32(max, len);
        fill(data, max, b);
        TEST_ASSERT_EQUAL_MEMORY(data, got, max);
    }
    for (uint32_t page = 0; page < 4; page++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((page < 3) ? 1u : 0u,
                                         fake_flashdrv_erases(FLASH_REGION_PAYLOAD, page * FLASH_MAP_PAGE_SIZE),
                                         "each page entered must be erased once");
    }
}

/** @brief The same rules hold for 1 KB slots (two per page) and 2 KB slots (one per page). */
void test_slots_of_half_a_page_and_of_a_whole_page(void)
{
    check_slot_size(1024u);
    check_slot_size(2048u);
}

/* ---- 2. Laps and sequence numbers ---- */

/** @brief Two laps and one block erase every page twice, and page 0 a third time. */
void test_one_erase_per_page_per_lap(void)
{
    append_blocks(0, 2u * SLOTS + 1u);

    TEST_ASSERT_EQUAL_UINT32(3, page_erases(0));
    for (uint32_t page = 1; page < PAGES; page++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, page_erases(page), "one erase per page per lap");
    }
}

/** @brief The block after a full lap erases page 0: its 16 blocks go, all others stay. */
void test_a_full_ring_drops_its_oldest_page(void)
{
    append_blocks(0, SLOTS + 1u);

    uint8_t data[DATA_MAX];
    uint32_t len, seq;
    TEST_ASSERT_EQUAL_INT(HAL_OK, ring_read_oldest(&ring, data, &len, &seq));
    TEST_ASSERT_EQUAL_UINT32(PER_PAGE, seq);
    assert_read(0, PER_PAGE, PER_PAGE);   /* from a number already dropped: the oldest block left */
    assert_read(SLOTS, SLOTS, SLOTS);     /* the newest, in slot 0 */

    reboot();
    TEST_ASSERT_EQUAL_UINT32(SLOTS + 1u, ring.next_seq);
    TEST_ASSERT_EQUAL_UINT32(1, ring.write);
    assert_read(PER_PAGE, PER_PAGE, PER_PAGE);
}

/** @brief KI-10: the newest block and the order of reads survive the wrap of the 32-bit number. */
void test_sequence_numbers_keep_their_order_across_the_wrap(void)
{
    put_block(0, 0xFFFFFFF8u, 1000);
    reboot();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF9u, ring.next_seq);

    append_blocks(1001, 20);   /* numbers 0xFFFFFFF9 .. 0xFFFFFFFF, then 0 .. 12 */
    reboot();
    TEST_ASSERT_EQUAL_HEX32(13u, ring.next_seq);
    TEST_ASSERT_EQUAL_UINT32(21, ring.write);

    uint8_t data[DATA_MAX];
    uint32_t len, seq;
    TEST_ASSERT_EQUAL_INT(HAL_OK, ring_read_oldest(&ring, data, &len, &seq));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF8u, seq);
    assert_read(0xFFFFFFFEu, 0xFFFFFFFEu, 1006);
    assert_read(0, 0, 1008);
    assert_read(12, 12, 1020);
}

/* ---- 3. Faults ---- */

/** @brief F10 / KI-11: a page that no longer erases is skipped, once per lap, and history goes on. */
void test_a_page_that_fails_to_erase_is_skipped(void)
{
    fake_flashdrv_fail_erase(FLASH_REGION_HT, 1u * FLASH_MAP_PAGE_SIZE);

    append_blocks(0, PER_PAGE + 1u);   /* block 16 finds page 1 broken and goes to page 2 */
    TEST_ASSERT_EQUAL_UINT32(1, ring.stats.erase_failures);
    TEST_ASSERT_EQUAL_UINT32(0, ring.stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(PER_PAGE, slot_seq(2u * PER_PAGE));
    assert_read(PER_PAGE, PER_PAGE, PER_PAGE);

    /* One more lap: page 1 is tried once more, and the block goes to page 2 again */
    append_blocks(PER_PAGE + 1u, SLOTS - PER_PAGE);
    TEST_ASSERT_EQUAL_UINT32(2, ring.stats.erase_failures);
    TEST_ASSERT_EQUAL_UINT32(2, page_erases(1));
    TEST_ASSERT_EQUAL_UINT32(0, ring.stats.dropped);

    reboot();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, ring.stats.erase_failures, "a reload keeps the counters");
    TEST_ASSERT_EQUAL_UINT32(SLOTS + 1u, ring.next_seq);
    assert_read(SLOTS, SLOTS, SLOTS);
}

/** @brief Two broken pages in a row cost one block and two erases, no more. */
void test_two_failing_pages_drop_one_block_after_two_erases(void)
{
    fake_flashdrv_fail_erase(FLASH_REGION_HT, 1u * FLASH_MAP_PAGE_SIZE);
    fake_flashdrv_fail_erase(FLASH_REGION_HT, 2u * FLASH_MAP_PAGE_SIZE);
    append_blocks(0, PER_PAGE);

    uint8_t data[DATA_MAX];
    fill(data, 20, 99);
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ring_append(&ring, data, 20));
    TEST_ASSERT_EQUAL_UINT32(1, ring.stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(2, ring.stats.erase_failures);
    TEST_ASSERT_EQUAL_UINT32(1, page_erases(1));
    TEST_ASSERT_EQUAL_UINT32(1, page_erases(2));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, page_erases(3), "one block must not cost a third erase");

    append(PER_PAGE);   /* the next block goes to page 3; no number was used by the failed erases */
    TEST_ASSERT_EQUAL_UINT32(PER_PAGE, slot_seq(3u * PER_PAGE));
    assert_read(PER_PAGE, PER_PAGE, PER_PAGE);
}

/** @brief A slot holding stray data cannot be programmed: the block goes to the next page with the next number. */
void test_a_slot_that_cannot_be_programmed_ends_its_page(void)
{
    append_blocks(0, 5);
    slot_bytes(5)[40] = 0x00;

    append(5);
    TEST_ASSERT_EQUAL_UINT32(1, ring.stats.program_failures);
    TEST_ASSERT_EQUAL_UINT32(PER_PAGE + 1u, ring.write);
    assert_read(5, 6, 5);   /* the attempt in slot 5 took number 5 */

    reboot();
    TEST_ASSERT_EQUAL_UINT32(7, ring.next_seq);
    TEST_ASSERT_EQUAL_UINT32(PER_PAGE + 1u, ring.write);
    assert_read(5, 6, 5);
    for (uint32_t seq = 0; seq < 5; seq++) assert_read(seq, seq, seq);
}

/**
 * @brief Appends block 3 to slot 3 with a fault in one of its doublewords, which the read-back must catch.
 * @details Block 3 fills its slot: header at 0, data from 8 to 119, trailer at SLOT - 8.
 * @param offset_in_slot The faulty doubleword: 0 for the header, 16 or SLOT - 16 for the first or the last
 *                       of the data (the first and the last chunk read back), SLOT - 8 for the trailer.
 * @param ecc true: it fails ECC when read back. false: it is programmed with three bits wrong and reads back without an error.
 */
static void check_a_fault_the_read_back_catches(uint32_t offset_in_slot, bool ecc)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(DATA_MAX, length_for(3), "block 3 must fill its slot");
    append_blocks(0, 3);
    if (ecc) {
        fake_flashdrv_inject_ecc(FLASH_REGION_HT, 3u * SLOT + offset_in_slot);
    } else {
        fake_flashdrv_inject_wrong_program(FLASH_REGION_HT, 3u * SLOT + offset_in_slot);
    }

    append(3);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ring.stats.program_failures, "the read-back did not catch the fault");
    TEST_ASSERT_EQUAL_UINT32(PER_PAGE + 1u, ring.write);
    assert_read(3, 4, 3);   /* slot 3 kept number 3 but holds no valid block */

    reboot();
    TEST_ASSERT_EQUAL_UINT32(5, ring.next_seq);
    assert_read(3, 4, 3);
    for (uint32_t seq = 0; seq < 3; seq++) assert_read(seq, seq, seq);
}

/** @brief A block whose data does not read back (its first doubleword fails ECC) is written again on the next page, under a new number. */
void test_a_block_that_does_not_read_back_is_written_again(void) { check_a_fault_the_read_back_catches(16u, true); }

/** @brief Same with the header failing ECC. */
void test_a_header_that_does_not_read_back_is_written_again(void) { check_a_fault_the_read_back_catches(0u, true); }

/** @brief Same with the trailer failing ECC. */
void test_a_trailer_that_does_not_read_back_is_written_again(void) { check_a_fault_the_read_back_catches(SLOT - 8u, true); }

/** @brief Same with the header programmed wrong, without an ECC error. */
void test_a_header_that_reads_back_wrong_is_written_again(void) { check_a_fault_the_read_back_catches(0u, false); }

/** @brief Same with the last doubleword of the data programmed wrong, without an ECC error. */
void test_a_block_that_reads_back_wrong_is_written_again(void) { check_a_fault_the_read_back_catches(SLOT - 16u, false); }

/** @brief Same with the trailer programmed wrong, without an ECC error. */
void test_a_trailer_that_reads_back_wrong_is_written_again(void) { check_a_fault_the_read_back_catches(SLOT - 8u, false); }

/** @brief An ECC error in a header hides that slot only. */
void test_an_ecc_error_in_a_header_hides_only_its_slot(void)
{
    append_blocks(0, 5);
    fake_flashdrv_inject_ecc(FLASH_REGION_HT, 2u * SLOT);
    reboot();

    TEST_ASSERT_EQUAL_UINT32(5, ring.next_seq);
    TEST_ASSERT_EQUAL_UINT32(5, ring.write);
    assert_read(0, 0, 0);
    assert_read(2, 3, 3);
}

/**
 * @brief A header damaged without an ECC error fails its check: its slot counts as blank, and the ring goes on after the newest block.
 * @details Here the number of slot 2 reads 1000. Taken for the newest block, it
 *          would send the next block to slot 3, which holds data, and from
 *          there to page 1, whose erase would lose blocks 16 to 19.
 */
void test_a_header_damaged_without_an_ecc_error_counts_as_blank(void)
{
    append_blocks(0, PER_PAGE + 4u);       /* pages 0 and 1 */
    put_le32(slot_bytes(2) + 4, 1000u);    /* slot 2's number */
    reboot();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(PER_PAGE + 4u, ring.next_seq, "the damaged number was taken for the newest block");
    TEST_ASSERT_EQUAL_UINT32(PER_PAGE + 4u, ring.write);

    append(PER_PAGE + 4u);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, page_erases(1), "page 1 was erased again");
    assert_read(2, 3, 3);   /* slot 2 holds no block */
    for (uint32_t seq = 0; seq <= PER_PAGE + 4u; seq++) {
        if (seq != 2u) assert_read(seq, seq, seq);
    }
}

/* ---- 4. Power cuts ---- */

/** @brief F14: a slot whose header is programmed but whose trailer is not holds no block, and the ring goes on after it. */
void test_a_block_cut_short_is_not_read(void)
{
    append_blocks(0, 3);

    uint8_t data[DATA_MAX];
    uint32_t len, seq;
    fill(data, length_for(3), 3);
    fake_flashdrv_cut_power_after(1);   /* the header is programmed; the power goes during the data */
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ring_append(&ring, data, length_for(3)));

    reboot();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, slot_seq(3), "the header should be in flash");
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_ERROR, ring_read(&ring, 3, data, &len, &seq), "a block cut short was read");
    TEST_ASSERT_EQUAL_UINT32(4, ring.write);
    TEST_ASSERT_EQUAL_UINT32(4, ring.next_seq);

    append(4);
    assert_read(3, 4, 4);
    for (uint32_t s = 0; s < 3; s++) assert_read(s, s, s);
}

/** @brief FLS-INT-010: the trailer is the last part programmed, so a cut at any earlier step leaves it erased. */
void test_the_trailer_is_programmed_last(void)
{
    uint8_t data[DATA_MAX];
    fill(data, 20, 77);
    for (long cut = 0; cut < 4; cut++) {   /* the header, 2 doublewords of data and the tail come first */
        fake_flashdrv_reset();
        ring_load(&ring);
        append(0);
        fake_flashdrv_cut_power_after(cut);
        TEST_ASSERT_EQUAL_INT(HAL_ERROR, ring_append(&ring, data, 20));

        const uint8_t *trailer = slot_bytes(1) + SLOT - 8u;
        for (uint32_t i = 0; i < 8u; i++) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, trailer[i], "the trailer was programmed before the rest of the slot");
        }
    }
}

/** @brief After a cut, the blocks before are intact, the new one is whole or absent, and the ring works. */
static void check_after_cut(HAL_StatusTypeDef status, uint32_t first_kept, uint32_t new_seq, const uint8_t *new_data, uint32_t new_len)
{
    reboot();
    for (uint32_t seq = first_kept; seq < new_seq; seq++) assert_read(seq, seq, seq);

    uint8_t got[DATA_MAX];
    uint32_t len = 0, seq = 0;
    bool found = (ring_read(&ring, new_seq, got, &len, &seq) == HAL_OK);
    if (found) {
        TEST_ASSERT_EQUAL_UINT32(new_seq, seq);
        TEST_ASSERT_EQUAL_UINT32(new_len, len);
        TEST_ASSERT_EQUAL_MEMORY(new_data, got, new_len);
    } else {
        TEST_ASSERT_TRUE_MESSAGE(status != HAL_OK, "a block reported as stored was lost");
    }

    /* The ring goes on: the next block gets a number after every block stored and reads back as the newest.
       It may land a page further on, after a slot the cut left half programmed. */
    append(900);
    uint32_t stored = ring.next_seq - 1u;
    TEST_ASSERT_TRUE_MESSAGE((int32_t)(stored - new_seq) >= (found ? 1 : 0), "a number was given twice");
    reboot();
    assert_read(stored, stored, 900);
    for (uint32_t s = first_kept; s < new_seq; s++) assert_read(s, s, s);
}

/** @brief One cut point of an append into the middle of page 0; true if the cut happened. */
static bool cut_during_an_append(long cut, bool ecc)
{
    uint8_t data[DATA_MAX];
    fake_flashdrv_reset();
    fake_flashdrv_set_cut_ecc(ecc);
    ring_load(&ring);
    append_blocks(0, 5);

    fill(data, 20, 77);
    fake_flashdrv_cut_power_after(cut);
    HAL_StatusTypeDef status = ring_append(&ring, data, 20);
    bool cut_happened = fake_flashdrv_power_is_cut();

    check_after_cut(status, 0, 5, data, 20);
    return cut_happened;
}

static void sweep_append(bool ecc)
{
    long cut = 0;
    while (cut_during_an_append(cut, ecc)) cut++;
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, cut, "a 20-byte block should take 5 doublewords: header, 2 of data, tail, trailer");
}

/** @brief Power cut at every step of an append; the cut doubleword fails ECC, as on the satellite. */
void test_power_cut_during_an_append(void) { sweep_append(true); }

/** @brief Same, with the cut doubleword readable but half programmed. */
void test_power_cut_during_an_append_without_ecc_error(void) { sweep_append(false); }

/** @brief One cut point of the append that erases page 0 of a full ring; true if the cut happened. */
static bool cut_during_an_erasing_append(long cut, bool ecc)
{
    uint8_t data[DATA_MAX];
    fake_flashdrv_reset();
    fake_flashdrv_set_cut_ecc(ecc);
    ring_load(&ring);
    append_blocks(0, SLOTS);

    fill(data, 20, 77);
    fake_flashdrv_cut_power_after(cut);
    HAL_StatusTypeDef status = ring_append(&ring, data, 20);
    bool cut_happened = fake_flashdrv_power_is_cut();

    check_after_cut(status, PER_PAGE, SLOTS, data, 20);   /* page 0's blocks may go; every other one stays */
    return cut_happened;
}

static void sweep_erasing_append(bool ecc)
{
    long cut = 0;
    while (cut_during_an_erasing_append(cut, ecc)) cut++;
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, cut, "an append that enters a page should take 1 erase and 5 doublewords");
}

/** @brief Power cut at every step of an append that erases a page, with ECC errors on what the cut damaged. */
void test_power_cut_during_an_append_that_erases(void) { sweep_erasing_append(true); }

/** @brief Same, without ECC errors. */
void test_power_cut_during_an_append_that_erases_without_ecc_error(void) { sweep_erasing_append(false); }

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();

    // 1. Layout and basics
    POCAT_RUN_TEST(test_slot_layout_matches_the_spec);
    POCAT_RUN_TEST(test_blank_region_holds_no_block);
    POCAT_RUN_TEST(test_blocks_read_back_by_sequence_number);
    POCAT_RUN_TEST(test_lengths_from_empty_to_a_full_slot);
    POCAT_RUN_TEST(test_a_block_too_long_for_a_slot_is_refused);
    POCAT_RUN_TEST(test_a_reload_continues_after_the_newest_block);
    POCAT_RUN_TEST(test_a_zeroed_slot_counts_as_blank);
    POCAT_RUN_TEST(test_a_length_that_does_not_fit_counts_as_blank);
    POCAT_RUN_TEST(test_slots_of_half_a_page_and_of_a_whole_page);

    // 2. Laps and sequence numbers
    POCAT_RUN_TEST(test_one_erase_per_page_per_lap);
    POCAT_RUN_TEST(test_a_full_ring_drops_its_oldest_page);
    POCAT_RUN_TEST(test_sequence_numbers_keep_their_order_across_the_wrap);

    // 3. Faults
    POCAT_RUN_TEST(test_a_page_that_fails_to_erase_is_skipped);
    POCAT_RUN_TEST(test_two_failing_pages_drop_one_block_after_two_erases);
    POCAT_RUN_TEST(test_a_slot_that_cannot_be_programmed_ends_its_page);
    POCAT_RUN_TEST(test_a_block_that_does_not_read_back_is_written_again);
    POCAT_RUN_TEST(test_a_header_that_does_not_read_back_is_written_again);
    POCAT_RUN_TEST(test_a_trailer_that_does_not_read_back_is_written_again);
    POCAT_RUN_TEST(test_a_header_that_reads_back_wrong_is_written_again);
    POCAT_RUN_TEST(test_a_block_that_reads_back_wrong_is_written_again);
    POCAT_RUN_TEST(test_a_trailer_that_reads_back_wrong_is_written_again);
    POCAT_RUN_TEST(test_an_ecc_error_in_a_header_hides_only_its_slot);
    POCAT_RUN_TEST(test_a_header_damaged_without_an_ecc_error_counts_as_blank);

    // 4. Power cuts
    POCAT_RUN_TEST(test_a_block_cut_short_is_not_read);
    POCAT_RUN_TEST(test_the_trailer_is_programmed_last);
    POCAT_RUN_TEST(test_power_cut_during_an_append);
    POCAT_RUN_TEST(test_power_cut_during_an_append_without_ecc_error);
    POCAT_RUN_TEST(test_power_cut_during_an_append_that_erases);
    POCAT_RUN_TEST(test_power_cut_during_an_append_that_erases_without_ecc_error);

    return POCAT_END();
}
