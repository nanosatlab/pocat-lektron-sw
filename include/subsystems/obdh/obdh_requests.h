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
 * @brief Program a buffer into already-erased flash.
 *
 * Enqueues a FLASH_PROGRAM request and blocks until it completes. No erase is
 * performed: the target must read as erased (all 0xFF), the address must be
 * 8-byte aligned and the length a multiple of 8 (see flash_program()).
 *
 * @param address Flash address to program at.
 * @param data Data to be programmed.
 * @param length Length of the data in bytes.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH, HAL_BUSY if the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_program_request(uint32_t address, const uint8_t *data, size_t length);

/**
 * @brief Erase the flash page containing the address, then program a buffer
 * at that address, as one OBDH operation (no other request can interleave
 * between the erase and the program).
 *
 * Enqueues a FLASH_ERASE_PROGRAM request and blocks until it completes. The
 * erase affects the WHOLE page containing the address, so this must only be
 * used by the module that owns every byte of that page. Same alignment
 * requirements as obdh_program_request().
 *
 * @param address Flash address to program at (its page is erased first).
 * @param data Data to be programmed.
 * @param length Length of the data in bytes.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH, HAL_BUSY if the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_erase_program_request(uint32_t address, const uint8_t *data, size_t length);

#endif /* INC_OBDH_REQUESTS_H_ */
