/**
 * @file hk_history.h
 * @brief What an HK history downlink sends: the stored telemetry-history blocks from a sequence number on, copied to RAM when the telecommand arrives.
 * @details
 * TC_REQUEST_HK_HISTORY names FROM_SEQ, the first block the ground wants. The
 * comms task copies the stored blocks numbered FROM_SEQ or later into RAM,
 * oldest first, while the largest block still fits HK_HISTORY_MAX bytes. The
 * ARQ session sends that copy, resends from it and computes its CRC from it,
 * so nothing is read from flash while it runs (docs/FLASH-USAGE-SPECIFICATION.md §5.4).
 *
 * | Bytes           | Content |
 * |-----------------|---------|
 * | 0               | HT_FORMAT: how the blocks decode (ht_store.h) |
 * | then, per block | SEQ (4 bytes, big-endian), LEN (1 byte), then the LEN bytes of the block as stored |
 *
 * The ground asks for the rest with FROM_SEQ = the last SEQ + 1; FROM_SEQ 0
 * gives the oldest block stored. A number missing between two records is a
 * block no longer stored: dropped, damaged, or overwritten by the ring. The
 * block being filled is not stored yet, so it is never sent.
 * @date 2026-11-21
 */

#ifndef INC_HK_HISTORY_H_
#define INC_HK_HISTORY_H_

#include <stdint.h>

#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"

#define HK_HISTORY_MAX            2048u   /**< Most bytes one HK history session sends. */
#define HK_HISTORY_RECORD_HEADER  5u      /**< SEQ and LEN, before each block. */

/**
 * @brief Copies the stored blocks numbered from_seq or later into buf, oldest first, through the OBDH task.
 * @details Reads one block at a time with ht_read_request(), each from the
 *          number last read plus one, and stops when no block is left, when
 *          the largest block might not fit, or when the block time has run
 *          out: ticks_to_wait counts once, for all the reads together. A read
 *          that fails after the first one ends the copy with what was read.
 *          The OBDH task never calls it: it would wait for itself.
 * @param from_seq First sequence number wanted.
 * @param buf Receives the copy; HK_HISTORY_MAX bytes.
 * @param len Receives its length; written only on HAL_OK.
 * @param ticks_to_wait Block time for all the reads; normally OBDH_REQUEST_WAIT (obdh_requests.h).
 * @return HAL_OK with at least one block; otherwise what the first read
 *         returned: HAL_ERROR if no block numbered from_seq or later is
 *         stored, HAL_BUSY or HAL_TIMEOUT if OBDH did not answer in time.
 */
HAL_StatusTypeDef hk_history_request(uint32_t from_seq, uint8_t buf[HK_HISTORY_MAX], uint32_t *len, TickType_t ticks_to_wait);

#endif /* INC_HK_HISTORY_H_ */
