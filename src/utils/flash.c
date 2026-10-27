/**
 * @file flash.c
 * @brief Internal flash memory access implementation.
 * @details
 * Implements raw STM32 flash page erase, program and read inside the regions of
 * flash_map.h, plus the old-map write and read kept until step 4.
 * OBDH-mediated request functions live in the OBDH subsystem (obdh_requests.c).
 * @author Medir Segura
 * @date 2023-01-17
 * @note Modified on 2026-10-27.
 */


#include "flash.h"
#include "stm32l4xx_hal.h"
#include "string.h"
#include "stdio.h"

_Static_assert(FLASH_MAP_PAGE_SIZE == FLASH_PAGE_SIZE, "flash_map.h and the HAL disagree on the page size");

/** @brief Where a region of flash_map.h lies, and how it is written. */
typedef struct {
    uint32_t      base;
    uint32_t      size;
    flash_store_t store;
} flash_region_t;

static const flash_region_t regions[FLASH_REGION_COUNT] = {
#define FLASH_REGION_ENTRY(id, base, size, owner, store) [id] = { (base), (size), (store) },
    FLASH_REGIONS(FLASH_REGION_ENTRY)
#undef FLASH_REGION_ENTRY
};

static bool region_range(flash_region_id_t region, uint32_t offset, uint32_t n_bytes, bool write, uint32_t *addr);
static bool old_map_range(uint32_t addr, uint32_t n_bytes);
static uint32_t get_page(uint32_t Addr);
static uint32_t get_bank(uint32_t Addr);


HAL_StatusTypeDef flash_read(flash_region_id_t region, uint32_t offset, uint8_t *data, uint32_t n_bytes) {

    uint32_t data_addr;
    if (!region_range(region, offset, n_bytes, false, &data_addr)) {
        return HAL_ERROR;
    }

    memcpy(data, (const void *)data_addr, n_bytes);
    return HAL_OK;
}

HAL_StatusTypeDef flash_program(flash_region_id_t region, uint32_t offset, const uint8_t *data, uint32_t n_bytes) {

    uint32_t data_addr;
    if (!region_range(region, offset, n_bytes, true, &data_addr)) {
        printf("Flash program refused: region %d, offset 0x%05lX, %lu bytes\n", (int)region, (unsigned long)offset, (unsigned long)n_bytes);
        return HAL_ERROR;
    }
    if (((data_addr % 8u) != 0u) || ((n_bytes % 8u) != 0u)) {
        return HAL_ERROR;
    }
    if (n_bytes == 0u) {
        return HAL_OK;
    }

    /* Check that target is erased */
    for (uint32_t i = 0; i < n_bytes; i += 8u) {
        uint64_t current;
        memcpy(&current, (const void *)(data_addr + i), sizeof(current));
        if (current != UINT64_MAX) {
            return HAL_ERROR;
        }
    }

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    for (uint32_t i = 0; i < n_bytes; i += 8u) {
        uint64_t doubleWord;
        memcpy(&doubleWord, data + i, sizeof(doubleWord));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, data_addr + i, doubleWord) != HAL_OK) {
            HAL_FLASH_Lock();
            printf("Error programming flash at address 0x%08lX, error code %lu\n", (unsigned long)(data_addr + i), (unsigned long)HAL_FLASH_GetError());
            return HAL_ERROR;
        }
    }

    HAL_FLASH_Lock(); // Lock the Flash to disable the flash control register access (protect against unwanted operation).
    return HAL_OK;
}

HAL_StatusTypeDef flash_erase_page(flash_region_id_t region, uint32_t offset) {

    uint32_t page_addr;
    if (!region_range(region, offset, 1u, true, &page_addr)) {
        printf("Flash erase refused: region %d, offset 0x%05lX\n", (int)region, (unsigned long)offset);
        return HAL_ERROR;
    }

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    FLASH_EraseInitTypeDef EraseInitStruct = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Banks     = get_bank(page_addr),
        .Page      = get_page(page_addr),
        .NbPages   = 1,
    };

    uint32_t PAGEError;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError);

    HAL_FLASH_Lock(); // Lock the Flash to disable the flash control register access (protect against unwanted operation).

    if (status != HAL_OK) {
        printf("Error erasing flash page at address 0x%08lX, error code %lu\n", (unsigned long)page_addr, (unsigned long)PAGEError);
    }
    return status;
}

/* ---- Old map: deleted with the old addresses in step 4 ---- */

