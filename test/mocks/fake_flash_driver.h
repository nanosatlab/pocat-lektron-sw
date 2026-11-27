/**
 * @file fake_flash_driver.h
 * @brief Fake of the region flash driver (flash_read, flash_program, flash_erase_page) over a RAM copy of the data area, with power cuts and ECC errors.
 * @date 2026-10-27
 *
 * @details
 * The stores (param_store.c, ring.c) call the driver of include/subsystems/obdh/flash.h
 * directly, from the OBDH task. This fake behaves like the STM32L4 flash and
 * the real driver for them:
 * - programming only clears bits, 8 bytes at a time, and refuses a target that
 *   is not erased (like flash_program());
 * - an erase sets one 2 KB page to 0xFF;
 * - a range outside its region is refused, and unit data is never written.
 *
 * Faults:
 * - fake_flashdrv_cut_power_after(n): the next n program or erase steps (a
 *   step is one doubleword or one page erase) complete, the one after is cut
 *   short, and every later program or erase fails until fake_flashdrv_power_on().
 *   A cut doubleword keeps its first 4 bytes and leaves the last 4 erased; a cut
 *   erase leaves the first half of the page erased and the second half as it was.
 *   With ECC on (the default), the doublewords a cut damaged also fail ECC.
 * - fake_flashdrv_inject_ecc(): one doubleword fails ECC.
 * - fake_flashdrv_inject_wrong_program(): the next program of one doubleword
 *   stores three bits wrong, and reads report no error, as when ECC
 *   miscorrects more bad bits than it can handle.
 * - fake_flashdrv_fail_erase(): one page no longer erases.
 * A read that touches a doubleword failing ECC returns the bytes and HAL_ERROR,
 * as the real flash_read() does after the NMI.
 */
#ifndef FAKE_FLASH_DRIVER_H
#define FAKE_FLASH_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "flash.h"

/** @brief Erases the whole image, clears faults and counters. Call it in `setUp()`. */
void fake_flashdrv_reset(void);

/**
 * @brief Plans a power cut.
 * @param steps Program or erase steps that still complete; the next one is cut short.
 */
void fake_flashdrv_cut_power_after(long steps);

/** @brief Restores the power and cancels a planned cut, as at a reboot. */
void fake_flashdrv_power_on(void);

/** @brief Returns true once a planned cut has happened (and until fake_flashdrv_power_on()). */
bool fake_flashdrv_power_is_cut(void);

/** @brief Chooses whether doublewords damaged by a cut also fail ECC (default true). */
void fake_flashdrv_set_cut_ecc(bool on);

/** @brief Makes the doubleword at @p offset of @p region fail ECC until its page is erased. */
void fake_flashdrv_inject_ecc(flash_region_id_t region, uint32_t offset);

/** @brief Makes the next program of the doubleword at @p offset of @p region store three bits wrong, which its reads do not report. */
void fake_flashdrv_inject_wrong_program(flash_region_id_t region, uint32_t offset);

/** @brief Makes every erase of the page holding @p offset of @p region fail and leave the page as it was, until fake_flashdrv_reset(). */
void fake_flashdrv_fail_erase(flash_region_id_t region, uint32_t offset);

/** @brief Returns a pointer into the image at @p offset of @p region, to read or scribble bytes. */
uint8_t *fake_flashdrv_bytes(flash_region_id_t region, uint32_t offset);

/** @brief Returns how many times the page holding @p offset of @p region was erased (cut and failed erases count). */
uint32_t fake_flashdrv_erases(flash_region_id_t region, uint32_t offset);

/** @brief Returns how many doublewords were programmed since the last reset. */
uint32_t fake_flashdrv_programs(void);

/** @brief Returns how many times flash_read() was called since the last reset, refused calls included. */
uint32_t fake_flashdrv_reads(void);

#endif
