/**
 * @file ht_handling.c
 * @brief Historic telemetry staging, POCKET+ compression and circular flash storage.
 * @details This module owns the circular-queue state.
 * @date 2026-05-12
 * @todo The beacons are currently being stored in compressed form, but
 * the layout in which they are stored is the worst case scenario for POCKET+ compression.
 * This should be revised.
 * @todo using safeStartPocket() right now, but look into default startPocket.
 * @todo What should we do in the case in which the compressed packets are larger than the uncompressed packets?
 * @todo Revise headers
 */

#include "ht_handling.h"
#include "obdh_requests.h"
#include "flash.h" /* memory map only (HT_BASE_ADDR / HT_REGION_SIZE / FLASH_PAGE_SIZE); the driver is never called from here */
#include "compression.h"
#include <string.h>
#include <stdio.h>

static uint8_t  ht_block[HT_SLOT_SIZE];   /* The block being filled */
static uint16_t ht_block_used = 0;        /* payload bytes used */
static uint8_t  ht_block_count = 0;       /* beacons in the open block */
static int      ht_block_compressing = 0; /* records whether safeStartPocket() succeeded with the current block */

/**
 * @brief RAM-only circular queue state (rebuilt by ht_init(), never persisted).
 */
static struct {
    int current_ht_count; /**< Number of occupied slots. */
    int reading_pointer;  /**< Slot index of the oldest block. */
    int writing_pointer;  /**< Slot index the next block will be written to. */
    uint32_t next_seq;    /**< Sequence number the next block will be stamped with. */
} ht_queue;

/**
 * @brief Pack a byte buffer into the big-endian 32-bit word array POCKET+ expects.
 * @details size must be a multiple of 4
 *          (beacons are zero-padded to HT_STORED_PACKET_SIZE for this).
 * @return Length of the word array.
 */
static uint_fast16_t pocket_plus_get_binary_data(const uint8_t *bytes, uint32_t *words, uint_fast16_t size)
{
    uint_fast16_t n_words = size / 4;

    for (uint_fast16_t w = 0; w < n_words; w++) {
        words[w] = ((uint32_t)bytes[4 * w]     << 24) |
                   ((uint32_t)bytes[4 * w + 1] << 16) |
                   ((uint32_t)bytes[4 * w + 2] << 8)  |
                    (uint32_t)bytes[4 * w + 3];
    }
    return n_words;
}

/**
 * @brief Check that a slot header read from flash describes a valid block.
 */
static int ht_slot_header_valid(const ht_slot_header *header)
{
    return (header->format == HT_FORMAT_POCKET_V3) && (header->count >= 1) && (header->count <= HT_SLOT_MAX_BEACONS) &&
           (header->data_len >= HT_STORED_PACKET_SIZE) && (header->data_len <= HT_SLOT_PAYLOAD_SIZE);
}

/**
 * @brief Drop from the queue accounting any occupied blocks contained in the
 * flash page holding the given slot (the oldest ones, which a full circular
 * queue was about to overwrite anyway).
 */
static void ht_account_page_erase(int slot_index)
{
    int page_first = slot_index - (slot_index % (int)HT_SLOTS_PER_PAGE);

    int lost = 0;
    for (int s = page_first; s < page_first + (int)HT_SLOTS_PER_PAGE; s++) {
        int offset = (s - ht_queue.reading_pointer + (int)HT_NUM_SLOTS) % (int)HT_NUM_SLOTS;
        if (offset < ht_queue.current_ht_count)
            lost++;
    }
    if (lost > 0) {
        ht_queue.current_ht_count -= lost;
        ht_queue.reading_pointer = (page_first + (int)HT_SLOTS_PER_PAGE) % (int)HT_NUM_SLOTS;
    }
}

/**
 * @brief Store one built block in the circular queue through OBDH flash
 * requests (the calls block until OBDH completes them).
 *
 * A FLASH_PROGRAM failure means the target slot was not erased: 
 * skip to the next page boundary, which takes the
 * erase+program path, and retry once there.
 *
 * @param slot Finished HT_SLOT_SIZE block, header already included.
 * @return 0 on success, -1 on failure.
 */
static int ht_store_block(const uint8_t *slot)
{
    int written = 0;
    for (uint8_t attempt = 0; (attempt < 2) && !written; attempt++) {
        uint32_t addr = HT_BASE_ADDR + (uint32_t)ht_queue.writing_pointer * HT_SLOT_SIZE;
        if ((ht_queue.writing_pointer % (int)HT_SLOTS_PER_PAGE) == 0) {
            ht_account_page_erase(ht_queue.writing_pointer);
            if (obdh_erase_program_request(addr, slot, HT_SLOT_SIZE) != HAL_OK)
                return -1;
            written = 1;
        } else {
            if (obdh_program_request(addr, slot, HT_SLOT_SIZE) == HAL_OK) {
                written = 1;
            } else {
                ht_queue.writing_pointer = (((ht_queue.writing_pointer / (int)HT_SLOTS_PER_PAGE) + 1)
                                            * (int)HT_SLOTS_PER_PAGE) % (int)HT_NUM_SLOTS; // go to the next page and retry
            }
        }
    }
    if (!written)
        return -1;

    ht_queue.next_seq++;
    ht_queue.writing_pointer = (ht_queue.writing_pointer + 1) % (int)HT_NUM_SLOTS;
    if (ht_queue.current_ht_count < (int)HT_NUM_SLOTS) {
        ht_queue.current_ht_count++;
    }
    return 0;
}

