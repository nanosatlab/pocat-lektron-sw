/**
 * @file test_ht_handling.c
 * @brief Host tests for historic telemetry (HT): the circular queue of compressed beacon blocks in internal flash.
 * @date 2026-10-23
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/ht_handling.c:
 *   - `ht_init()`, which rebuilds the queue state at boot by scanning the slot
 *     headers;
 *   - `fill_ht_from_it()`, which adds one beacon and flushes the block to
 *     flash when it is full;
 *   - the private helpers `ht_store_block()` and `ht_account_page_erase()`.
 *
 * **Geometry used by these tests** (from mocks/stubs_project/flash.h)
 * - HT region: 0x2000 bytes (8 KB) at 0x080FE000.
 * - The region holds 64 slots of 128 bytes, in 4 flash pages of 2 KB (16 slots
 *   per page).
 * - Each slot is a 12-byte header followed by 116 bytes of payload.
 *
 * **How many beacons fill a block here**
 * `mocks/mock_pocket_plus.c` replaces the real POCKET+ compressor with one
 * that always returns 20 bytes, so block filling is predictable:
 * - the reference beacon (20 B) plus 4 compressed beacons (4 × 20 B) =
 *   100 B ≤ 116 B. A 5th compressed beacon (120 B) doesn't fit;
 * - so a block holds **5 beacons**. The **6th** `fill_ht_from_it()` call
 *   flushes the first block to flash, and **12** calls flush exactly 2 blocks.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - This suite is older than the rest and uses its own fakes (`NO_FAKES`; no
 *   fake_rtos):
 *   - `mocks/stubs_project/flash.h` replaces the real flash map, for this
 *     suite only;
 *   - `support/fake_flash_memory.c` provides `fake_flash_memory[]`, a RAM copy
 *     of the HT region, plus counters (`mock_erase_count`,
 *     `mock_program_count`) and a power-cut simulation
 *     (`mock_power_cut_after_ops`);
 *   - `mocks/mock_obdh_requests.c` provides the 4 `obdh_*_request()`
 *     functions over that RAM copy, with error injection through
 *     `mock_obdh_inject_error()`.
 * - `ht_handling.c` is `#include`d below, so tests can read and set the
 *   private `ht_queue` state directly.
 *
 * **Rule when setting `ht_queue` by hand**
 * Set `current_ht_count` as well as the pointers. The eviction logic uses the
 * count to decide which slots are occupied, so pointers without a count
 * describe an empty queue.
 *
 * **Current status**
 * 12 of 14 tests pass. Two fail on purpose because they expose real
 * limitations of ht_handling.c: KI-10 and KI-11 in test/README.md.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_ht_handling
 * docker run --rm -v "${PWD}:/app" pocat-tests test_ht_handling -f init
 * @endcode
 */
#include "unity.h"
#include "fake_flash_memory.h"
#include "stm32l4xx_hal.h"
#include <string.h>

/** @brief Makes every later OBDH flash request fail with @p err (`HAL_OK` turns it off). Defined in mocks/mock_obdh_requests.c. */
extern void mock_obdh_inject_error(HAL_StatusTypeDef err);

// We include the C file directly to statically inspect variables without breaking encapsulation
#include "ht_handling.c"

/**
 * @brief Helper: writes a valid slot header straight into the fake flash, as if a block had been stored before a reboot.
 * @details Used to prepare the flash before calling `ht_init()`. The header
 * says 5 beacons in POCKET V3 format; only the header is written, not the
 * payload.
 * @param slot_index Slot number (0..63).
 * @param seq        Block sequence number. `ht_init()` uses it to find the oldest and newest blocks.
 * @param epoch      Unix time of the block's first beacon.
 * @param data_len   Payload bytes used. Must be 20..116, or `ht_init()` treats the slot as invalid.
 */
void inject_slot(int slot_index, uint32_t seq, uint32_t epoch, uint16_t data_len) {
    ht_slot_header header;
    header.seq = seq;
    header.epoch = epoch;
    header.data_len = data_len;
    header.count = 5;
    header.format = HT_FORMAT_POCKET_V3;

    int offset = slot_index * HT_SLOT_SIZE;
    memcpy(&fake_flash_memory[offset], &header, sizeof(ht_slot_header));
}

/**
 * @brief Runs before every test: erased flash and no injected errors.
 * @details `fake_flash_reset()` does the following:
 * - fills the HT region with 0xFF (erased flash);
 * - zeroes the erase/program counters;
 * - turns off the power-cut simulation;
 * - turns on strict mode.
 *
 * In strict mode a test fails if the code programs flash at an address or
 * length that isn't a multiple of 8, or tries to turn a 0 bit back into 1
 * without erasing first. The real STM32L4 flash can't do either.
 */
