/**
 * @file flash.c
 * @brief Internal flash driver: region-checked read, program and page erase.
 * @details
 * Program and erase follow RM0351 §3.3.6 and §3.3.7, in the order the STM32L4
 * HAL (1.13.6) uses. HAL_FLASH_Unlock(), FLASH_PageErase(), HAL_FLASH_Lock()
 * and the HAL flag and cache macros are called directly. The other steps
 * follow HAL code that cannot be called as is:
 * - unlock_flash_control_register(): HAL_FLASH_Unlock() plus the start of
 *   HAL_FLASH_Program() and HAL_FLASHEx_Erase();
 * - flash_program(): the body of FLASH_Program_DoubleWord();
 * - wait_for_last_operation(): FLASH_WaitForLastOperation();
 * - flush_caches(): FLASH_FlushCaches();
 * - get_page(), get_bank(): GetPage() and GetBank() from ST's flash examples.
 *
 * Differences from the HAL:
 * - a program checks that its whole target is erased before writing anything;
 * - waits are bounded by the datasheet times instead of 50 s, and the erase
 *   wait sleeps;
 * - the unlock keys are written with interrupts disabled;
 * - FLASH_CR is never written while the flash is busy; after a timeout the
 *   driver returns at once and the next operation cleans up;
 * - flush_caches() re-enables the caches that stm32l4xx_hal_conf.h enables,
 *   so a cache left off by a timeout comes back on.
 * @author Medir Segura
 * @date 2023-01-17
 * @note Reworked in 2026-11: region-based access, bounded waits, ECC handling.
 */


#include "flash.h"
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "string.h"
#include "stdio.h"

_Static_assert(FLASH_MAP_PAGE_SIZE == FLASH_PAGE_SIZE, "flash_map.h and the HAL disagree on the page size");

/** @brief Where a region of flash_map.h lies, and whether the software may write it. */
typedef struct {
    uint32_t       base;
    uint32_t       size;
    flash_access_t access;
} flash_region_t;

static const flash_region_t regions[FLASH_REGION_COUNT] = {
#define FLASH_REGION_ENTRY(id, base, size, access) [id] = { (base), (size), (access) },
    FLASH_REGIONS(FLASH_REGION_ENTRY)
#undef FLASH_REGION_ENTRY
};

/* Linker script symbols: the program image ends with the initial values of .data */
extern uint32_t _sidata, _sdata, _edata;

#define SYSTEM_MEMORY_BASE  0x1FFF0000u   /**< System memory (bootloader, OTP), where SYSF_ECC errors lie (RM0351 §3.3.1). */

#define ECC_IRQ_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY - 1)   /**< The least urgent priority that kernel critical sections do not mask; the handler calls no FreeRTOS function. */

static volatile uint32_t ecc_detections;
static volatile uint32_t ecc_corrections;
static volatile uint32_t ecc_last_address;
static volatile uint32_t ecc_last_corrected;

static bool region_range(flash_region_id_t region, uint32_t offset, uint32_t n_bytes, bool write, uint32_t *addr);
static uint32_t get_page(uint32_t Addr);
static uint32_t get_bank(uint32_t Addr);
static HAL_StatusTypeDef unlock_flash_control_register(void);
static HAL_StatusTypeDef wait_for_last_operation(uint32_t limit_ms, bool sleep);
static HAL_StatusTypeDef wait_while_busy(uint32_t limit_ms, bool sleep);
static void flush_caches(bool instruction);
static HAL_StatusTypeDef copy_checked(uint8_t *data, uint32_t addr, uint32_t n_bytes);
static void clear_ecc_correction(void);
static uint32_t ecc_address(uint32_t eccr);


uint32_t flash_region_size(flash_region_id_t region) {

    return ((uint32_t)region < (uint32_t)FLASH_REGION_COUNT) ? regions[region].size : 0u;
}

