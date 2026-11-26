/**
 * @file ring.h
 * @brief Ring store: blocks in fixed-size slots of a flash region, the oldest overwritten page by page.
 * @details
 * Used by the stores that keep blocks in a ring: the telemetry history, the
 * payload results and the OBC event log. Each store owns one ring_t and calls
 * these functions from the OBDH task only.
 *
 * Slot layout (little-endian):
 *
 *     Offset          Type      Field          Description
 *     0               uint16_t  check          Low 16 bits of the CRC-32 of len and seq
 *     2               uint16_t  len            Data length in bytes
 *     4               uint32_t  seq            Sequence number
 *     8               uint8_t   data[len]      Padded with 0xFF to a multiple of 8 bytes
 *     slot_size - 8   uint32_t  crc            CRC-32 of the header and data[len], programmed last
 *     slot_size - 4   uint32_t  crc_inverted   Bitwise complement of crc
 *
 * The bytes between the data and the trailer stay erased. A slot holds a block
 * only if check passes, len fits the slot and the trailer matches.
 *
 * Blocks go to consecutive slots, and the writer erases each page as it enters
 * it. If a page fails to erase, or a slot fails to program or read back, the
 * writer moves to the start of the next page. Each attempt takes the next
 * sequence number; numbers wrap from 0xFFFFFFFF to 0 and are compared by their
 * difference.
 *
 * No position is stored: ring_load() finds the newest block from the headers.
 * Slots don't record which store or format they hold, so a change of format or
 * of the memory map needs the region erased first.
 * @date 2026-11-21
 */

#ifndef INC_RING_H_
#define INC_RING_H_

#include <stdint.h>

#include "stm32l4xx_hal.h"
#include "flash_map.h"

#define RING_HEADER_SIZE   8u   /**< Header at the start of each slot. */
#define RING_TRAILER_SIZE  8u   /**< Trailer at the end of each slot. */

/** @brief Most data one block of a slot_size-byte slot holds. */
#define RING_DATA_MAX(slot_size)  ((slot_size) - RING_HEADER_SIZE - RING_TRAILER_SIZE)

/** @brief True if slot_size is a multiple of 8, divides a page and leaves room for data; check it with _Static_assert. */
#define RING_SLOT_SIZE_VALID(slot_size) \
    ((((slot_size) % 8u) == 0u) && ((slot_size) > RING_HEADER_SIZE + RING_TRAILER_SIZE) && \
     ((FLASH_MAP_PAGE_SIZE % (slot_size)) == 0u))

/** @brief What a store fixes about its ring. */
typedef struct {
    flash_region_id_t region;   /**< Region the ring fills; no other store uses it. */
    uint32_t slot_size;         /**< Bytes per slot (RING_SLOT_SIZE_VALID). */
} ring_config_t;

/** @brief Counters since boot; ring_load() keeps them. */
typedef struct {
    uint32_t appended;          /**< Blocks stored. */
    uint32_t dropped;           /**< Blocks not stored: both places an append tried failed. */
    uint32_t erase_failures;    /**< Page erases that failed; the page was skipped. */
    uint32_t program_failures;  /**< Slots that failed to program or to read back; the rest of their page was skipped. */
} ring_stats_t;

/** @brief One ring: the store sets config, this module keeps the rest. */
typedef struct {
    ring_config_t config;
    uint32_t slots;             /**< Slots in the region. */
    uint32_t write;             /**< Slot the next block goes to. */
    uint32_t next_seq;          /**< Sequence number of the next attempt to program a slot. */
    ring_stats_t stats;
} ring_t;

/**
 * @brief Reads every slot header to find the newest block; the next block goes after it.
 * @details Call it each time the OBDH task starts. Reads flash only and keeps
 *          the counters. An empty ring starts at slot 0 with sequence number 0.
 * @param ring The ring, with its config set.
 */
void ring_load(ring_t *ring);

/**
 * @brief Stores one block in the next slot, erasing the page first when the slot starts one.
 * @details If the erase, the program or the read-back fails, it tries once more
 *          at the start of the next page; if that fails too, the block is dropped.
 * @param ring A loaded ring.
 * @param data The block.
 * @param len Its length, at most RING_DATA_MAX(slot_size).
 * @return HAL_OK if the block is stored; HAL_ERROR if it was dropped or is too long (then nothing is written).
 */
HAL_StatusTypeDef ring_append(ring_t *ring, const uint8_t *data, uint32_t len);

/**
 * @brief Reads the oldest stored block numbered from_seq or later.
 * @details Missing numbers are skipped, so reading on from the returned number
 *          plus one returns every stored block in order.
 * @param ring A loaded ring.
 * @param from_seq First sequence number wanted.
 * @param data Receives the block; room for RING_DATA_MAX(slot_size) bytes.
 * @param len Receives its length.
 * @param seq Receives its sequence number.
 * @return HAL_OK; HAL_ERROR if no such block is stored. data, len and seq are valid only on HAL_OK.
 */
HAL_StatusTypeDef ring_read(const ring_t *ring, uint32_t from_seq, uint8_t *data, uint32_t *len, uint32_t *seq);

/**
 * @brief Reads the oldest stored block.
 * @param ring A loaded ring.
 * @param data Receives the block; room for RING_DATA_MAX(slot_size) bytes.
 * @param len Receives its length.
 * @param seq Receives its sequence number.
 * @return HAL_OK; HAL_ERROR if the ring holds no block. data, len and seq are valid only on HAL_OK.
 */
HAL_StatusTypeDef ring_read_oldest(const ring_t *ring, uint8_t *data, uint32_t *len, uint32_t *seq);

#endif /* INC_RING_H_ */