void setUp(void) {
    fake_flash_reset();
    mock_obdh_inject_error(HAL_OK);
}

/** @brief Runs after every test. Nothing to check: this suite doesn't use the fake RTOS. */
void tearDown(void) {
}

/* =========================================================================
 * 1. BOOT & INITIALIZATION TESTS
 *    Prepare the flash with inject_slot(), call ht_init(), and check
 *    where it places the reading pointer (oldest block) and the
 *    writing pointer (next slot to use).
 * ========================================================================= */

/**
 * @brief [Objective #1] With completely empty (erased) flash, both pointers start at slot 0.
 */
void test_init_virgin_flash(void) {
    ht_init();
    TEST_ASSERT_EQUAL(0, ht_queue.writing_pointer);
    TEST_ASSERT_EQUAL(0, ht_queue.reading_pointer);
}

/**
 * @brief [Objective #3] `ht_init()` finds the oldest and newest blocks among the stored ones.
 * @details Slots 0..9 hold seq 100..109. Expected: the reading pointer is 0
 * (oldest, seq 100), and the writing pointer is 10, the slot after the newest.
 */
void test_init_finds_correct_pointers(void) {
    for (int i = 0; i < 10; i++) inject_slot(i, 100 + i, 1000 + i, 116);
    ht_init();
    TEST_ASSERT_EQUAL(0, ht_queue.reading_pointer);
    TEST_ASSERT_EQUAL(10, ht_queue.writing_pointer);
}

/**
 * @brief [Objective #4] Queue full and already wrapped around: the newest block is in slot 30, the oldest in slot 31.
 * @details All 64 slots are occupied. Expected: the reading pointer is 31
 * (oldest), and the writing pointer is also 31, because the next block will
 * overwrite the oldest one.
 */
void test_init_full_and_wrapped(void) {
    for (int i = 0; i < 64; i++) {
        // Slot 31 is the oldest (seq 437), Slot 30 is the newest (seq 500)
        uint32_t seq = (i > 30) ? (437 + (i - 31)) : (437 + (64 - 31) + i);
        inject_slot(i, seq, 1000, 116);
    }
    ht_init();
    TEST_ASSERT_EQUAL_MESSAGE(31, ht_queue.reading_pointer, "Failed to find oldest wrapped block");
    TEST_ASSERT_EQUAL_MESSAGE(31, ht_queue.writing_pointer, "Failed to position writing pointer at the oldest block to overwrite it");
}

/**
 * @brief [Objective #5] The 32-bit sequence counter has wrapped from 0xFFFFFFFE back to small numbers.
 * @details Slots 0..3 hold seq 0xFFFFFFFD, 0xFFFFFFFE, 1 and 2. Ordered by
 * time, slot 0 is the oldest and slot 3 the newest. Expected: reading
 * pointer 0, writing pointer 4.
 * @bug KI-10 (test/README.md): FAILS today. `ht_init()` compares `seq` with
 * plain `<` / `>`, so it picks seq 1 (slot 2) as the oldest. This needs 2^32
 * stored blocks, so it is very unlikely in a real mission, but it is real.
 */
void test_init_seq_counter_wrap(void) {
    inject_slot(0, 0xFFFFFFFD, 1000, 116);
    inject_slot(1, 0xFFFFFFFE, 1000, 116);
    inject_slot(2, 0x00000001, 1000, 116); // Wrapped around, skipped FFFFFFFF
    inject_slot(3, 0x00000002, 1000, 116);

    ht_init();
    TEST_ASSERT_EQUAL_MESSAGE(0, ht_queue.reading_pointer, "Oldest should be 0xFFFFFFFD at slot 0");
    TEST_ASSERT_EQUAL_MESSAGE(4, ht_queue.writing_pointer, "Newest is 0x00000002 at slot 3");
}

/**
 * @brief [Objective #10] An erased slot (all 0xFF, so seq 0xFFFFFFFF) isn't treated as a stored block.
 * @details Only slot 0 holds a block. Expected: the writing pointer is 1.
 * If the erased slot 1 were taken as the newest block, the pointer would
 * jump to 2.
 */
void test_init_ignores_0xFFFFFFFF(void) {
    inject_slot(0, 5, 1000, 116);
    // Slot 1 remains 0xFF
    ht_init();
    TEST_ASSERT_EQUAL_MESSAGE(1, ht_queue.writing_pointer, "CRITICAL BUG: ht_init thought 0xFFFFFFFF was a valid sequence number!");
}

/**
 * @brief [Objective #8] Two slots with the same sequence number don't crash `ht_init()` or produce a wild pointer.
 * @details Any consistent choice is accepted. The test only requires the
 * writing pointer to stay within the occupied range (≤ 2).
 */
