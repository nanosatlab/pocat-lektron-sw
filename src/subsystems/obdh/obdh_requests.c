/**
 * @file obdh_requests.c
 * @brief Client-side implementation of OBDH-mediated store access.
 * @details
 * Each public function only names its operation. send() and post() carry
 * data, which prepare_with_data() copies into the request; a read carries none
 * (len 0), and fetch_block() copies the block out of the answer, refusing one
 * longer than the caller's buffer. round_trip() queues a request and waits for
 * its answer within one deadline; post() queues a post, which gets no answer.
 *
 * A client is a task of the task table or the OBC task (tm_current_index()).
 * Each client builds its request and receives its answer in its own static
 * slot, so a request costs the caller no stack. The slot also keeps the
 * client's request number, so a restarted task continues the sequence and
 * ignores the answers to its predecessor's requests.
 * @author Medir Segura
 * @date 2026-07-03
 * @note Moved out of flash.c so the flash driver stays free of OBDH queue logic.
 *       Reworked in 2026-11: store operations instead of flash ranges, block times, posts.
 */

#include "obdh_requests.h"
#include "obdh.h"
#include "health.h"
#include "task_management.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <string.h>

/* Sanity check only: no task may wait longer than the health period between two health reports */
_Static_assert(OBDH_REQUEST_WAIT_MS < HEALTH_PERIOD_MS, "the standard block time must be shorter than the health period");

/** @brief What one client has in flight: the request it builds and the answer it receives. */
typedef struct {
    obdh_request request;
    obdh_answer  answer;
} client_slot_t;

static client_slot_t slots[OBDH_CLIENTS];

static HAL_StatusTypeDef send(obdh_request_header header, const uint8_t *data, size_t len, TickType_t ticks_to_wait);
static HAL_StatusTypeDef fetch_block(obdh_request_header header, uint8_t *data, size_t room, uint32_t *len, uint32_t *seq, TickType_t ticks_to_wait);
static HAL_StatusTypeDef post(obdh_request_header header, const uint8_t *data, size_t len);
static int prepare_with_data(obdh_request_header header, const uint8_t *data, size_t len);
static int prepare(obdh_request_header header, size_t len);
static HAL_StatusTypeDef round_trip(int client, TickType_t ticks_to_wait);


HAL_StatusTypeDef obdh_param_write_request(uint8_t id, const uint8_t *data, size_t len, TickType_t ticks_to_wait)
{
    return send((obdh_request_header){ .op = FLASH_PARAM_WRITE, .param_id = id }, data, len, ticks_to_wait);
}

HAL_StatusTypeDef obdh_ht_post(const uint8_t beacon[HT_BEACON_SIZE])
{
    return post((obdh_request_header){ .op = HT_ADD_BEACON }, beacon, HT_BEACON_SIZE);
}

HAL_StatusTypeDef obdh_ht_read_request(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq, TickType_t ticks_to_wait)
{
    return fetch_block((obdh_request_header){ .op = HT_READ_BLOCK, .seq = from_seq }, block, HT_BLOCK_MAX, len, seq, ticks_to_wait);
}

/**
 * @brief Sends a request that carries data, and returns OBDH's answer.
 * @param header The operation.
 * @param data The bytes to copy into the request.
 * @param len Number of bytes.
 * @param ticks_to_wait Block time.
 * @return As described in obdh_requests.h.
 */
static HAL_StatusTypeDef send(obdh_request_header header, const uint8_t *data, size_t len, TickType_t ticks_to_wait)
{
    int client = prepare_with_data(header, data, len);
    if (client < 0)
        return HAL_ERROR;

    return round_trip(client, ticks_to_wait);
}

/**
 * @brief Sends a request that returns a block of a ring, and copies the block out of a successful answer.
 * @details The request carries no data, so its len is 0.
 * @param header The operation.
 * @param data Destination of the block; untouched unless the result is HAL_OK.
 * @param room Bytes data can take; a longer block is refused.
 * @param len Receives the block's length.
 * @param seq Receives the block's sequence number.
 * @param ticks_to_wait Block time.
 * @return As described in obdh_requests.h; HAL_ERROR without sending for a NULL data, len or seq;
 *         HAL_ERROR with nothing written for a block longer than room.
 */