HAL_StatusTypeDef flash_read(flash_region_id_t region, uint32_t offset, uint8_t *data, uint32_t n_bytes) {

    uint32_t data_addr;
    if (!region_range(region, offset, n_bytes, false, &data_addr)) {
        return HAL_ERROR;
    }

    return copy_checked(data, data_addr, n_bytes);
}

/* ---- Program and erase ---- */

HAL_StatusTypeDef flash_program(flash_region_id_t region, uint32_t offset, const uint8_t *data, uint32_t n_bytes) {

    uint32_t addr;
    if (!region_range(region, offset, n_bytes, true, &addr)) {
        printf("Flash program refused: region %d, offset 0x%05lX, %lu bytes\n", (int)region, (unsigned long)offset, (unsigned long)n_bytes);
        return HAL_ERROR;
    }
    /* Only whole doublewords, at doubleword addresses, can be programmed (RM0351 §3.3.7) */
    if (((addr % 8u) != 0u) || ((n_bytes % 8u) != 0u)) {
        return HAL_ERROR;
    }
    if (n_bytes == 0u) {
        return HAL_OK;
    }

    /* The whole target must be erased. The chip checks each doubleword only as
       it programs it (PROGERR, RM0351 §3.3.7) and accepts all-zero data over
       anything, so check first: a refused program writes nothing. */
    for (uint32_t i = 0u; i < n_bytes; i += 8u) {
        uint64_t current;
        memcpy(&current, (const void *)(addr + i), sizeof(current));
        if (current != UINT64_MAX) {
            printf("Flash program refused: 0x%08lX is not erased\n", (unsigned long)(addr + i));
            return HAL_ERROR;
        }
    }

    HAL_StatusTypeDef status = unlock_flash_control_register();
    if (status != HAL_OK) {
        return status;
    }

    /* As HAL_FLASH_Program(): the data cache is off while programming */
    __HAL_FLASH_DATA_CACHE_DISABLE();
    SET_BIT(FLASH->CR, FLASH_CR_PG);   // RM0351 §3.3.7 step 3

    uint32_t i;
    for (i = 0u; (i < n_bytes) && (status == HAL_OK); i += 8u) {
        uint32_t words[2];
        memcpy(words, data + i, sizeof(words));

        /* Step 4, as FLASH_Program_DoubleWord(): writing the second word starts the programming */
        *(__IO uint32_t *)(addr + i) = words[0];
        __ISB();   // keeps the two writes in order, as in the HAL
        *(__IO uint32_t *)(addr + i + 4u) = words[1];

        /* Steps 5 and 6. A doubleword takes under 0.1 ms, so the wait does not
           sleep. An error flag (RM0351 §3.7.5) stops the program here. */
        status = wait_for_last_operation(FLASH_PROGRAM_LIMIT_MS, false);
    }

    if (status != HAL_TIMEOUT) {             // writing FLASH_CR while busy would stall the bus
        CLEAR_BIT(FLASH->CR, FLASH_CR_PG);   // step 7
        flush_caches(false);                 // as HAL_FLASH_Program(): the data cache only
        HAL_FLASH_Lock();
    }

    if (status != HAL_OK) {
        printf("Flash program failed at address 0x%08lX, status %d\n", (unsigned long)(addr + i - 8u), (int)status);
    }
    return status;
}

