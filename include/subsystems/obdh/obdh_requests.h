/**
 * @file obdh_requests.h
 * @brief OBDH-mediated flash access.
 * @details
 * Declares the request functions any task can call to read or write internal
 * flash through the OBDH queue. Requests are serviced by the OBDH task, which
 * serializes all flash access; the calling task blocks until OBDH reports the
 * completion.
 *
 * Each request names a region of flash_map.h and an offset inside it; the
 * flash driver refuses any range outside the region. obdh_write_request() and
 * obdh_read_old_request() still take absolute addresses of the old map and are
 * deleted in step 4 of the flash plan (docs/FLASH-USAGE-SPECIFICATION.md §10).
 * @author Medir Segura
 * @date 2026-07-03
 * @note Modified on 2026-10-27.
 */

#ifndef INC_OBDH_REQUESTS_H_
#define INC_OBDH_REQUESTS_H_

#include <stdint.h>
#include <stddef.h>

#include "stm32l4xx_hal.h"
#include "flash_map.h"

#define FLASH_QUEUE_SEND_TIMEOUT_MS 100u   /* wait for room in the OBDH request queue */

/**
 * @brief Allows any task to perform a read on a flash region.
 *
 * Enqueues a FLASH_READ request to the OBDH task and blocks until the
 * operation completes.
 *
 * @param region Region of flash_map.h to read from.
 * @param offset First byte to read, counted from the start of the region.
 * @param data Destination buffer for the data read.
 * @param length Length of the data to be read.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH (HAL_ERROR also when the range is not inside the region), HAL_BUSY if
 * the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_read_request(flash_region_id_t region, uint32_t offset, uint8_t *data, size_t length);

/**
 * @brief Program a buffer into already-erased flash of a region.
 *
 * Enqueues a FLASH_PROGRAM request and blocks until it completes. No erase is
 * performed: the target must read as erased (all 0xFF), the offset must be a
 * multiple of 8 and the length a multiple of 8 (see flash_program()).
 *
 * @param region Region of flash_map.h to program.
 * @param offset Destination, counted from the start of the region.
 * @param data Data to be programmed.
 * @param length Length of the data in bytes.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH (HAL_ERROR also when the range is not inside the region), HAL_BUSY if
 * the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_program_request(flash_region_id_t region, uint32_t offset, const uint8_t *data, size_t length);

/**
 * @brief Erase the flash page containing the offset, then program a buffer
 * at that offset, as one OBDH operation (no other request can interleave
 * between the erase and the program).
 *
 * Enqueues a FLASH_ERASE_PROGRAM request and blocks until it completes. The
 * erase affects the WHOLE page containing the offset, so this must only be
 * used by the module that owns every byte of that page. Same alignment
 * requirements as obdh_program_request().
 *
 * @param region Region of flash_map.h to erase and program.
 * @param offset Destination, counted from the start of the region (its page is erased first).
 * @param data Data to be programmed.
 * @param length Length of the data in bytes.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH (HAL_ERROR also when the range is not inside the region), HAL_BUSY if
 * the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_erase_program_request(flash_region_id_t region, uint32_t offset, const uint8_t *data, size_t length);

/**
 * @brief Stores a parameter value through the OBDH task; use param_set() or param_reset() instead.
 *
 * Enqueues a FLASH_PARAM_WRITE request and blocks until param_store_write()
 * has run in the OBDH task.
 *
 * @param id Parameter ID (param_store.h).
 * @param data The new value.
 * @param length The parameter's size.
 * @return HAL_StatusTypeDef Status of the operation: param_store_write()'s
 * result, HAL_BUSY if the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_param_write_request(uint8_t id, const uint8_t *data, size_t length);

/* ---- Old map: deleted with the old addresses in step 4 ---- */

/**
 * @brief Performs a write request to the OBDH task.
 *
 * Enqueues a FLASH_WRITE request to the OBDH task and blocks until the
 * operation completes.
 *
 * @param address Flash address to write to, inside the old map pages (OLD_MAP_BASE, OLD_MAP_SIZE in flash.h).
 * @param data Data to be written.
 * @param length Length of the data to be written.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH, HAL_BUSY if the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_write_request(uint32_t address, const uint8_t *data, size_t length);

/**
 * @brief Allows any task to perform a read on the old map pages.
 *
 * Enqueues a FLASH_READ_OLD request to the OBDH task and blocks until the
 * operation completes.
 *
 * @param address Flash address to read from, inside the old map pages (OLD_MAP_BASE, OLD_MAP_SIZE in flash.h).
 * @param data Destination buffer for the data read.
 * @param length Length of the data to be read.
 * @return HAL_StatusTypeDef Status of the operation: HAL_OK / HAL_ERROR from
 * OBDH, HAL_BUSY if the request queue stayed full (request not enqueued).
 */
HAL_StatusTypeDef obdh_read_old_request(uint32_t address, uint8_t *data, size_t length);

#endif /* INC_OBDH_REQUESTS_H_ */
