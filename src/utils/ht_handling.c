/**
 * @file ht_handling.c
 * @brief Historic telemetry staging, POCKET+ compression and circular flash storage.
 * @details This module owns the circular-queue state.
 * @date 2026-05-12
 * @todo The beacons are currently being stored in compressed form, but
 * the layout in which they are stored is the worst case scenario for POCKET+ compression.
 * This should be revised.
 */

#include "ht_handling.h"
#include "obdh_requests.h"
#include "flash.h" /* memory map only (HT_BASE_ADDR / HT_REGION_SIZE / FLASH_PAGE_SIZE); the driver is never called from here */
#include "compression.h"
#include <string.h>
#include <stdio.h>

static uint8_t ht_temporal_buffer[POCKET_PLUS_PERIOD][HT_BEACON_SIZE];
static uint8_t temporal_count = 0;

/**
 * @brief RAM-only circular queue state (rebuilt by ht_init(), never persisted).
 *
 * Private to this module. Unlocked: only ever touched from the single task
 * that calls ht_init() and fill_ht_from_it() -- see the concurrency note in
 * ht_handling.h.
 */
typedef struct {
    int current_ht_count; /**< Number of occupied slots. */
    int reading_pointer;  /**< Slot index of the oldest block. */
    int writing_pointer;  /**< Slot index the next block will be written to. */
    uint32_t next_seq;    /**< Sequence number the next block will be stamped with. */
} CircularFlashHandler;

static CircularFlashHandler ht_queue;

/**
 * @brief Pack a byte buffer into the big-endian 32-bit word array POCKET+ expects.
 * @return Length of the word array.
 */
static uint_fast16_t pocket_plus_get_binary_data(const uint8_t *binaryBuffer, uint32_t *binaryData, uint_fast16_t size)
{
    uint_fast16_t arrayLenght = (size + 3) / 4; //lenght for the 32 bit uint32_t array.
    memset(binaryData, 0, sizeof(*binaryData) * arrayLenght);

    uint_fast8_t i = 0; //counter the bitshift. (i = 0 -> bitshift 8, i = 1 -> bitshift 16 ...) must be reseted when reaching 4
    uint32_t bytesToInt = 0; //variable for casting 4 bytes into one 32 bit unsigned integer
    int_fast16_t currentWord = arrayLenght - 1;

    for (int_fast32_t currentByte = size - 1; currentByte >= 0; currentByte--) {

        bytesToInt |= binaryBuffer[currentByte] << (i * 8);
        i += 1;
        if (i == 4) {
            memcpy(binaryData + currentWord, &bytesToInt, sizeof(*binaryData));
            bytesToInt = 0;
            i = 0;
            currentWord -= 1;
        }
    }
    if ((i < 4) && (currentWord == 0)) {
        memcpy(binaryData + currentWord, &bytesToInt, sizeof(*binaryData));
    }
    return arrayLenght;
}

/**
 * @brief Check that a slot header read from flash describes a valid block.
 */
static int ht_slot_header_valid(const ht_slot_header *header)
{
    return (header->format == HT_FORMAT_POCKET_V2) && (header->count >= 1) && (header->count <= POCKET_PLUS_PERIOD) &&
           (header->data_len >= HT_BEACON_SIZE) && (header->data_len <= HT_SLOT_PAYLOAD_SIZE);
}