void test_init_duplicate_seq(void) {
    inject_slot(0, 42, 1000, 116);
    inject_slot(1, 42, 1000, 100); // Duplicate!
    ht_init();
    // It should deterministically pick one without crashing.
    TEST_ASSERT_MESSAGE(ht_queue.writing_pointer <= 2, "Duplicate sequence caused crash or wild pointer");
}

/**
 * @brief [Objective #9] A missing or corrupted slot in the middle of the queue doesn't confuse `ht_init()`.
 * @details Slots 0..29 hold seq 10..39, except slot 15, which stays erased.
 * Expected: the oldest block is still slot 0, and the writing pointer is 30.
 */
void test_init_corrupt_middle_slot(void) {
    for (int i = 0; i < 30; i++) {
        if (i == 15) continue; // Skip slot 15 (seq 25)
        inject_slot(i, 10 + i, 1000, 116);
    }
    ht_init();
    TEST_ASSERT_EQUAL_MESSAGE(0, ht_queue.reading_pointer, "Oldest should still be slot 0");
    TEST_ASSERT_EQUAL_MESSAGE(30, ht_queue.writing_pointer, "Newest should still be slot 29");
}

/* =========================================================================
 * 2. WRITE & CIRCULAR WRAPAROUND TESTS
 *    Feed beacons with fill_ht_from_it(). Remember: the 6th call flushes
 *    the first block, and 12 calls flush 2 blocks.
 * ========================================================================= */

/**
 * @brief [Objective #14] A flash page is erased only when the writing pointer enters it, i.e. at slots 0, 16, 32 and 48.
 * @details 12 beacons flush 2 blocks, into slot 0 and slot 1. Expected:
 * - 1 erase, because entering page 0 at slot 0 uses erase+program;
 * - 2 programs in total, because slot 1 uses program only.
 * Erasing for every slot would wear the flash out 16 times faster.
 */
void test_erase_only_on_page_boundaries(void) {
    ht_init();
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};

    // Force exactly 2 slot flushes
    for(int i = 0; i < 12; i++) fill_ht_from_it(dummy_it);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_erase_count, "There should be exactly 1 erase (for Page 0)");
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_program_count, "There should be 2 programs (Slot 0 and Slot 1)");
}

/**
 * @brief The writing pointer goes back to slot 0 after writing the last slot (63).
 * @details Starting from writing pointer 63, one flush (6 beacons) writes
 * slot 63, and the pointer must wrap to 0, not run to 64 past the end of the
 * region.
 */
void test_queue_wraparound(void) {
    ht_init();
    ht_queue.writing_pointer = 63;
    ht_queue.reading_pointer = 0;

    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    for(int i = 0; i < 6; i++) fill_ht_from_it(dummy_it); // Force flush

    TEST_ASSERT_EQUAL(0, ht_queue.writing_pointer);
}

/**
 * @brief [Objective #17] Erasing page 0 doesn't move the reading pointer when the oldest block is in another page.
 * @details
 * - Arrange: blocks occupy slots 40..62 (23 blocks, oldest in page 2), and
 *   the writing pointer is at 63.
 * - Act: 12 beacons, i.e. 2 flushes. The first writes slot 63; the second
 *   wraps to slot 0 and erases page 0 (slots 0..15), which holds no stored
 *   block.
 * - Expected: the reading pointer stays at 40.
 */
void test_rp_in_another_page(void) {
    ht_init();
    ht_queue.writing_pointer = 63;
    ht_queue.reading_pointer = 40; // Safely in page 2
    ht_queue.current_ht_count = 23; // Slots 40..62 occupied (keeps the queue state consistent with the pointers)

    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    for(int i = 0; i < 12; i++) fill_ht_from_it(dummy_it); // 2 flushes: slot 63, then wrap to slot 0 (erases page 0)

    TEST_ASSERT_EQUAL_MESSAGE(40, ht_queue.reading_pointer, "Reading pointer should NOT move if not in the erased page!");
}

/* =========================================================================
 * 3. ORBIT-KILLER BUGS & FAULTS
 *    Situations that would lose telemetry in orbit: page eviction, bad
 *    flash, power cuts in the middle of a write, resets.
 * ========================================================================= */

/**
 * @brief [Objectives #16/#18] Erasing a page that holds the oldest blocks moves the reading pointer past that page.
 * @details
 * - Arrange: the queue is almost full, with 62 blocks in slots 18..63 and
 *   0..15. The writing pointer is at 16, the first slot of page 1, and the
 *   oldest block is in slot 18.
 * - Act: one flush (6 beacons). Writing slot 16 erases all of page 1 (slots
 *   16..31), which destroys the blocks in slots 18..31.
 * - Expected: the reading pointer jumps to 32, the first slot that survived.
 *   Otherwise the ground would download erased data.
 */
