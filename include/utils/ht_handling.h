/**
 * @file ht_handling.h
 * @brief Historic telemetry (HT) circular storage of POCKET+ compressed beacons.
 * @details
 * Instant telemetry (IT) beacons are staged in RAM. Every POCKET_PLUS_PERIOD
 * beacons they are compressed with POCKET+ into one block and
 * written to a fixed-size slot of a circular queue in internal flash.
 *
 * This module owns ALL historic-telemetry knowledge: staging, compression,
 * the slot format, and the circular-queue state and policy.
 * 
 * Slot layout (HT_SLOT_SIZE bytes, one slot per block):
 *   [0..11]  ht_slot_header (epoch of first beacon, monotonic block sequence
 *            number, payload length, beacon count, format marker)
 *   [12..]   payload:
 *            - reference beacon, raw (HT_BEACON_SIZE bytes, first beacon of
 *              the block; POCKET+ decompression must start from it)
 *            - per remaining beacon: 1-byte compressed length + compressed
 *              packet produced by compressPacket()
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

#define POCKET_PLUS_PERIOD   12   /**< Beacons accumulated per compressed block. */

#define HT_BEACON_RAW_SIZE   19u  /**< Beacon body size as built by build_beacon_body(). */
#define HT_BEACON_SIZE       20u  /**< Stored beacon size: raw body zero-padded to a multiple of 4 (POCKET+ works on 32-bit words). */

#define HT_SLOT_SIZE         128u /**< Fixed flash slot size per block. */
#define HT_SLOT_HEADER_SIZE  12u  /**< sizeof(ht_slot_header). */
#define HT_SLOT_PAYLOAD_SIZE (HT_SLOT_SIZE - HT_SLOT_HEADER_SIZE)

#define HT_SLOTS_PER_PAGE    (FLASH_PAGE_SIZE / HT_SLOT_SIZE)
#define HT_NUM_SLOTS         (HT_REGION_SIZE / HT_SLOT_SIZE) /**< Derived from the region reserved in flash.h. */

#define HT_FORMAT_POCKET_V2  0x02u /**< Slot format marker; erased flash reads 0xFF. */

_Static_assert((FLASH_PAGE_SIZE % HT_SLOT_SIZE) == 0u, "HT_SLOT_SIZE must divide the flash page size exactly");
_Static_assert((HT_BASE_ADDR % FLASH_PAGE_SIZE) == 0u, "HT_BASE_ADDR must be flash page aligned");
_Static_assert((HT_REGION_SIZE % FLASH_PAGE_SIZE) == 0u, "HT_REGION_SIZE must be a whole number of flash pages");
_Static_assert(HT_REGION_SIZE >= FLASH_PAGE_SIZE, "HT region must be at least one flash page");
_Static_assert((HT_BASE_ADDR >= 0x08000000u) && ((HT_BASE_ADDR + HT_REGION_SIZE) <= 0x08100000u), "HT region must lie inside the 1 MB internal flash");

/**
 * @brief Header stored at the start of every occupied flash slot.
 * @todo Revise what the header actually needs to contain. This version is too detailed.
 */
typedef struct __attribute__ ((__packed__)) {
    uint32_t epoch;    /**< Unix time of the block's first (reference) beacon. */
    uint32_t seq;      /**< Monotonic block sequence number, stamped by OBDH at store time; orders blocks at recovery independently of the RTC. */
    uint16_t data_len; /**< Payload bytes used (reference + compressed entries). */
    uint8_t  count;    /**< Beacons stored in this block (1..POCKET_PLUS_PERIOD). */
    uint8_t  format;   /**< HT_FORMAT_POCKET_V2 when valid; 0xFF in an erased slot. */
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
 * @brief Take an instant telemetry (IT) beacon, accumulate it in RAM and when
 * POCKET_PLUS_PERIOD beacons are staged, compress and store them in flash.
 *
 * Must always be called from the same task: the POCKET+ compressor keeps
 * static state and is not reentrant, and the queue state is unlocked. 
 * Called by the beacon task.
 *
 * @param it Instant telemetry beacon body (HT_BEACON_RAW_SIZE bytes).
 */
void fill_ht_from_it(const uint8_t *it);

#endif /* __HT_HANDLING_H__ */
