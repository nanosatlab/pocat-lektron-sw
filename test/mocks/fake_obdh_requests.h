/**
 * @file fake_obdh_requests.h
 * @brief Fake of the OBDH parameter write, served at once by the real parameter store; with a call log.
 * @date 2026-10-23
 * @note Modified on 2026-11-21: obdh_write_request() deleted (flash plan step 3);
 * parameter writes added; the region, program, erase and old-map requests
 * deleted with the flash image (step 5a). On 2026-11-24 the block time was
 * added to the log (step 5b).
 *
 * @details
 * On the satellite, `obdh_param_write_request()` (include/subsystems/obdh/obdh_requests.h)
 * sends the write to the OBDH task and waits, at most its block time, until
 * OBDH has run `param_store_write()`. This fake runs it immediately instead,
 * and ignores the block time. It runs it as the OBDH task, the task
 * `tm_get_task_handle(TM_TASK_OBDH)` returns, since the store refuses any
 * other caller (spec FLS-REQ-110); the calling task is current again after.
 * The request protocol itself is tested in test_obdh_requests.c.
 *
 * A suite that uses it links the real param_store.c, crc32.c and a flash
 * driver for the store (mocks/fake_flash_driver.c), and provides
 * tm_get_task_handle(), real or faked, with an OBDH task.
 *
 * Every call is logged with its ID, length and block time, even calls that
 * fail. Like the OBDH requests, a write
 * returns `HAL_ERROR` when its data is NULL or its length is 0 or above
 * `OBDH_DATA_MAX`.
 *
 * @note The HT operations, obdh_ht_post() and obdh_ht_read_request(), are not
 * faked here: test_ht_store fakes them itself.
 */
#ifndef FAKE_OBDH_REQUESTS_H
#define FAKE_OBDH_REQUESTS_H
#include <stddef.h>
#include <stdint.h>
#include "obdh_requests.h"

/** @brief One entry of the call log: a parameter write. */
typedef struct {
    uint8_t    id;      /**< The parameter ID. */
    size_t     len;     /**< Length passed by the caller. */
    TickType_t ticks;   /**< Block time passed by the caller. */
} fake_obdh_call_t;

/** @brief Hook run at the start of every request, before it is served. */
typedef void (*fake_obdh_request_hook_t)(void);

/** @brief Clears the call log, and removes any injected error and the hook. Call it in `setUp()`. */
void fake_obdh_reset(void);

/**
 * @brief Makes every later request fail with @p err without serving it.
 * @param err For example `HAL_ERROR`, `HAL_BUSY` or `HAL_TIMEOUT`; `HAL_OK` turns injection off.
 */
void fake_obdh_inject_error(HAL_StatusTypeDef err);

/**
 * @brief Installs a hook that runs at the start of every request, or removes it with NULL.
 * @details For checks about the moment a request is sent, for example that
 * the OBC never sends one while it holds OBDH paused (spec FLS-REQ-080).
 */
void fake_obdh_set_request_hook(fake_obdh_request_hook_t hook);

/** @brief Returns the number of requests made since the last reset (at most 256 are stored). */
int  fake_obdh_call_count(void);

/** @brief Returns log entry @p i (0 = oldest). An index out of range fails the test. */
fake_obdh_call_t fake_obdh_call_at(int i);

#endif