HAL_StatusTypeDef flash_erase_page(flash_region_id_t region, uint32_t offset) {

    uint32_t addr;
    if (!region_range(region, offset, 1u, true, &addr)) {
        printf("Flash erase refused: region %d, offset 0x%05lX\n", (int)region, (unsigned long)offset);
        return HAL_ERROR;
    }
    /* Regions start on a page boundary (flash_map.h), so this whole page lies in the region */
    uint32_t page = get_page(addr);
    uint32_t bank = get_bank(addr);

    HAL_StatusTypeDef status = unlock_flash_control_register();
    if (status != HAL_OK) {
        return status;
    }

    /* As HAL_FLASHEx_Erase(): the data cache is off while erasing */
    __HAL_FLASH_DATA_CACHE_DISABLE();

    /* RM0351 §3.3.6 steps 3 and 4: BKER selects the bank and PNB the page inside
       it (§3.7.6), then PER and STRT start the erase. FLASH_PageErase() writes
       FLASH_CR directly, so the controller must already be idle and unlocked. */
    FLASH_PageErase(page, bank);

    /* Step 5. Up to 24.47 ms: the task sleeps between checks while other tasks
       run from bank 1 (read-while-write, RM0351 §3.3.8). */
    status = wait_for_last_operation(FLASH_ERASE_LIMIT_MS, true);

    if (status != HAL_TIMEOUT) {                             // writing FLASH_CR while busy would stall the bus
        CLEAR_BIT(FLASH->CR, FLASH_CR_PER | FLASH_CR_PNB);   // as HAL_FLASHEx_Erase()
        flush_caches(true);                                  // as HAL_FLASHEx_Erase(): both caches
        HAL_FLASH_Lock();
    }

    if (status != HAL_OK) {
        printf("Flash erase failed: page %lu of bank %lu, status %d\n", (unsigned long)page, (unsigned long)bank, (int)status);
    }
    return status;
}

void flash_interface_reset(void) {

    /* Finish the steps a task deleted during flash_program() or
       flash_erase_page() skipped. The wait clears any error flag; only a
       timeout matters here. */
    if (wait_for_last_operation(FLASH_ERASE_LIMIT_MS, true) == HAL_TIMEOUT) {
        return;   // still busy: FLASH_CR cannot be written, and the data cache must stay off
    }

    /* PG, PER and PNB can only be left set while FLASH_CR is unlocked */
    if (READ_BIT(FLASH->CR, FLASH_CR_LOCK) == 0u) {
        CLEAR_BIT(FLASH->CR, FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_PNB);
        HAL_FLASH_Lock();
    }
    flush_caches(true);
}

/* ---- ECC errors ---- */

void flash_ecc_init(void) {

    clear_ecc_correction();            // one left since reset: the startup code copies .data out of flash
    FLASH->ECCR = FLASH_ECCR_ECCIE;    // the flags clear on a 1, so the 0 written to them leaves them as they are
    HAL_NVIC_SetPriority(FLASH_IRQn, ECC_IRQ_PRIORITY, 0u);
    HAL_NVIC_EnableIRQ(FLASH_IRQn);
}

void flash_ecc_irq(void) {

    clear_ecc_correction();
}

bool flash_ecc_nmi(void) {

    uint32_t eccr = FLASH->ECCR;
    if ((eccr & FLASH_ECCR_ECCD) == 0u) {
        return false;
    }

    uint32_t addr = ecc_address(eccr);
    uint32_t image_end = (uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata);
    if ((addr >= FLASH_CODE_BASE) && (addr < image_end)) {
        NVIC_SystemReset();   // the program image is damaged
    }

    ecc_last_address = addr;
    ecc_detections++;
    FLASH->ECCR = (eccr & FLASH_ECCR_ECCIE) | FLASH_ECCR_ECCD;   // write 1 to clear ECCD only
    return true;
}