/**
 * @brief Close the in-progress block and store it in the circular queue
 * (ht_store_block(), via OBDH flash requests).
 *
 * Stamps the header (epoch taken from the raw reference beacon at the start
 * of the payload, seq from the queue state) and resets the staging state.
 * The block is discarded on store failure too, so staging never backs up.
 *
 * @return 0 on success or nothing to flush, -1 if the store failed.
 */
static int ht_flush_block(void)
{
    if (ht_block_count == 0)
        return 0;

    const uint8_t *ref = ht_block + HT_SLOT_HEADER_SIZE;
    ht_slot_header header = {
        .epoch    = ((uint32_t)ref[1] << 24) | ((uint32_t)ref[2] << 16) |
                    ((uint32_t)ref[3] << 8)  |  (uint32_t)ref[4],
        .seq      = ht_queue.next_seq,
        .data_len = ht_block_used,
        .count    = ht_block_count,
        .format   = HT_FORMAT_POCKET_V3,
    };
    memcpy(ht_block, &header, HT_SLOT_HEADER_SIZE);

    int result = ht_store_block(ht_block);

    if (result == 0)
        printf("HT: block stored, %u beacons in %u payload bytes\n",
               (unsigned)header.count, (unsigned)header.data_len);
    else
        printf("HT: block store FAILED, %u beacons dropped\n", (unsigned)header.count);

    ht_block_used = 0;
    ht_block_count = 0;
    ht_block_compressing = 0;
    return result;
}

void ht_init(void)
{
    uint32_t newest_seq = 0;
    uint32_t oldest_seq = 0;
    int newest_slot = -1;
    int oldest_slot = -1;
    int used = 0;

    /* Discard any half-built block */
    ht_block_used = 0;
    ht_block_count = 0;
    ht_block_compressing = 0;

    for (int i = 0; i < (int)HT_NUM_SLOTS; i++) {
        ht_slot_header header;

        if (obdh_read_request(HT_BASE_ADDR + (uint32_t)i * HT_SLOT_SIZE, (uint8_t *)&header, sizeof(header)) != HAL_OK)
            continue;

        if (!ht_slot_header_valid(&header))
            continue;

        used++;
        
        if (newest_slot < 0 || header.seq > newest_seq) {
            newest_seq = header.seq;
            newest_slot = i;
        }
        if (oldest_slot < 0 || header.seq < oldest_seq) {
            oldest_seq = header.seq;
            oldest_slot = i;
        }
    }

    ht_queue.current_ht_count = used;
    if (used == 0) {
        ht_queue.reading_pointer = 0;
        ht_queue.writing_pointer = 0;
        ht_queue.next_seq = 0;
    } else {
        ht_queue.reading_pointer = oldest_slot;
        ht_queue.writing_pointer = (newest_slot + 1) % HT_NUM_SLOTS;
        ht_queue.next_seq = newest_seq + 1u;
    }

}

void fill_ht_from_it(const uint8_t *it)
{
    uint8_t beacon[HT_STORED_PACKET_SIZE];
    uint32_t words[HT_STORED_PACKET_SIZE / 4];

    /* Zero-pad the raw body to HT_STORED_PACKET_SIZE */
    memset(beacon, 0, sizeof(beacon));
    memcpy(beacon, it, HT_BEACON_SIZE);
    pocket_plus_get_binary_data(beacon, words, HT_STORED_PACKET_SIZE);

    /* Append to the open block while the compressed beacon still fits */
    if ((ht_block_count > 0) && ht_block_compressing) {
        uint8_t cbuf[HT_STORED_PACKET_SIZE * 4]; /* compressPacket needs 4x the packet size */
        int_fast16_t clen = compressPacket(cbuf, words);

        if ((clen >= 0) && ((uint16_t)(ht_block_used + clen) <= HT_SLOT_PAYLOAD_SIZE)) {
            uint8_t *payload = ht_block + HT_SLOT_HEADER_SIZE;

            memcpy(&payload[ht_block_used], cbuf, (size_t)clen);
            ht_block_used += (uint16_t)clen;
            ht_block_count++;
            return;
        }
    }

    ht_flush_block();

    memset(ht_block, 0xFF, sizeof(ht_block));
    memcpy(ht_block + HT_SLOT_HEADER_SIZE, beacon, HT_STORED_PACKET_SIZE);
    ht_block_used = HT_STORED_PACKET_SIZE;
    ht_block_count = 1;
    ht_block_compressing = (safeStartPocket(words, HT_STORED_PACKET_SIZE, POCKET_PLUS_UPDATE_RATE) != 0);
}