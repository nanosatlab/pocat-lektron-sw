/**
 * @file hk_history.c
 * @brief Copies the stored telemetry-history blocks for an HK history downlink (hk_history.h).
 * @date 2026-11-21
 */

#include "hk_history.h"
#include "ht_store.h"

#include "FreeRTOS.h"
#include "task.h"

#define FIRST_RECORD  1u   /**< The records start after the format byte. */

_Static_assert(FIRST_RECORD + HK_HISTORY_RECORD_HEADER + HT_BLOCK_MAX <= HK_HISTORY_MAX, "a copy must hold at least one block");
_Static_assert(HT_BLOCK_MAX <= UINT8_MAX, "LEN is one byte");

HAL_StatusTypeDef hk_history_request(uint32_t from_seq, uint8_t buf[HK_HISTORY_MAX], uint32_t *len, TickType_t ticks_to_wait)
{
    TimeOut_t deadline;
    vTaskSetTimeOutState(&deadline);

    uint32_t used = FIRST_RECORD;
    HAL_StatusTypeDef status;
    do {
        uint8_t *record = &buf[used];
        uint32_t block_len = 0u;
        uint32_t seq = 0u;
        status = ht_read_request(from_seq, &record[HK_HISTORY_RECORD_HEADER], &block_len, &seq, ticks_to_wait);
        if (status != HAL_OK) {
            break;
        }
        record[0] = (uint8_t)(seq >> 24);
        record[1] = (uint8_t)(seq >> 16);
        record[2] = (uint8_t)(seq >> 8);
        record[3] = (uint8_t)seq;
        record[4] = (uint8_t)block_len;
        used += HK_HISTORY_RECORD_HEADER + block_len;
        from_seq = seq + 1u;
    } while ((used + HK_HISTORY_RECORD_HEADER + HT_BLOCK_MAX <= HK_HISTORY_MAX) &&
             (xTaskCheckForTimeOut(&deadline, &ticks_to_wait) == pdFALSE));

    if (used == FIRST_RECORD) {
        return status;   /* no block read: nothing stored from from_seq on (HAL_ERROR), or OBDH did not answer */
    }
    buf[0] = (uint8_t)HT_FORMAT;
    *len = used;
    return HAL_OK;
}