flash_ecc_stats_t flash_ecc_stats(void) {

    flash_ecc_stats_t stats = {
        .detections     = ecc_detections,
        .corrections    = ecc_corrections,
        .last_address   = ecc_last_address,
        .last_corrected = ecc_last_corrected,
    };
    return stats;
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

  if (write && (r->access == FLASH_ACCESS_READ_ONLY))
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
  * @brief  Gets the page of a given address
  * @details As GetPage() in ST's STM32L4 flash examples: the page number inside
  *          its bank, which FLASH_PageErase() takes.
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
  * @details As GetBank() in ST's STM32L4 flash examples. Takes Flash bank
  *          swapping into account (FB_MODE, RM0351 §9.2.1). This is kept for
  *          now to make the implementation compatible with a possible future
  *          dual-boot configuration.
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

/**
  * @brief  Waits for the flash to be idle, clears stale error flags, unlocks FLASH_CR and clears any leftover PG or PER bit.
  * @details Steps 1 and 2 of RM0351 §3.3.6 and §3.3.7, then the unlock of
  *          §3.3.5; the caller locks FLASH_CR again with HAL_FLASH_Lock().
  *          A write to FLASH_CR while BSY is set stalls the bus (RM0351
  *          §3.3.5), so after a timeout the driver returns without writing
  *          FLASH_CR and leaves the data cache off. The next call waits here
  *          again and clears the PG or PER bit left set, which would otherwise
  *          make the next operation fail with PGSERR (§3.3.7).
  * @retval HAL_OK with FLASH_CR unlocked and no operation selected; HAL_TIMEOUT
  *         if the flash stayed busy; HAL_ERROR if FLASH_CR did not unlock (it
  *         stays locked until reset).
  */
static HAL_StatusTypeDef unlock_flash_control_register(void)
{
  /* Step 1, bounded by the longest operation, an erase */
  if (wait_while_busy(FLASH_ERASE_LIMIT_MS, true) != HAL_OK)
  {
    return HAL_TIMEOUT;
  }

  /* Step 2: a stale error flag would cause PGSERR. The HAL clears it too, but
     then aborts the operation. */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_SR_ERRORS);

  /* Interrupts off while HAL_FLASH_Unlock() writes the two keys: a task deleted
     between them would leave the sequence half written, and the next KEY1 would
     lock FLASH_CR until reset with a hard fault (RM0351 §3.3.5). */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  HAL_StatusTypeDef status = HAL_FLASH_Unlock();
  __set_PRIMASK(primask);
  if (status != HAL_OK)
  {
    return HAL_ERROR;
  }

  /* What the HAL clears after each operation, in case the last one timed out */
  CLEAR_BIT(FLASH->CR, FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_PNB);
  return HAL_OK;
}

/**
  * @brief  Waits for the last operation started and gives its result, as FLASH_WaitForLastOperation() does.
  * @details RM0351 §3.3.7 step 6 checks EOP, but EOP is set only while its
  *          interrupt is enabled (EOPIE, §3.7.5), and the driver never enables
  *          it. As in the HAL, success is BSY clear with no error flag.
  * @param  limit_ms: Longest the operation may take.
  * @param  sleep: true to let the calling task sleep between checks (erase).
  * @retval HAL_OK; HAL_ERROR if an error flag is set (it is cleared);
  *         HAL_TIMEOUT if still busy after limit_ms.
  */
static HAL_StatusTypeDef wait_for_last_operation(uint32_t limit_ms, bool sleep)
{
  if (wait_while_busy(limit_ms, sleep) != HAL_OK)
  {
    return HAL_TIMEOUT;
  }

  uint32_t errors = READ_REG(FLASH->SR) & FLASH_FLAG_SR_ERRORS;
  if (errors != 0u)
  {
    __HAL_FLASH_CLEAR_FLAG(errors);   // the flags clear on a 1 (RM0351 §3.7.5)
    return HAL_ERROR;
  }
  return HAL_OK;
}

/**
  * @brief  Waits until the flash is not busy, at most limit_ms.
  * @details The time comes from HAL_GetTick(), as in the HAL. Sleeping needs
  *          the scheduler, so without it the wait is a plain poll. BSY is read
  *          once more after the limit, because a higher-priority task may have
  *          kept this one from running while the operation ended.
  * @param  limit_ms: Longest wait.
  * @param  sleep: true to sleep one tick between checks.
  * @retval HAL_OK when not busy; HAL_TIMEOUT if still busy after limit_ms.
  */
static HAL_StatusTypeDef wait_while_busy(uint32_t limit_ms, bool sleep)
{
  uint32_t start = HAL_GetTick();
  bool can_sleep = sleep && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);

  while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY) && ((HAL_GetTick() - start) < limit_ms))
  {
    if (can_sleep)
    {
      vTaskDelay(1);
    }
  }
  return __HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY) ? HAL_TIMEOUT : HAL_OK;
}