static HAL_StatusTypeDef fetch_block(obdh_request_header header, uint8_t *data, size_t room, uint32_t *len, uint32_t *seq, TickType_t ticks_to_wait)
{
    if ((data == NULL) || (len == NULL) || (seq == NULL))
        return HAL_ERROR;

    int client = prepare(header, 0u);
    if (client < 0)
        return HAL_ERROR;

    HAL_StatusTypeDef status = round_trip(client, ticks_to_wait);
    if (status != HAL_OK)
        return status;

    const obdh_answer *a = &slots[client].answer;
    if (a->len > room)
        return HAL_ERROR;

    memcpy(data, a->data, a->len);
    *len = a->len;
    *seq = a->seq;
    return HAL_OK;
}

/**
 * @brief Queues a post without waiting; OBDH serves it and sends no answer.
 * @details The post is queued only while more than OBDH_CLIENTS entries are
 *          free, so the last entries stay free, one for each client's request.
 *          The count and the send run with the scheduler suspended, so no
 *          other task takes an entry in between; a send that does not wait is
 *          allowed there.
 * @param header The operation.
 * @param data The bytes to copy into the post.
 * @param len Number of bytes.
 * @return HAL_OK once queued; HAL_BUSY if there was no room for a post; HAL_ERROR if prepare_with_data() refused it. Nothing is sent unless HAL_OK.
 */
static HAL_StatusTypeDef post(obdh_request_header header, const uint8_t *data, size_t len)
{
    int client = prepare_with_data(header, data, len);
    if (client < 0)
        return HAL_ERROR;

    BaseType_t queued = pdFAIL;
    vTaskSuspendAll();
    if (uxQueueSpacesAvailable(obdh_queue_handle) > OBDH_CLIENTS)
        queued = xQueueSend(obdh_queue_handle, &slots[client].request, 0);
    (void)xTaskResumeAll();

    return (queued == pdPASS) ? HAL_OK : HAL_BUSY;
}

/**
 * @brief Prepares a request that carries data: fills in the header and copies the data into the calling client's request.
 * @param header The operation.
 * @param data The bytes to carry.
 * @param len Number of bytes: 1 to OBDH_DATA_MAX.
 * @return The calling client's index, or -1 if the request is refused (nothing is sent).
 */
static int prepare_with_data(obdh_request_header header, const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0u))
        return -1;

    int client = prepare(header, len);
    if (client >= 0)
        memcpy(slots[client].request.data, data, len);
    return client;
}

/**
 * @brief Fills in the calling client's request header.
 * @param header The operation.
 * @param len Bytes of data the request carries; 0 for a read.
 * @return The calling client's index, or -1 if the calling task is no client or len is above OBDH_DATA_MAX (nothing is sent).
 */
static int prepare(obdh_request_header header, size_t len)
{
    int client = tm_current_index();
    if ((client < 0) || (len > OBDH_DATA_MAX))
        return -1;

    obdh_request_header *h = &slots[client].request.header;
    uint32_t number = h->number + 1u;   /* continues the sequence of an earlier task with this index */
    *h = header;
    h->number = number;
    h->len = (uint16_t)len;
    h->client = (uint8_t)client;
    return client;
}

/**
 * @brief Queues the client's request and waits for its answer, within one deadline.
 * @details An answer with another number belongs to an earlier request that
 *          timed out: it is dropped and the wait goes on, without a new deadline.
 * @param client Index of the calling client.
 * @param ticks_to_wait Block time for queueing and answer together.
 * @return OBDH's status; HAL_BUSY if the request could not be queued; HAL_TIMEOUT if no answer came in time.
 */
static HAL_StatusTypeDef round_trip(int client, TickType_t ticks_to_wait)
{
    client_slot_t *slot = &slots[client];
    TimeOut_t deadline;
    vTaskSetTimeOutState(&deadline);

    if (xQueueSend(obdh_queue_handle, &slot->request, ticks_to_wait) != pdPASS)
        return HAL_BUSY;

    for (;;) {
        if (xTaskCheckForTimeOut(&deadline, &ticks_to_wait) != pdFALSE)
            return HAL_TIMEOUT;
        if (xQueueReceive(obdh_answer_queues[client], &slot->answer, ticks_to_wait) != pdPASS)
            return HAL_TIMEOUT;
        if (slot->answer.number == slot->request.header.number)
            return slot->answer.status;
    }
}
