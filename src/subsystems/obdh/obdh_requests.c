/**
 * @file obdh_requests.c
 * @brief Client-side implementation of OBDH-mediated flash access.
 * @details
 * Implements the request functions that route task flash access through the
 * OBDH queue: each call builds an obdh_request, enqueues it for the OBDH task
 * and blocks on an indexed task notification until the completion arrives.
 * Each public function only fills in its request; submit_with_data() and
 * obdh_submit_request() check it and do the queue round trip.
 * @todo Consider other options other than simply indefinitely blocking the caller.
 * @author Medir Segura
 * @date 2026-07-03
 * @note Moved out of flash.c so the flash driver stays free of OBDH queue logic.
 */

#include "obdh_requests.h"
#include "obdh.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "notifications.h"

static HAL_StatusTypeDef submit_with_data(obdh_request *request);
static HAL_StatusTypeDef obdh_submit_request(obdh_request *request);


HAL_StatusTypeDef obdh_read_request(flash_region_id_t region, uint32_t offset, uint8_t *data, size_t len)
{
    return submit_with_data(&(obdh_request){ .op = FLASH_READ, .region = region, .offset = offset,
                                             .len = len, .buf.dst = data });
}

HAL_StatusTypeDef obdh_program_request(flash_region_id_t region, uint32_t offset, const uint8_t *data, size_t len)
{
    return submit_with_data(&(obdh_request){ .op = FLASH_PROGRAM, .region = region, .offset = offset,
                                             .len = len, .buf.src = data });
}

HAL_StatusTypeDef obdh_erase_program_request(flash_region_id_t region, uint32_t offset, const uint8_t *data, size_t len)
{
    return submit_with_data(&(obdh_request){ .op = FLASH_ERASE_PROGRAM, .region = region, .offset = offset,
                                             .len = len, .buf.src = data });
}

HAL_StatusTypeDef obdh_param_write_request(uint8_t id, const uint8_t *data, size_t len)
{
    return submit_with_data(&(obdh_request){ .op = FLASH_PARAM_WRITE, .param_id = id,
                                             .len = len, .buf.src = data });
}

/* ---- Old map: deleted with the old addresses in step 4 ---- */

HAL_StatusTypeDef obdh_write_request(uint32_t address, const uint8_t *data, size_t len)
{
    return submit_with_data(&(obdh_request){ .op = FLASH_WRITE, .offset = address,
                                             .len = len, .buf.src = data });
}

HAL_StatusTypeDef obdh_read_old_request(uint32_t address, uint8_t *data, size_t len)
{
    return submit_with_data(&(obdh_request){ .op = FLASH_READ_OLD, .offset = address,
                                             .len = len, .buf.dst = data });
}

/**
 * @brief Check that a request carries data, then submit it.
 *
 * @param request Flash request with op/region/offset/len/buf already populated.
 * @retval HAL_ERROR if the buffer is NULL or the length is 0 (nothing is enqueued).
 * @retval Otherwise as obdh_submit_request().
 */
static HAL_StatusTypeDef submit_with_data(obdh_request *request)
{
    if ((request->buf.src == NULL) || (request->len == 0u))
        return HAL_ERROR;

    return obdh_submit_request(request);
}

/**
 * @brief Post a prepared flash request to the OBDH task and block for its result.
 *
 * Stamps the calling task handle, enqueues the request, then blocks on the
 * dedicated flash notification index (OBDH_NOTIFY_IDX) until OBDH returns the
 * completion carrying the HAL status.
 *
 * @param request Flash request with op/region/offset/len/buf already populated.
 * @retval HAL_OK / HAL_ERROR  Status reported by the OBDH task for the operation.
 * @retval HAL_BUSY            Request queue still full after the send timeout.
 */
static HAL_StatusTypeDef obdh_submit_request(obdh_request *request)
{
    request->client = xTaskGetCurrentTaskHandle();

    if (xQueueSend(obdh_queue_handle, request, pdMS_TO_TICKS(FLASH_QUEUE_SEND_TIMEOUT_MS)) != pdPASS)
        return HAL_BUSY;

    uint32_t value = 0;
    xTaskNotifyWaitIndexed(OBDH_NOTIFY_IDX, UINT32_MAX, UINT32_MAX, &value, portMAX_DELAY);
    return (HAL_StatusTypeDef)value;
}
