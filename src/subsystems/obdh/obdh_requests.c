/**
 * @file obdh_requests.c
 * @brief Client-side implementation of OBDH-mediated flash access.
 * @details
 * Implements the request functions that route task flash access through the
 * OBDH queue: each call builds an obdh_request, enqueues it for the OBDH task
 * and blocks on an indexed task notification until the completion arrives.
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

static HAL_StatusTypeDef obdh_submit_request(obdh_request *request);


HAL_StatusTypeDef obdh_write_request(uint32_t address, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0)
        return HAL_ERROR;

    obdh_request request = {
        .op      = FLASH_WRITE,
        .addr    = address,
        .len     = len,
        .buf.src = data,
    };
    return obdh_submit_request(&request);
}

HAL_StatusTypeDef obdh_read_request(uint32_t address, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0)
        return HAL_ERROR;

    obdh_request request = {
        .op      = FLASH_READ,
        .addr    = address,
        .len     = len,
        .buf.dst = data,
    };
    return obdh_submit_request(&request);
}

HAL_StatusTypeDef obdh_store_ht_request(const uint8_t *slot)
{
    if (slot == NULL)
        return HAL_ERROR;

    obdh_request request = {
        .op      = FLASH_STORE_HT,
        .addr    = 0,              /* slot address is chosen by OBDH from its queue state */
        .len     = HT_SLOT_SIZE,
        .buf.src = slot,
    };
    return obdh_submit_request(&request);
}

/**
 * @brief Post a prepared flash request to the OBDH task and block for its result.
 *
 * Stamps the calling task handle, enqueues the request, then blocks on the
 * dedicated flash notification index (OBDH_NOTIFY_IDX) until OBDH returns the
 * completion carrying the HAL status.
 *
 * @param request Flash request with op/addr/len/buf already populated.
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