HAL_StatusTypeDef flash_write(uint32_t data_addr, const uint8_t *data, uint16_t n_bytes) {

    static uint8_t  dataSave[FLASH_PAGE_SIZE];

    if (!old_map_range(data_addr, n_bytes)) {
        printf("Flash write refused: address 0x%08lX, %u bytes, outside the old map pages\n", (unsigned long)data_addr, (unsigned)n_bytes);
        return HAL_ERROR;
    }

	  HAL_FLASH_Unlock(); // Unlock the Flash to enable the flash control register access

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS); // Clear error flags from previous operations. We assume that they have been handled already

    if (n_bytes == 0) {
      HAL_FLASH_Lock();
      return HAL_OK;
    }

    /* Read-modify-write one page at a time, so only a single FLASH_PAGE_SIZE
       RAM buffer is needed no matter how many pages the write spans. */
    uint32_t write_start = data_addr;
    uint32_t write_end   = data_addr + n_bytes; // exclusive end of the write

    for (uint32_t page_addr = write_start & ~(FLASH_PAGE_SIZE - 1);
         page_addr < write_end;
         page_addr += FLASH_PAGE_SIZE) {

        uint32_t page_end = page_addr + FLASH_PAGE_SIZE;

        /* Portion of this page covered by the new data */
        uint32_t chunk_start = (write_start > page_addr) ? write_start : page_addr;
        uint32_t chunk_end   = (write_end   < page_end)  ? write_end   : page_end;
        uint32_t page_offset = chunk_start - page_addr;   // where in the page
        uint32_t data_offset = chunk_start - write_start; // where in the source
        uint32_t chunk_len   = chunk_end - chunk_start;

        /* Preserve the existing page contents, then overlay the new bytes */
        memcpy(dataSave, (const void *)page_addr, FLASH_PAGE_SIZE);
        memcpy(dataSave + page_offset, data + data_offset, chunk_len);

        /* Erase this page (bank/page resolved per page in case of bank crossing) */
        FLASH_EraseInitTypeDef EraseInitStruct = {
            .TypeErase = FLASH_TYPEERASE_PAGES,
            .Banks     = get_bank(page_addr),
            .Page      = get_page(page_addr),
            .NbPages   = 1,
        };

        uint32_t PAGEError;
        if (HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError) != HAL_OK)
        {
            HAL_FLASH_Lock();
            printf("Error erasing flash page at address 0x%08lX, error code %lu\n", (unsigned long)page_addr, (unsigned long)PAGEError);
            return HAL_ERROR;
        }

        /* Write the page back, one doubleword at a time */
        for (uint32_t i = 0; i < FLASH_PAGE_SIZE; i += 8) {
            uint64_t doubleWord;
            memcpy(&doubleWord, dataSave + i, sizeof(doubleWord));
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, page_addr + i, doubleWord) != HAL_OK) {
                HAL_FLASH_Lock();
                printf("Error programming flash at address 0x%08lX, error code %lu\n", (unsigned long)(page_addr + i), (unsigned long)HAL_FLASH_GetError());
                return HAL_ERROR;
            }
        }
    }

    HAL_FLASH_Lock(); // Lock the Flash to disable the flash control register access (protectagainst unwanted operation).
    return HAL_OK;
}

HAL_StatusTypeDef flash_read_old(uint32_t data_addr, uint8_t *data, uint32_t n_bytes) {

    if (!old_map_range(data_addr, n_bytes)) {
        return HAL_ERROR;
    }

    memcpy(data, (const void *)data_addr, n_bytes);
    return HAL_OK;
}

/**
  * @brief  Checks that a range lies inside a region, and gives its address.
  * @param  region: Region of flash_map.h.
  * @param  offset: First byte of the range, counted from the start of the region.
  * @param  n_bytes: Length of the range.
  * @param  write: true for a program or erase, which unit data (written on the ground) refuses.
  * @param  addr: Receives the absolute address of the first byte.
  * @retval true if the whole range lies inside the region.
  */
static bool region_range(flash_region_id_t region, uint32_t offset, uint32_t n_bytes, bool write, uint32_t *addr)
{
  if ((uint32_t)region >= (uint32_t)FLASH_REGION_COUNT)
  {
    return false;
  }

  const flash_region_t *r = &regions[region];

  if (write && (r->store == FLASH_STORE_ONCE))
  {
    return false;
  }
  if ((offset > r->size) || (n_bytes > r->size - offset))
  {
    return false;
  }

  *addr = r->base + offset;
  return true;
}

/**
  * @brief  Checks that a range lies inside the old map pages.
  * @param  addr: First address of the range.
  * @param  n_bytes: Length of the range.
  * @retval true if the whole range lies inside OLD_MAP_BASE .. OLD_MAP_BASE + OLD_MAP_SIZE.
  */
static bool old_map_range(uint32_t addr, uint32_t n_bytes)
{
  return (addr >= OLD_MAP_BASE) && (addr - OLD_MAP_BASE <= OLD_MAP_SIZE) &&
         (n_bytes <= OLD_MAP_SIZE - (addr - OLD_MAP_BASE));
}

/**
  * @brief  Gets the page of a given address
  * @param  Addr: Address of the FLASH Memory
  * @retval The page of a given address
  */
static uint32_t get_page(uint32_t Addr)
{
  uint32_t page = 0;

  if (Addr < (FLASH_BASE + FLASH_BANK_SIZE))
  {
    /* Bank 1 */
    page = (Addr - FLASH_BASE) / FLASH_PAGE_SIZE;
  }
  else
  {
    /* Bank 2 */
    page = (Addr - (FLASH_BASE + FLASH_BANK_SIZE)) / FLASH_PAGE_SIZE;
  }

  return page;
}


/**
  * @brief  Gets the bank of a given address
  * @details Takes Flash bank swapping into account. This is kept for now to
  *          make the implementation compatible with a possible future dual-boot
  *          configuration.
  * @param  Addr: Address of the FLASH Memory
  * @retval The bank of a given address
  */
static uint32_t get_bank(uint32_t Addr)
{
  uint32_t bank = 0;

  if (READ_BIT(SYSCFG->MEMRMP, SYSCFG_MEMRMP_FB_MODE) == 0)
  {
  	/* No Bank swap */
    if (Addr < (FLASH_BASE + FLASH_BANK_SIZE))
    {
      bank = FLASH_BANK_1;
    }
    else
    {
      bank = FLASH_BANK_2;
    }
  }
  else
  {
  	/* Bank swap */
    if (Addr < (FLASH_BASE + FLASH_BANK_SIZE))
    {
      bank = FLASH_BANK_2;
    }
    else
    {
      bank = FLASH_BANK_1;
    }
  }

  return bank;
}