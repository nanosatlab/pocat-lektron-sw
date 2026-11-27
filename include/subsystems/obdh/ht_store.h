/**
 * @file ht_store.h
 * @brief Telemetry history (HT): beacons compressed with POCKET+ into blocks kept in a ring, at most one sample per 30 s on average.
 * @details
 * The beacon task posts each beacon with ht_beacon_post() and goes on without
 * waiting. The OBDH task runs the store (docs/FLASH-USAGE-SPECIFICATION.md §5.4):
 *
 * - Rate (FLS-RATE-020). A beacon becomes a sample only when the credit
 *   allows it. Each sample costs HT_SAMPLE_PERIOD_S seconds of credit; the
 *   credit grows by one second per second of uptime, up to HT_CREDIT_MAX_S,
 *   and is 0 at boot (O21). So in any T seconds, resets included, at most
 *   2 + T / HT_SAMPLE_PERIOD_S samples are stored, whatever the beacon period
 *   and the beacons sent on request. Beacons of a periodic timer at
 *   HT_SAMPLE_PERIOD_S or slower are all stored, however late each one arrives:
 *   a timer never fires early.
 * - Blocks. A block starts with its first sample raw, then holds each later
 *   sample compressed with POCKET+ against the earlier ones, back to back.
 *   When a sample no longer fits, the block goes to the ring and the sample
 *   starts the next block. Each block decodes on its own.
 * - Ring (ring.h). 128-byte slots in FLASH_REGION_HT: up to HT_BLOCK_MAX bytes
 *   of data per block, one erase per page per lap, a CRC trailer programmed last.
 *
 * The block being filled is in RAM only: a power cut, or a restart of the
 * OBDH task, loses it (up to one block of samples).
 *
 * Block data, as ht_read_request() returns it:
 * | Bytes                  | Content |
 * |------------------------|---------|
 * | 0 to 19                | The first sample: the 19-byte beacon body, then one zero byte |
 * | 20 to the block length | Each later sample compressed by POCKET+, a whole number of bytes each, back to back |
 *
 * The ground decompresses the samples in order from the first, with the
 * settings of safeStartPocket() (compression.h) and POCKET_PLUS_UPDATE_RATE,
 * until it has used the block length. Each sample carries its own time: Unix
 * time in bytes 1 to 4 and uptime in bytes 15 to 18 of the beacon body (beacon.c).
 * @date 2026-11-21
 */

#ifndef INC_HT_STORE_H_
#define INC_HT_STORE_H_

#include <stdint.h>

#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "ring.h"

#define HT_FORMAT                1u     /**< Format of the block data below; comms/ht_handling.c wrote V3 slots before. Not stored in flash: firmware that changes it needs FLASH_REGION_HT erased first (FLS-INT-090). The HK history downlink sends it first (hk_history.h). */
#define HT_BEACON_SIZE           19u    /**< The beacon body as beacon.c builds it. */
#define HT_PACKET_SIZE           20u    /**< One sample as POCKET+ takes it: the beacon body and a zero byte, whole 32-bit words. */
#define HT_SLOT_SIZE             128u   /**< Ring slot. */
#define HT_BLOCK_MAX             RING_DATA_MAX(HT_SLOT_SIZE)   /**< Most data in one block: 112 bytes. */
#define HT_SAMPLE_PERIOD_S       30u    /**< Credit each stored sample costs (FLS-RATE-020, TBC; O4). */
#define HT_CREDIT_MAX_S          (2u * HT_SAMPLE_PERIOD_S)     /**< Where the credit stops growing: two samples' worth. */
#define POCKET_PLUS_UPDATE_RATE  12u    /**< POCKET+ positive-mask update rate. The ground decoder must use the same value: it is not a block size. */

/** @brief Telemetry history events since boot, for flash health telemetry. */
typedef struct {
    uint32_t posts_dropped;     /**< Beacons ht_beacon_post() could not queue (FLS-REQ-050). */
    uint32_t beacons_skipped;   /**< Beacons the credit did not allow (FLS-RATE-020). */
    uint32_t samples;           /**< Beacons added to a block. */
    ring_stats_t ring;          /**< Blocks the ring stored or dropped, erase and program failures. */
} ht_store_stats_t;

/* ---- Any task ---- */

/**
 * @brief Gives a beacon to the telemetry history without waiting (a post, spec §5.2.1). Any task; the beacon task in practice.
 * @details The OBDH task applies the credit, compresses the beacon and stores
 *          it later. A beacon that finds no room in the OBDH queue is dropped
 *          and counted (FLS-REQ-050).
 * @param beacon The beacon body, HT_BEACON_SIZE bytes.
 */
void ht_beacon_post(const uint8_t beacon[HT_BEACON_SIZE]);

/**
 * @brief Reads the oldest stored block numbered from_seq or later, through the OBDH task, waiting at most ticks_to_wait.
 * @details Numbers missing from the ring are passed over, so a reader that goes
 *          on from the number it got plus one sees every stored block in order.
 *          The block being filled is not stored yet, so it is never read.
 *          The OBDH task never calls it: it would wait for itself.
 * @param from_seq First sequence number wanted; 0 at the start gives the oldest block.
 * @param block Receives the block's data; room for HT_BLOCK_MAX bytes.
 * @param len Receives its length.
 * @param seq Receives its sequence number.
 * @param ticks_to_wait Block time; normally OBDH_REQUEST_WAIT (obdh_requests.h).
 * @return HAL_OK, which alone makes block, len and seq valid; HAL_ERROR if no
 *         such block is stored, or, with nothing sent, for a NULL argument;
 *         HAL_BUSY if the request could not be queued; HAL_TIMEOUT if no answer
 *         came in time.
 */
HAL_StatusTypeDef ht_read_request(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq, TickType_t ticks_to_wait);

/**
 * @brief Returns the store's event counters since boot (plain function).
 */
ht_store_stats_t ht_store_stats(void);

/* ---- OBDH task only: any other caller gets HAL_ERROR, with nothing done (obdh_is_caller()) ---- */

/**
 * @brief Finds where the ring stands and drops the block being filled. The OBDH task calls it each time it starts.
 * @details Reads flash only. A restarted OBDH task may have been deleted in the
 *          middle of POCKET+, so the block in RAM is not trusted (spec §5.2.1
 *          rule 5). The credit is kept: it counts time since boot, not since
 *          the task started.
 * @return HAL_OK; HAL_ERROR for another caller, with nothing read or changed.
 */
HAL_StatusTypeDef ht_store_load(void);

/**
 * @brief Adds one beacon, if the credit allows it. The OBDH task runs it for ht_beacon_post().
 * @param beacon The beacon body, HT_BEACON_SIZE bytes.
 * @return HAL_OK, the beacon added or passed over by the credit (a block the
 *         ring could not store is counted in its stats); HAL_ERROR for another
 *         caller, with no credit taken and nothing changed.
 */
HAL_StatusTypeDef ht_store_add(const uint8_t beacon[HT_BEACON_SIZE]);

/**
 * @brief Reads a stored block. The OBDH task runs it for ht_read_request().
 * @param from_seq First sequence number wanted.
 * @param block Receives the block's data; room for HT_BLOCK_MAX bytes.
 * @param len Receives its length.
 * @param seq Receives its sequence number.
 * @return As ring_read(); HAL_ERROR for another caller, with nothing read or written.
 */
HAL_StatusTypeDef ht_store_read(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq);

#endif /* INC_HT_STORE_H_ */
