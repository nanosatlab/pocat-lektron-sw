/**
 * @file fake_obdh_requests.h
 * @brief Fake of the four OBDH flash requests, backed by a RAM copy of the whole 1 MB internal flash, with a call log.
 * @date 2026-10-23
 *
 * @details
 * On the satellite, `obdh_*_request()` (include/subsystems/obdh/obdh_requests.h)
 * sends a request to the OBDH task and blocks until OBDH has accessed the
 * flash. This fake serves the request immediately from a RAM image instead.
 * Tests can then read what was written, or preset bytes before running the
 * code.
 *
 * | Request                          | Effect on the image                                         |
 * |----------------------------------|-------------------------------------------------------------|
 * | `obdh_read_request`              | copies bytes out                                            |
 * | `obdh_write_request`             | copies bytes in (like flash_write's read-modify-write)      |
 * | `obdh_program_request`           | ANDs bytes in (programming can only clear bits)             |
 * | `obdh_erase_program_request`     | sets the 2 KB page to 0xFF, then ANDs bytes in              |
 *
 * Every call is logged (operation, address, length), even calls that fail.
 * A request with NULL data, length 0, or a range outside the flash returns
 * `HAL_ERROR`, like the real request functions.
 *
 * @note The older suite test_ht_handling uses mocks/mock_obdh_requests.c
 * instead, which covers only the HT region. Never link both into one suite.
 */
#ifndef FAKE_OBDH_REQUESTS_H
#define FAKE_OBDH_REQUESTS_H
#include <stddef.h>
#include <stdint.h>
#include "obdh_requests.h"

#define FAKE_FLASH_BASE 0x08000000u   /**< First address of the image (start of internal flash). */
#define FAKE_FLASH_SIZE 0x00100000u   /**< Image size: the full 1 MB of the STM32L476RG. */

/** @brief Which request was made. */
typedef enum { FAKE_OBDH_READ, FAKE_OBDH_WRITE, FAKE_OBDH_PROGRAM, FAKE_OBDH_ERASE_PROGRAM } fake_obdh_op_t;

/** @brief One entry of the call log. */
typedef struct {
    fake_obdh_op_t op;   /**< Request type. */
    uint32_t       addr; /**< Flash address passed by the caller. */
    size_t         len;  /**< Length passed by the caller. */
} fake_obdh_call_t;

/** @brief Resets the image to erased flash (all 0xFF), clears the call log, and removes any injected error. Call it in `setUp()`. */
void fake_obdh_reset(void);

/**
 * @brief Makes every later request fail with @p err without touching the image.
 * @param err For example `HAL_ERROR` or `HAL_BUSY`; `HAL_OK` turns injection off.
 */
void fake_obdh_inject_error(HAL_StatusTypeDef err);

/**
 * @brief Returns a pointer into the image at flash address @p addr, to read results or preset bytes.
 * @return NULL if @p addr is outside 0x08000000..0x080FFFFF.
 * @code
 * fake_obdh_flash(DEPLOYMENT_STATE_ADDR)[0] = 0xAA;            // preset before the code runs
 * TEST_ASSERT_EQUAL_UINT8(OBC_STATE_SM, fake_obdh_flash(CURRENT_STATE_ADDR)[0]);  // check after
 * @endcode
 */
uint8_t *fake_obdh_flash(uint32_t addr);

/** @brief Returns the number of requests made since the last reset (at most 256 are stored). */
int  fake_obdh_call_count(void);

/** @brief Returns log entry @p i (0 = oldest). An index out of range fails the test. */
fake_obdh_call_t fake_obdh_call_at(int i);

#endif