/**
  * @brief  Resets the caches and enables them again, as FLASH_FlushCaches() does.
  * @details A cache can be reset only while disabled (RM0351 §3.7.1). The HAL
  *          restores the caches that were on before the operation; this enables
  *          those HAL_Init() enables (stm32l4xx_hal_conf.h), so a data cache
  *          left off by a timeout comes back on.
  * @param  instruction: true to flush the instruction cache too, as after an erase.
  */
static void flush_caches(bool instruction)
{
  if (instruction && (INSTRUCTION_CACHE_ENABLE != 0U))
  {
    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
  }
  if (DATA_CACHE_ENABLE != 0U)
  {
    __HAL_FLASH_DATA_CACHE_DISABLE();   // off already after an operation, but flash_interface_reset() can find it on
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
  }
}

/* ---- Reads ---- */

/**
  * @brief  Copies bytes out of flash and reports whether an ECC double error hit them.
  * @details While ECCC is set, a double error raises no NMI (RM0351 §3.3.2), so
  *          a pending correction is cleared first. A double error during the
  *          copy sets ECCD; its NMI (flash_ecc_nmi()) counts it and clears ECCD,
  *          with no guaranteed latency (Arm AN321 §3.6). ECCD is therefore read
  *          before the count: an NMI that already ran shows in the count, one
  *          that has not run yet shows in ECCD. The DSB completes the copy
  *          before ECCD is read (AN321 §2.3); the ISB lets a pending NMI run first.
  * @param  data: Destination buffer.
  * @param  addr: Source address in flash.
  * @param  n_bytes: Number of bytes.
  * @retval HAL_OK; HAL_ERROR if a double error hit the copy, whether or not its NMI has run yet.
  */
static HAL_StatusTypeDef copy_checked(uint8_t *data, uint32_t addr, uint32_t n_bytes)
{
  clear_ecc_correction();
  uint32_t detections = ecc_detections;

  memcpy(data, (const void *)addr, n_bytes);
  __DSB();   // the copy's reads are complete before ECCD is read
  __ISB();   // an NMI that is already pending runs here

  bool flagged = ((FLASH->ECCR & FLASH_ECCR_ECCD) != 0u);   // first: found, its NMI has not run yet
  bool counted = (ecc_detections != detections);            // second: its NMI has run
  return (flagged || counted) ? HAL_ERROR : HAL_OK;
}

/**
  * @brief  Counts, records and clears a pending ECC correction (ECCC), so the next double error raises the NMI.
  * @details The correction interrupt calls it, and so do flash_ecc_init() and
  *          every read. Interrupts are off from reading FLASH_ECCR to clearing
  *          the flag, so no correction is counted twice.
  */
static void clear_ecc_correction(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  uint32_t eccr = FLASH->ECCR;
  if ((eccr & FLASH_ECCR_ECCC) != 0u)
  {
    ecc_last_corrected = ecc_address(eccr);
    ecc_corrections++;
    FLASH->ECCR = (eccr & FLASH_ECCR_ECCIE) | FLASH_ECCR_ECCC;   // write 1 to clear ECCC only
  }

  __set_PRIMASK(primask);
}

/**
  * @brief  Gives the address of the doubleword that an ECC error hit (RM0351 §3.7.7).
  * @param  eccr: FLASH_ECCR, read while its ECCC or ECCD flag is set.
  * @retval The absolute address.
  */
static uint32_t ecc_address(uint32_t eccr)
{
  uint32_t addr = eccr & FLASH_ECCR_ADDR_ECC;   // byte address of the doubleword inside its bank

  if ((eccr & FLASH_ECCR_SYSF_ECC) != 0u)
  {
    return addr + SYSTEM_MEMORY_BASE;
  }
  return addr + (((eccr & FLASH_ECCR_BK_ECC) != 0u) ? FLASH_MAP_BANK2_START : FLASH_CODE_BASE);
}
