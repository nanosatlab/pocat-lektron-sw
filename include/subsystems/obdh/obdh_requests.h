/**
 * @file obdh_requests.h
 * @brief OBDH-mediated flash access.
 * @details
 * Declares the request functions any task can call to read or write internal
 * flash through the OBDH queue. Requests are serviced by the OBDH task, which
 * serializes all flash access; the calling task blocks until OBDH reports the
 * completion.
 * @author Medir Segura
 * @date 2026-07-03
 */

#ifndef INC_OBDH_REQUESTS_H_
#define INC_OBDH_REQUESTS_H_

#include <stdint.h>
#include <stddef.h>

#include "stm32l4xx_hal.h"

#define FLASH_QUEUE_SEND_TIMEOUT_MS 100u   /* wait for room in the OBDH request queue */

/**
 * @brief Performs a write request to the OBDH task.
 *
 * Enqueues a FLASH_WRITE request to the OBDH task and blocks until the
 * operation completes.
 *
 * @param address Flash address to write to.
 * @param data Data to be written.
 * @param length Length of the data to be written.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH, HAL_BUSY if the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_write_request(uint32_t address, const uint8_t *data, size_t length);

/**
 * @brief Allows any task to perform a read on the flash.
 *
 * Enqueues a FLASH_READ request to the OBDH task and blocks until the
 * operation completes.
 *
 * @param address Flash address to read from.
 * @param data Destination buffer for the data read.
 * @param length Length of the data to be read.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH, HAL_BUSY if the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_read_request(uint32_t address, uint8_t *data, size_t length);

/**
 * @brief Store one built historic-telemetry block in the circular queue.
 *
 * Enqueues a FLASH_STORE_HT request and blocks until it completes.
 *
 * @param slot Built block of HT_SLOT_SIZE bytes (see ht_handling.h); its seq
 * field is assigned by OBDH.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH, HAL_BUSY if the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_store_ht_request(const uint8_t *slot);

#endif /* INC_OBDH_REQUESTS_H_ */
