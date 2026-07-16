/**
 * @file ht_handling.h
 * @brief Historic telemetry (HT) circular storage of POCKET+ compressed beacons.
 * @details
 * Instant telemetry (IT) beacons are compressed with POCKET+ into an
 * in-progress block as they arrive (fill-driven): each block starts with a
 * raw reference beacon and grows one compressed beacon at a time until the
 * next one no longer fits the slot, and is then written to a fixed-size slot
 * of a circular queue in internal flash. Note this means the better the
 * compression, the more history sits in RAM only (up to HT_SLOT_MAX_BEACONS
 * beacons) and is lost if power drops before the block reaches flash.
 *
 * This module owns ALL historic-telemetry knowledge: staging, compression,
 * the slot format, and the circular-queue state and policy.
 * 
 * Slot layout (HT_SLOT_SIZE bytes, one slot per block):
 *   [0..11]  ht_slot_header (epoch of first beacon, monotonic block sequence
 *            number, payload length, beacon count, format marker)
 *   [12..]   payload:
 *            - reference beacon, raw (HT_STORED_PACKET_SIZE bytes, first beacon of
 *              the block; POCKET+ decompression must start from it)
 *            - the remaining beacons' compressed packets appended back-to-back
 *              with no framing: the POCKET+ bitstream is self-delimiting, the
 *              ground decompressor (decompression.cpp, decompressPacket()
 *              returns the bytes consumed) walks the chain packet by packet
 *
 * Wear strategy: a page is erased once, when the write pointer enters it
 * (dropping the oldest blocks a full queue was about to overwrite anyway),
 * as a single erase+program OBDH request; the slots inside are then
 * programmed individually without further erases (FLASH_PROGRAM requests),
 * i.e. one erase per page per queue lap.
 */

#ifndef __HT_HANDLING_H__
#define __HT_HANDLING_H__

#include <stdint.h>
#include <assert.h>

#include "flash.h" /* HT_BASE_ADDR / HT_REGION_SIZE (flash memory map) */

#define POCKET_PLUS_UPDATE_RATE 12 /**< POCKET+ positive-mask update rate (packets between mask history refreshes). Must match the ground decompressor's setting -- NOT a block-size knob. */

#define HT_BEACON_SIZE        19u /**< Beacon body size as built by build_beacon_body(). */
#define HT_STORED_PACKET_SIZE 20u /**< Size of one packet as stored in flash and fed to POCKET+: the beacon body zero-padded to a multiple of 4 (POCKET+ works on 32-bit words). */

#define HT_SLOT_SIZE         128u /**< Fixed flash slot size per block. */
#define HT_SLOT_HEADER_SIZE  12u  /**< sizeof(ht_slot_header). */
#define HT_SLOT_PAYLOAD_SIZE (HT_SLOT_SIZE - HT_SLOT_HEADER_SIZE)
#define HT_SLOT_MAX_BEACONS  (1u + (HT_SLOT_PAYLOAD_SIZE - HT_STORED_PACKET_SIZE)) /**< Physical bound on a block's beacon count: raw reference + at least 1 byte per compressed packet. */

#define HT_SLOTS_PER_PAGE    (FLASH_PAGE_SIZE / HT_SLOT_SIZE)
#define HT_NUM_SLOTS         (HT_REGION_SIZE / HT_SLOT_SIZE) /**< Derived from the region reserved in flash.h. */

#define HT_FORMAT_POCKET_V3  0x03u /**< Slot format marker (V3: compressed packets stored without length prefixes); erased flash reads 0xFF. */

_Static_assert((FLASH_PAGE_SIZE % HT_SLOT_SIZE) == 0u, "HT_SLOT_SIZE must divide the flash page size exactly");
_Static_assert((HT_BASE_ADDR % FLASH_PAGE_SIZE) == 0u, "HT_BASE_ADDR must be flash page aligned");
_Static_assert((HT_REGION_SIZE % FLASH_PAGE_SIZE) == 0u, "HT_REGION_SIZE must be a whole number of flash pages");
_Static_assert(HT_REGION_SIZE >= FLASH_PAGE_SIZE, "HT region must be at least one flash page");
_Static_assert((HT_BASE_ADDR >= 0x08000000u) && ((HT_BASE_ADDR + HT_REGION_SIZE) <= 0x08100000u), "HT region must lie inside the 1 MB internal flash");
_Static_assert(HT_SLOT_MAX_BEACONS <= 255u, "block beacon count must fit the uint8_t header field");
_Static_assert((HT_STORED_PACKET_SIZE % 4u) == 0u, "stored packets must be whole 32-bit words (POCKET+ and the word packing require it)");
_Static_assert(HT_BEACON_SIZE <= HT_STORED_PACKET_SIZE, "the beacon body must fit the stored packet");

/**
 * @brief Header stored at the start of every occupied flash slot.
 * @todo Revise what the header actually needs to contain. This version is too detailed.
 */
typedef struct __attribute__ ((__packed__)) {
    uint32_t epoch;    /**< Unix time of the block's first (reference) beacon. */
    uint32_t seq;      /**< Monotonic block sequence number, stamped by ht_flush_block(); orders blocks at recovery independently of the RTC. */
    uint16_t data_len; /**< Payload bytes used (reference + compressed entries). */
    uint8_t  count;    /**< Beacons stored in this block (1..HT_SLOT_MAX_BEACONS). */
    uint8_t  format;   /**< HT_FORMAT_POCKET_V3 when valid; 0xFF in an erased slot. */
} ht_slot_header;

_Static_assert(sizeof(ht_slot_header) == HT_SLOT_HEADER_SIZE, "ht_slot_header must match HT_SLOT_HEADER_SIZE");

/**
 * @brief Rebuild the circular-queue state by scanning the slot headers in flash.
 *
 * Must be called once, from the task that owns historic telemetry (beacon task)
 * at startup.
 */
void ht_init(void);

/**
 * @brief Take an instant telemetry (IT) beacon, compress it into the
 * in-progress block, and flush the block to flash when this beacon no longer
 * fits the slot (the flush blocks until the OBDH flash requests complete).
 *
 * Must always be called from the same task: the POCKET+ compressor keeps
 * static state and is not reentrant, and the queue state is unlocked.
 * Called by the beacon task.
 *
 * @param it Instant telemetry beacon body (HT_BEACON_SIZE bytes).
 */
void fill_ht_from_it(const uint8_t *it);

#endif /* __HT_HANDLING_H__ */
