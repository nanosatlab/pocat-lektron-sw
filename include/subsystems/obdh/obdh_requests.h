/**
 * @file obdh_requests.h
 * @brief OBDH-mediated store access.
 * @details
 * The OBDH task runs every store, so it alone touches flash. The stores send
 * their operations to it through the OBDH queue with these functions; other
 * tasks call the store functions (param_set_request(), ht_beacon_post(),
 * ht_read_request()) instead.
 *
 * A request waits at most its block time in total: for room in the queue, then
 * for OBDH's answer. Data travels by copy (at most OBDH_DATA_MAX bytes), so a
 * caller that gives up leaves nothing behind. A request returns:
 * - OBDH's status: HAL_OK, or HAL_ERROR if the operation failed;
 * - HAL_BUSY if the queue stayed full for the whole block time (nothing sent);
 * - HAL_TIMEOUT if no answer came in time: the result is unknown, and OBDH may
 *   still carry out the operation later;
 * - HAL_ERROR without sending for NULL data, a length of 0 or above
 *   OBDH_DATA_MAX, or a calling task that is neither in the task table nor the
 *   OBC task;
 * - for a read, HAL_ERROR with nothing written if the block is longer than the
 *   caller's buffer.
 *
 * The OBDH task must not send a request, nor the OBC task while it holds OBDH
 * paused: either would wait for itself.
 *
 * A post (obdh_ht_post()) never waits and gets no answer. It is queued only
 * while more than OBDH_CLIENTS entries are free, so posts never take the
 * entries the clients need for their requests. It returns HAL_OK once queued,
 * HAL_BUSY if there was no room (nothing sent), or HAL_ERROR without sending
 * for the cases above.
 * @author Medir Segura
 * @date 2026-07-03
 * @note Reworked in 2026-11: store operations instead of flash ranges, block times, posts.
 */

#ifndef INC_OBDH_REQUESTS_H_
#define INC_OBDH_REQUESTS_H_

#include <stdint.h>
#include <stddef.h>

#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "ht_store.h"

#define OBDH_REQUEST_WAIT_MS  1000u                                /**< Standard block time of a request. */
#define OBDH_REQUEST_WAIT     pdMS_TO_TICKS(OBDH_REQUEST_WAIT_MS)  /**< The same, in ticks: what callers normally pass. */

/**
 * @brief Stores a parameter value through the OBDH task; use param_set_request() or param_reset_request() instead.
 *
 * @param id Parameter ID (param_store.h).
 * @param data The new value.
 * @param length The parameter's size.
 * @param ticks_to_wait Block time.
 * @return See the file description; otherwise param_store_write()'s result.
 */
HAL_StatusTypeDef obdh_param_write_request(uint8_t id, const uint8_t *data, size_t length, TickType_t ticks_to_wait);

/**
 * @brief Posts a beacon to the telemetry history; use ht_beacon_post() instead.
 *
 * @param beacon The beacon body, HT_BEACON_SIZE bytes.
 * @return See the post in the file description.
 */
HAL_StatusTypeDef obdh_ht_post(const uint8_t beacon[HT_BEACON_SIZE]);

/**
 * @brief Reads a block of the telemetry history; use ht_read_request() instead.
 *
 * @param from_seq First sequence number wanted.
 * @param block Receives the block's data; room for HT_BLOCK_MAX bytes, written only when the request returns HAL_OK.
 * @param len Receives the block's length.
 * @param seq Receives its sequence number.
 * @param ticks_to_wait Block time.
 * @return See the file description; otherwise ht_store_read()'s result.
 */
HAL_StatusTypeDef obdh_ht_read_request(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq, TickType_t ticks_to_wait);

#endif /* INC_OBDH_REQUESTS_H_ */
