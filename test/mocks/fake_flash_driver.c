/**
 * @file fake_flash_driver.c
 * @brief RAM implementation of flash_read / flash_write / flash_program / flash_erase_page. See fake_flash_driver.h.
 * @date 2026-10-23
 */
#include "fake_flash_driver.h"
#include "unity.h"
#include <string.h>

/** @brief RAM copy of the internal flash. Index 0 is address FAKE_FLASH_DRIVER_BASE. */
static uint8_t image[FAKE_FLASH_DRIVER_SIZE];
static HAL_StatusTypeDef injected = HAL_OK;
static fake_flash_driver_counts_t counts;
static uint32_t last_addr;

void fake_flash_driver_reset(void)
{
    memset(image, 0xFF, sizeof(image));
    injected = HAL_OK;
    memset(&counts, 0, sizeof(counts));
    last_addr = 0;
}

/**
 * @brief Converts a flash address range to an offset in the image, or fails the test if it falls outside the flash.
 * @details On the target, an out-of-range address would hard-fault or
 * corrupt memory. Failing loudly here makes such a bug visible.
 */
static uint32_t offset_of(uint32_t addr, uint32_t len)
{
    TEST_ASSERT_TRUE_MESSAGE(addr >= FAKE_FLASH_DRIVER_BASE &&
                             (uint64_t)addr - FAKE_FLASH_DRIVER_BASE + len <= FAKE_FLASH_DRIVER_SIZE,
                             "fake_flash_driver: address outside internal flash");
    return addr - FAKE_FLASH_DRIVER_BASE;
}

uint8_t *fake_flash_driver_mem(uint32_t addr) { return &image[offset_of(addr, 0)]; }
void fake_flash_driver_inject_error(HAL_StatusTypeDef err) { injected = err; }
fake_flash_driver_counts_t fake_flash_driver_counts(void) { return counts; }
uint32_t fake_flash_driver_last_addr(void) { return last_addr; }

void flash_read(uint32_t data_addr, uint8_t *data, uint16_t n_bytes)
{
    counts.read++;
    last_addr = data_addr;
    memcpy(data, &image[offset_of(data_addr, n_bytes)], n_bytes);
}

HAL_StatusTypeDef flash_write(uint32_t data_addr, const uint8_t *data, uint16_t n_bytes)
{
    counts.write++;
    last_addr = data_addr;
    if (injected != HAL_OK) return injected;
    /* The real driver erases and rewrites every page it touches, keeping the
     * bytes outside [data_addr, data_addr + n_bytes): in RAM that is a memcpy. */
    memcpy(&image[offset_of(data_addr, n_bytes)], data, n_bytes);
    return HAL_OK;
}

HAL_StatusTypeDef flash_program(uint32_t data_addr, const uint8_t *data, uint16_t n_bytes)
{
    counts.program++;
    last_addr = data_addr;
    if (injected != HAL_OK) return injected;
    if ((data_addr % 8u) != 0u || (n_bytes % 8u) != 0u) return HAL_ERROR;   /* double-word programming only */

    uint32_t off = offset_of(data_addr, n_bytes);
    for (uint32_t i = 0; i < n_bytes; i++) {
        if (image[off + i] != 0xFFu) return HAL_ERROR;                       /* target must be erased */
    }
    memcpy(&image[off], data, n_bytes);
    return HAL_OK;
}

HAL_StatusTypeDef flash_erase_page(uint32_t page_addr)
{
    counts.erase++;
    last_addr = page_addr;
    if (injected != HAL_OK) return injected;
    uint32_t off = offset_of(page_addr, 0);
    memset(&image[off - (off % FLASH_PAGE_SIZE)], 0xFF, FLASH_PAGE_SIZE);
    return HAL_OK;
}
