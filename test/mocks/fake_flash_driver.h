/**
 * @file fake_flash_driver.h
 * @brief Fake of the low-level flash driver (src/utils/flash.c) over a RAM copy of the 1 MB internal flash.
 * @date 2026-10-23
 *
 * @details
 * The OBDH task is the only caller of the real driver functions
 * `flash_read()`, `flash_write()`, `flash_program()` and `flash_erase_page()`.
 * Those functions touch STM32 flash registers and can't run on a PC. This fake
 * implements them on a RAM image and keeps the same rules as flash.c:
 *
 * | Function            | Behaviour (same as flash.c)                                                    |
 * |---------------------|--------------------------------------------------------------------------------|
 * | `flash_read`        | copies bytes out                                                               |
 * | `flash_write`       | read-modify-write: the new bytes replace the old ones, the rest of the page stays |
 * | `flash_program`     | `HAL_ERROR` if the address or length isn't a multiple of 8, or the target isn't erased (0xFF) |
 * | `flash_erase_page`  | sets the whole 2 KB page containing the address to 0xFF                        |
 *
 * Every call is counted, and the address of the last call is kept. An error
 * can be injected into write, program and erase.
 *
 * Use it **instead of** fake_obdh_requests.c when the real OBDH task and the
 * real obdh_requests.c are under test (test_obdh). The other fake replaces
 * the requests themselves.
 */
#ifndef FAKE_FLASH_DRIVER_H
#define FAKE_FLASH_DRIVER_H
#include <stdint.h>
#include "stm32l4xx_hal.h"
#include "flash.h"

#define FAKE_FLASH_DRIVER_BASE 0x08000000u   /**< First address of the image. */
#define FAKE_FLASH_DRIVER_SIZE 0x00100000u   /**< 1 MB, the internal flash of the STM32L476RG. */

/** @brief Resets the image to erased flash (all 0xFF), zeroes the counters, and removes the injected error. Call it in `setUp()`. */
void fake_flash_driver_reset(void);

/**
 * @brief Returns a pointer into the image at flash address @p addr, to preset bytes or check results.
 * @details An address outside 0x08000000..0x080FFFFF fails the test.
 */
uint8_t *fake_flash_driver_mem(uint32_t addr);

/**
 * @brief Makes write, program and erase return @p err without touching the image. Reads still work.
 * @param err For example `HAL_ERROR`; `HAL_OK` turns injection off.
 */
void fake_flash_driver_inject_error(HAL_StatusTypeDef err);

/** @brief Number of calls since the last reset, one counter per driver function. */
typedef struct {
    int read;     /**< flash_read() calls. */
    int write;    /**< flash_write() calls. */
    int program;  /**< flash_program() calls. */
    int erase;    /**< flash_erase_page() calls. */
} fake_flash_driver_counts_t;

/** @brief Returns the call counters. */
fake_flash_driver_counts_t fake_flash_driver_counts(void);

/** @brief Returns the address passed to the most recent driver call, of any kind. */
uint32_t fake_flash_driver_last_addr(void);

#endif