void test_eviction_jumps_page(void) {
    ht_init();
    ht_queue.writing_pointer = 16;
    ht_queue.reading_pointer = 18;
    ht_queue.current_ht_count = 62; // Slots 18..15 occupied: pointers alone mean nothing without the count

    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    for(int i = 0; i < 6; i++) fill_ht_from_it(dummy_it);

    TEST_ASSERT_EQUAL_MESSAGE(32, ht_queue.reading_pointer, "CRITICAL BUG: Reading pointer did not jump past the destroyed page!");
}

/**
 * @brief [Objective #26] A flash page that can't be written must not block historic telemetry forever.
 * @details
 * - Arrange: every OBDH flash request fails with `HAL_ERROR`. This simulates
 *   page 0 being damaged, for example by radiation. Note that the injected
 *   error affects every flash call, not only page 0.
 * - Act: one flush (6 beacons). The erase+program of page 0 fails.
 * - Expected: the queue skips the bad page and continues at slot 16.
 * @bug KI-11 (test/README.md): FAILS today. `ht_store_block()` returns
 * without moving the writing pointer, so every later flush retries page 0
 * and history is no longer stored.
 */
void test_bad_sector_deadlock(void) {
    ht_init();
    mock_obdh_inject_error(HAL_ERROR); // Simulate radiation hit on Page 0
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};

    for (int i = 0; i < 6; i++) fill_ht_from_it(dummy_it);

    TEST_ASSERT_EQUAL_MESSAGE(16, ht_queue.writing_pointer, "CRITICAL BUG: Queue deadlocked on bad sector!");
}

/**
 * @brief [Objective A] Power is cut after the page erase but before the slot is programmed (a "torn write").
 * @details
 * - Arrange: `mock_power_cut_after_ops = 1` makes the fake flash "lose power"
 *   right after the first operation (the erase).
 * - Act: one flush, then power is restored and `ht_init()` runs again, as at
 *   reboot.
 * - Expected: the half-written slot isn't accepted as a valid block, so the
 *   writing pointer stays at 0.
 */
void test_torn_write_recovery(void) {
    mock_power_cut_after_ops = 1; // Cut power exactly after the erase

    ht_init();
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    for (int i = 0; i < 6; i++) fill_ht_from_it(dummy_it);

    mock_power_cut_after_ops = -1; // Restore power
    ht_init();

    TEST_ASSERT_EQUAL_MESSAGE(0, ht_queue.writing_pointer, "CRITICAL BUG: ht_init accepted a torn (half-written) slot!");
}

/**
 * @brief [Objective #39] Beacons still in the RAM block are lost at a reset, and the queue must not pretend otherwise.
 * @details 3 beacons fit in the open block, so nothing reaches flash. After a
 * simulated reset (`ht_init()`), the writing pointer must still be 0. This
 * is the documented trade-off in ht_handling.h: up to one block of history
 * lives only in RAM.
 */
void test_ram_loss_at_reset(void) {
    ht_init();
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    // Feed 3 beacons (not enough to flush to flash)
    for (int i = 0; i < 3; i++) fill_ht_from_it(dummy_it);

    // Simulate reset
    ht_init();

    // The RAM data is lost. The writing pointer should still be at 0.
    TEST_ASSERT_EQUAL_MESSAGE(0, ht_queue.writing_pointer, "Pointers should not advance for unflushed RAM data after reset");
}

#include "pocat_test_main.h"

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 * @details A test function that isn't listed here never runs. See
 * test/support/pocat_test_main.h for the `-l` / `-f` / `-n` / `-x` options.
 */
int main(int argc, char **argv) {
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();

    // 1. Boot
    POCAT_RUN_TEST(test_init_virgin_flash);
    POCAT_RUN_TEST(test_init_finds_correct_pointers);
    POCAT_RUN_TEST(test_init_full_and_wrapped);
    POCAT_RUN_TEST(test_init_seq_counter_wrap);
    POCAT_RUN_TEST(test_init_ignores_0xFFFFFFFF);
    POCAT_RUN_TEST(test_init_duplicate_seq);
    POCAT_RUN_TEST(test_init_corrupt_middle_slot);

    // 2. Logic
    POCAT_RUN_TEST(test_erase_only_on_page_boundaries);
    POCAT_RUN_TEST(test_queue_wraparound);
    POCAT_RUN_TEST(test_rp_in_another_page);

    // 3. Orbit-Killer Bugs
    POCAT_RUN_TEST(test_eviction_jumps_page);
    POCAT_RUN_TEST(test_bad_sector_deadlock);
    POCAT_RUN_TEST(test_torn_write_recovery);
    POCAT_RUN_TEST(test_ram_loss_at_reset);

    return POCAT_END();
}
