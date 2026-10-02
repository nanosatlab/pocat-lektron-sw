#include "unity.h"
#include "fake_flash_memory.h"
#include "stm32l4xx_hal.h"
#include <string.h>

// Mock fault injectors
extern void mock_obdh_inject_error(HAL_StatusTypeDef err);

// We include the C file directly to statically inspect variables without breaking encapsulation
#include "ht_handling.c"

// Helper function to inject a crafted valid header directly into the fake flash memory
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

void setUp(void) {
    fake_flash_reset();
    mock_obdh_inject_error(HAL_OK);
}

void tearDown(void) {
}

/* =========================================================================
 * 1. BOOT & INITIALIZATION TESTS
 * ========================================================================= */

// [Objective] #1: Verify ht_init behaves correctly when flash is completely empty (Virgin flash)
void test_init_virgin_flash(void) {
    ht_init();
    TEST_ASSERT_EQUAL(0, ht_queue.writing_pointer);
    TEST_ASSERT_EQUAL(0, ht_queue.reading_pointer);
}

// [Objective] #3: Verify ht_init correctly scans and finds the oldest and newest sequence numbers
void test_init_finds_correct_pointers(void) {
    for (int i = 0; i < 10; i++) inject_slot(i, 100 + i, 1000 + i, 116);
    ht_init();
    TEST_ASSERT_EQUAL(0, ht_queue.reading_pointer);
    TEST_ASSERT_EQUAL(10, ht_queue.writing_pointer);
}

// [Objective] #4: Full and wrapped. Queue is full, newest at slot 30, oldest at slot 31.
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

// [Objective] #5: Sequence counter wrap. Nears 0xFFFFFFFF and rolls over.
void test_init_seq_counter_wrap(void) {
    inject_slot(0, 0xFFFFFFFD, 1000, 116);
    inject_slot(1, 0xFFFFFFFE, 1000, 116);
    inject_slot(2, 0x00000001, 1000, 116); // Wrapped around, skipped FFFFFFFF
    inject_slot(3, 0x00000002, 1000, 116);
    
    ht_init();
    TEST_ASSERT_EQUAL_MESSAGE(0, ht_queue.reading_pointer, "Oldest should be 0xFFFFFFFD at slot 0");
    TEST_ASSERT_EQUAL_MESSAGE(4, ht_queue.writing_pointer, "Newest is 0x00000002 at slot 3");
}

// [Objective] #10: Verify that a sequence of 0xFFFFFFFF is ignored (it's erased memory)
void test_init_ignores_0xFFFFFFFF(void) {
    inject_slot(0, 5, 1000, 116); 
    // Slot 1 remains 0xFF
    ht_init();
    TEST_ASSERT_EQUAL_MESSAGE(1, ht_queue.writing_pointer, "CRITICAL BUG: ht_init thought 0xFFFFFFFF was a valid sequence number!");
}

// [Objective] #8: Duplicate sequence. Two slots have the same sequence number.
void test_init_duplicate_seq(void) {
    inject_slot(0, 42, 1000, 116);
    inject_slot(1, 42, 1000, 100); // Duplicate!
    ht_init();
    // It should deterministically pick one without crashing.
    TEST_ASSERT_MESSAGE(ht_queue.writing_pointer <= 2, "Duplicate sequence caused crash or wild pointer");
}

// [Objective] #9: Corrupt middle slot. Valid run 10..40, but 25 is missing/corrupted.
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
 * ========================================================================= */

// [Objective] #14: Verify that a flash Erase only occurs on Page Boundaries (every 16 slots)
void test_erase_only_on_page_boundaries(void) {
    ht_init();
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    
    // Force exactly 2 slot flushes
    for(int i = 0; i < 12; i++) fill_ht_from_it(dummy_it);
    
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_erase_count, "There should be exactly 1 erase (for Page 0)");
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_program_count, "There should be 2 programs (Slot 0 and Slot 1)");
}

// [Objective] Verify that writing pointer wraps around to 0 after slot 63
void test_queue_wraparound(void) {
    ht_init();
    ht_queue.writing_pointer = 63;
    ht_queue.reading_pointer = 0;
    
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    for(int i = 0; i < 6; i++) fill_ht_from_it(dummy_it); // Force flush
    
    TEST_ASSERT_EQUAL(0, ht_queue.writing_pointer);
}

// [Objective] #17: Reading pointer is in another page. Erasing page 0 should not move it.
void test_rp_in_another_page(void) {
    ht_init();
    ht_queue.writing_pointer = 63;
    ht_queue.reading_pointer = 40; // Safely in page 2
    
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    for(int i = 0; i < 6; i++) fill_ht_from_it(dummy_it); // Wrap to 0 and erase page 0
    
    TEST_ASSERT_EQUAL_MESSAGE(40, ht_queue.reading_pointer, "Reading pointer should NOT move if not in the erased page!");
}

/* =========================================================================
 * 3. ORBIT-KILLER BUGS & FAULTS
 * ========================================================================= */

// [Objective] #16/#18: The "Page Eviction" bug. When erasing a page, it destroys 16 slots.
void test_eviction_jumps_page(void) {
    ht_init();
    ht_queue.writing_pointer = 16;
    ht_queue.reading_pointer = 18; 
    
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    for(int i = 0; i < 6; i++) fill_ht_from_it(dummy_it);
    
    TEST_ASSERT_EQUAL_MESSAGE(32, ht_queue.reading_pointer, "CRITICAL BUG: Reading pointer did not jump past the destroyed page!");
}

// [Objective] #26: The Bad Sector Deadlock bug.
void test_bad_sector_deadlock(void) {
    ht_init();
    mock_obdh_inject_error(HAL_ERROR); // Simulate radiation hit on Page 0
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    
    for (int i = 0; i < 6; i++) fill_ht_from_it(dummy_it);
    
    TEST_ASSERT_EQUAL_MESSAGE(16, ht_queue.writing_pointer, "CRITICAL BUG: Queue deadlocked on bad sector!");
}

// [Objective] A (Torn Write): Power Cut during a slot program. 
void test_torn_write_recovery(void) {
    mock_power_cut_after_ops = 1; // Cut power exactly after the erase
    
    ht_init();
    uint8_t dummy_it[HT_BEACON_SIZE] = {0};
    for (int i = 0; i < 6; i++) fill_ht_from_it(dummy_it);
    
    mock_power_cut_after_ops = -1; // Restore power
    ht_init();
    
    TEST_ASSERT_EQUAL_MESSAGE(0, ht_queue.writing_pointer, "CRITICAL BUG: ht_init accepted a torn (half-written) slot!");
}

// [Objective] #39: RAM buffer lost at reset. 
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

int main(void) {
    UNITY_BEGIN();
    
    // 1. Boot
    RUN_TEST(test_init_virgin_flash);
    RUN_TEST(test_init_finds_correct_pointers);
    RUN_TEST(test_init_full_and_wrapped);
    RUN_TEST(test_init_seq_counter_wrap);
    RUN_TEST(test_init_ignores_0xFFFFFFFF);
    RUN_TEST(test_init_duplicate_seq);
    RUN_TEST(test_init_corrupt_middle_slot);
    
    // 2. Logic
    RUN_TEST(test_erase_only_on_page_boundaries);
    RUN_TEST(test_queue_wraparound);
    RUN_TEST(test_rp_in_another_page);
    
    // 3. Orbit-Killer Bugs
    RUN_TEST(test_eviction_jumps_page);
    RUN_TEST(test_bad_sector_deadlock);
    RUN_TEST(test_torn_write_recovery);
    RUN_TEST(test_ram_loss_at_reset);
    
    return UNITY_END();
}
