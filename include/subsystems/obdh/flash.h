/**
 * @file flash.h
 * @brief Internal flash driver.
 * @details
 * Programs, erases and reads internal flash inside the regions of flash_map.h.
 * Each call names a region and an offset inside it, and a range outside the
 * region is refused. Only the stores call the driver, from the OBDH task (and
 * param_store_load() from main() before the scheduler starts); other tasks go
 * through the store functions.
 * @author Medir Segura
 * @date 2023-01-17
 * @note Reworked in 2026-11: region-based access, bounded waits, ECC handling.
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

#include <stdint.h>
#include <stdbool.h>

#include "stm32l4xx_hal.h"
#include "flash_map.h"

/* ---- Driver: regions of flash_map.h ---- */

/** @brief Timeout for one doubleword program. DS10198: 90.76 µs max; 2 ms because HAL_GetTick() has 1 ms resolution. */
#define FLASH_PROGRAM_LIMIT_MS  2u

/** @brief Timeout for one page erase. DS10198: 24.47 ms max. */
#define FLASH_ERASE_LIMIT_MS    100u

/**
  * @brief  Returns the size of a region of flash_map.h.
  * @param  region: Region of flash_map.h.
  * @retval Its size in bytes; 0 for a value that is not a region.
  */
uint32_t flash_region_size(flash_region_id_t region);

/**
  * @brief  Reads bytes from a region into a RAM buffer.
  * @details Internal flash is memory-mapped, so the read is a copy. An ECC
  *          double error in the bytes read gives HAL_ERROR, even if its NMI
  *          has not run yet.
  * @param  region: Region of flash_map.h.
  * @param  offset: First byte to read, counted from the start of the region.
  * @param  data: Destination buffer (must hold at least n_bytes).
  * @param  n_bytes: Number of bytes to read.
  * @retval HAL_OK; HAL_ERROR if the range is not inside the region (nothing is
  *         copied) or an ECC double error hit the bytes read (they are wrong).
  */
HAL_StatusTypeDef flash_read(flash_region_id_t region, uint32_t offset, uint8_t *data, uint32_t n_bytes);

/**
  * @brief  Programs a byte buffer into already-erased flash of a region, without erasing.
  * @details Programs doublewords in ascending address order, so the end of the
  *          buffer is programmed last, each within FLASH_PROGRAM_LIMIT_MS. A
  *          destination that is not erased is refused before anything is
  *          programmed. If the chip reports an error or stays busy, programming
  *          stops at that doubleword; the ones before it stay programmed.
  * @param  region: Region of flash_map.h. Unit data, written on the ground, is refused.
  * @param  offset: Destination, counted from the start of the region (must be a multiple of 8).
  * @param  data: Pointer to the source buffer.
  * @param  n_bytes: Number of bytes to program (must be a multiple of 8).
  * @retval HAL_OK; HAL_ERROR if the range is outside the region, misaligned or
  *         not erased (nothing programmed), or the chip reported an error;
  *         HAL_TIMEOUT if the flash stayed busy past its limit. After a timeout
  *         the interface is left as it is, because FLASH_CR cannot be written
  *         while the flash is busy (RM0351 §3.3.5); the next call cleans up.
  */
HAL_StatusTypeDef flash_program(flash_region_id_t region, uint32_t offset, const uint8_t *data, uint32_t n_bytes);

/**
  * @brief  Erases the 2 KB page of a region that contains the given offset.
  * @details Bounded by FLASH_ERASE_LIMIT_MS. The calling task sleeps between
  *          status checks, so lower-priority tasks keep running.
  * @param  region: Region of flash_map.h. Unit data, written on the ground, is refused.
  * @param  offset: Any offset inside the page to erase, counted from the start of the region.
  * @retval HAL_OK; HAL_ERROR if the offset is not inside the region or the erase
  *         reported an error; HAL_TIMEOUT if it did not end within its limit, in
  *         which case the interface is left as for flash_program().
  */
HAL_StatusTypeDef flash_erase_page(flash_region_id_t region, uint32_t offset);

/**
  * @brief  Puts the flash interface in a known state; the OBDH task calls it every time it starts.
  * @details A task deleted during an operation can leave FLASH_CR unlocked with
  *          an operation bit set and the data cache disabled. This waits for the
  *          operation to end (bounded), clears the operation bits and error
  *          flags, locks FLASH_CR, and resets and re-enables the caches. If the
  *          flash is still busy at the limit, nothing is changed.
  */
void flash_interface_reset(void);

/* ---- ECC errors ---- */

/** @brief Flash ECC events since boot. */
typedef struct {
    uint32_t detections;      /**< Double errors outside the program image, each one an NMI. */
    uint32_t corrections;     /**< Correction events the hardware flagged (ECCC); corrections made before the flag is cleared count as one. */
    uint32_t last_address;    /**< Address of the last double error; 0 if none. */
    uint32_t last_corrected;  /**< Address of the last correction event; 0 if none. */
} flash_ecc_stats_t;

/**
  * @brief  Enables the ECC correction interrupt; main() calls it after HAL_Init(), before the driver's first read.
  * @details A correction pending since reset is counted first. The interrupt's
  *          priority is above configMAX_SYSCALL_INTERRUPT_PRIORITY, so kernel
  *          critical sections do not delay it; its handler calls no FreeRTOS API.
  */
void flash_ecc_init(void);

/**
  * @brief  Handles a flash ECC correction; FLASH_IRQHandler calls it.
  * @details The hardware corrects a single-bit error and sets ECCC (RM0351
  *          §3.3.2). While ECCC is set, a double error raises no NMI and
  *          FLASH_ECCR is not updated, so the handler clears the flag at once,
  *          counts the event and records its address. Corrections made while
  *          the flag is set are not counted. There is no rate limit.
  * @todo   Measure whether FLASH_ECCR accesses stall while a program or erase runs.
  */
void flash_ecc_irq(void);

/**
  * @brief  Handles a flash ECC double error; NMI_Handler calls it first.
  * @details A double-bit error raises an NMI (RM0351 §3.3.2). Inside the program
  *          image the MCU resets. Anywhere else the error is counted, the flag
  *          is cleared and the NMI returns; flash_read() then reports HAL_ERROR.
  * @retval true if the NMI was an ECC double error outside the program image;
  *         false if it has another cause.
  */
bool flash_ecc_nmi(void);

/**
  * @brief  Returns the ECC event counters, for flash health telemetry.
  */
flash_ecc_stats_t flash_ecc_stats(void);

#endif /* INC_FLASH_H_ */
