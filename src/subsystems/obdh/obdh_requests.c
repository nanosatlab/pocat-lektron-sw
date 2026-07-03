/**
 * @file obdh_requests.c
 * @brief Client-side implementation of OBDH-mediated flash access.
 * @details
 * Implements the request functions that route task flash access through the
 * OBDH queue: each call builds an obdh_request, enqueues it for the OBDH task
 * and blocks on an indexed task notification until the matching completion
 * arrives (or a timeout expires).
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

static uint32_t obdh_next_token(void);
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

/**
 * @brief Generate a unique token for a flash request.
 * @details Monotonic counter (masked to OBDH_TOKEN_MASK) used to pair a request
 *          with its completion. The critical section keeps the increment atomic
 *          across the tasks that share the OBDH queue.
 */
static uint32_t obdh_next_token(void)
{
    static uint32_t counter;
    uint32_t t;

    taskENTER_CRITICAL();
    t = ++counter;
    taskEXIT_CRITICAL();

    return t & OBDH_TOKEN_MASK;
}

/**
 * @brief Post a prepared flash request to the OBDH task and block for its result.
 *
 * Stamps the calling task handle and a unique token, enqueues the request, then
 * waits up to FLASH_OP_TIMEOUT_MS on the dedicated flash notification index
 * (OBDH_NOTIFY_IDX) for OBDH to return the completion. The completion carries the
 * token (see OBDH_* packing macros in obdh.h), so a stale completion left over
 * from an earlier request that already timed out is recognised and ignored
 * rather than mistaken for this request's result.
 *
 * @param request Flash request with op/addr/len/buf already populated.
 * @retval HAL_OK / HAL_ERROR  Status reported by the OBDH task for the operation.
 * @retval HAL_BUSY            Request queue still full after the send timeout.
 * @retval HAL_TIMEOUT         OBDH did not complete the request within FLASH_OP_TIMEOUT_MS.
 *
 * @note Read caveat: on HAL_TIMEOUT the request may still be serviced by OBDH
 *       later, which would write into the read destination buffer. A timeout
 *       therefore means "OBDH is wedged" (the health watchdog resets it); callers
 *       must not assume a timed-out read buffer is left untouched.
 */
static HAL_StatusTypeDef obdh_submit_request(obdh_request *request)
{
    request->client = xTaskGetCurrentTaskHandle();
    request->token  = obdh_next_token();

    if (xQueueSend(obdh_queue_handle, request, pdMS_TO_TICKS(FLASH_QUEUE_SEND_TIMEOUT_MS)) != pdPASS)
        return HAL_BUSY;   /* could not submit: queue still full after the timeout */

    const TickType_t timeout = pdMS_TO_TICKS(FLASH_OP_TIMEOUT_MS);
    const TickType_t start   = xTaskGetTickCount();

    for (;;) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout)
            return HAL_TIMEOUT;

        uint32_t value = 0;
        if (xTaskNotifyWaitIndexed(OBDH_NOTIFY_IDX, UINT32_MAX, UINT32_MAX,
                                   &value, timeout - elapsed) != pdPASS)
            return HAL_TIMEOUT;   /* no completion within the remaining window */

        /* Accept only the completion carrying our token; a mismatch is a late
           completion from an earlier request that already timed out. */
        if ((value >> OBDH_STATUS_BITS) == request->token)
            return (HAL_StatusTypeDef)(value & OBDH_STATUS_MASK);
    }
}