/**
 * @brief Drop from the queue accounting any occupied blocks contained in the
 * flash page holding the given slot (the oldest ones, which a full circular
 * queue was about to overwrite anyway).
 *
 * Pure accounting -- the actual erase is requested from OBDH by the caller.
 * Called when a page erase is decided, BEFORE the request is submitted:
 * conservative on failure (blocks in a possibly-not-erased page are still
 * dropped; the boot scan restores flash truth) and idempotent, so a failed
 * store retried at the same page does not double-count.
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
 * Picks the slot from the queue state. On entering a fresh page the store is
 * one FLASH_ERASE_PROGRAM request -- erase and program as a single OBDH
 * operation, so the page pays one erase per queue lap and nothing can
 * interleave between the two. Mid-page slots are plain FLASH_PROGRAM requests
 * into space erased when the page was entered.
 *
 * A FLASH_PROGRAM failure means the target slot was not erased (e.g. torn by
 * a power cut mid-write): skip to the next page boundary, which takes the
 * erase+program path, and retry once there. A FLASH_ERASE_PROGRAM failure is
 * a hardware-level error: the store is aborted (writing_pointer unchanged, so
 * the next store retries the same page).
 *
 * @param slot Built HT_SLOT_SIZE block, seq already stamped.
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
                                            * (int)HT_SLOTS_PER_PAGE) % (int)HT_NUM_SLOTS;
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
 * @brief Compress the staged beacons into block(s) and store them in the
 * circular queue (ht_store_block(), via OBDH flash requests).
 *
 * Called from fill_ht_from_it() once POCKET_PLUS_PERIOD beacons are staged.
 * The first staged beacon becomes the POCKET+ reference (stored raw); the
 * rest are appended compressed. If a block fills up (poor compression), the
 * remaining beacons start a new block so no beacon is ever dropped by
 * compression overflow.
 *
 * @return 0 on success, -1 if a store failed (staged beacons are discarded
 *         either way so staging never overflows).
 */
static int save_ht_to_circular_storage(void)
{
    uint8_t start = 0;
    int result = 0;

    while (start < temporal_count) {
        uint8_t slot[HT_SLOT_SIZE];
        uint8_t *payload = slot + HT_SLOT_HEADER_SIZE;
        uint16_t used = 0;
        uint8_t count = 0;

        memset(slot, 0xFF, sizeof(slot)); /* unused tail keeps the erased pattern */

        /* First beacon of the block goes in raw: it is the POCKET+ reference
           the decompressor needs to bootstrap the stream. */
        memcpy(payload, ht_temporal_buffer[start], HT_BEACON_SIZE);
        used = HT_BEACON_SIZE;
        count = 1;

        uint32_t words[HT_BEACON_SIZE / 4];
        pocket_plus_get_binary_data(ht_temporal_buffer[start], words, HT_BEACON_SIZE);

        if (safeStartPocket(words, HT_BEACON_SIZE, POCKET_PLUS_PERIOD) != 0) {
            for (uint8_t i = start + 1; i < temporal_count; i++) {
                uint8_t cbuf[HT_BEACON_SIZE * 4]; /* compressPacket needs 4x the packet size */

                pocket_plus_get_binary_data(ht_temporal_buffer[i], words, HT_BEACON_SIZE);
                int_fast16_t clen = compressPacket(cbuf, words);

                /* Compression overflow or slot full: close this block and let
                   the remaining beacons start the next one (with a fresh
                   reference), so nothing is dropped. */
                if ((clen < 0) || ((uint16_t)(used + 1 + clen) > HT_SLOT_PAYLOAD_SIZE))
                    break;

                payload[used] = (uint8_t)clen;
                memcpy(&payload[used + 1], cbuf, (size_t)clen);
                used += 1 + (uint16_t)clen;
                count++;
            }
        }

        const uint8_t *ref = ht_temporal_buffer[start];
        ht_slot_header header = {
            .epoch    = ((uint32_t)ref[1] << 24) | ((uint32_t)ref[2] << 16) |
                        ((uint32_t)ref[3] << 8)  |  (uint32_t)ref[4],
            .seq      = ht_queue.next_seq, 
            .data_len = used,
            .count    = count,
            .format   = HT_FORMAT_POCKET_V2,
        };
        memcpy(slot, &header, HT_SLOT_HEADER_SIZE);

        if (ht_store_block(slot) != 0) {
            result = -1;
            break;
        }

        start += count;
    }

    return result;
}

void ht_init(void)
{
    uint32_t newest_seq = 0;
    uint32_t oldest_seq = 0;
    int newest_slot = -1;
    int oldest_slot = -1;
    int used = 0;

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
    memset(ht_temporal_buffer[temporal_count], 0, HT_BEACON_SIZE);
    memcpy(ht_temporal_buffer[temporal_count], it, HT_BEACON_RAW_SIZE);
    temporal_count++;

    if (temporal_count >= POCKET_PLUS_PERIOD) {
        save_ht_to_circular_storage();
        temporal_count = 0;
    }
}