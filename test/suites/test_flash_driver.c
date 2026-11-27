/**
 * @file test_flash_driver.c
 * @brief Host tests for the flash driver: program and erase against the rules of RM0351, the ECC correction interrupt, a read's ECC checks, the region guard and the read-only unit data.
 * @date 2026-11-24
 *
 * @details
 * **What is under test**
 * - src/subsystems/obdh/flash.c, over the fake flash controller below:
 *   - flash_program() and flash_erase_page(), step by step against RM0351
 *     §3.3.6 and §3.3.7 and the HAL they call or follow: the error flags an
 *     earlier operation left cleared first, the two words of each doubleword
 *     in order, the page and the bank of an erase (with the banks swapped
 *     too), an error the chip reports returned and cleared, FLASH_CR locked
 *     again, the data cache off while an operation runs, and the caches
 *     flushed after it as the HAL does (spec FLS-DRV-090);
 *   - a target that is not erased refused before anything is programmed,
 *     all-zero data included (FLS-DRV-120);
 *   - the waits: an operation that stays busy returns HAL_TIMEOUT at its
 *     limit without writing FLASH_CR, and the next operation clears what it
 *     left (spec V13, on the driver); one that lasts exactly its limit
 *     succeeds; the erase wait sleeps and the program wait does not
 *     (FLS-DRV-100);
 *   - flash_interface_reset() after a task deleted in the middle of an erase
 *     or of a refused program, or between operations, when it writes no key
 *     (FLS-REQ-070), and with the flash stuck busy;
 *   - flash_ecc_init() and flash_ecc_irq(): a correction the hardware flags
 *     (ECCC) is counted, its address decoded and recorded, and the flag
 *     cleared at once, with interrupts off from the read of FLASH_ECCR to the
 *     write that clears it (spec FLS-INT-040, FLS-INT-070);
 *   - flash_read(): the same clear before the copy, as a backup; a double
 *     error in the copy reported whether its NMI ran before the check (the
 *     count shows it) or has not run yet (ECCD shows it, and the driver
 *     leaves the flag to the NMI); an error counted before the read does not
 *     fail it (FLS-INT-040);
 *   - flash_ecc_nmi(): a double error outside the program image counted, its
 *     address recorded and ECCD alone cleared; an NMI without a double error
 *     left to the other handlers;
 *   - the correction interrupt stays on during program and erase waits;
 *     unit data, any range outside its region (FLS-DRV-010) and a program
 *     that is not whole doublewords are refused before anything is touched.
 *
 * While ECCC is set, a double error raises no NMI (RM0351 §3.3.2), so a read
 * could return wrong bytes as good ones. That is why the flag is cleared at once.
 *
 * **The fake flash controller** acts as RM0351 says the chip does, and fails
 * the test where the chip would stall or lock up:
 * - FLASH_SR: the error flags clear on a 1 (§3.7.5); BSY is set while an
 *   operation runs, and cannot be written.
 * - FLASH_KEYR: KEY1 then KEY2 unlocks FLASH_CR (§3.3.5). The keys must be
 *   written with interrupts off, and any other sequence fails the test,
 *   because on the chip it locks FLASH_CR until reset with a hard fault.
 * - FLASH_CR: a write while BSY is set fails the test (on the chip it stalls
 *   the bus until the operation ends, §3.3.5), and so does a write that
 *   changes a locked FLASH_CR (the chip ignores it). Setting STRT starts the
 *   erase of the page that BKER and PNB select (§3.7.6), at the address where
 *   FB_MODE in SYSCFG_MEMRMP maps that bank (§9.2.1), unless PG is set too or
 *   an error flag is left, which set PGSERR instead (§3.3.7). A page outside
 *   the data area fails the test.
 * - Programming: the driver writes its doublewords into RAM mapped at the
 *   data area's address (`map_at.c`, see below). At each HAL_GetTick() the
 *   fake compares that RAM with its own copy of the flash: the doubleword
 *   that changed has been written. It sets PGSERR if PG is clear, an erase
 *   is selected or an error flag is left, WRPERR on the doubleword a test
 *   names (`wrperr_at`), as if write-protected, and PROGERR if the doubleword
 *   was not erased and the new data is not all zero (§3.3.7); then it puts
 *   the old content back, as the chip programs nothing. Otherwise the
 *   doubleword is programmed and BSY is set.
 * - FLASH_ACR: a write to ICRST or DCRST while that cache is on fails the
 *   test (§3.7.1).
 * - FLASH_ECCR: at the barrier after a read's copy (`__DSB()`), the fake sets
 *   ECCD and the address of the double error a test names (`double_error_at`),
 *   as the flash interface does when the copy reads it (§3.3.2). With
 *   `nmi_before_check` the NMI runs there, before the check; otherwise it has
 *   not run when flash_read() returns. run_nmi() calls flash_ecc_nmi() and
 *   then clears ECCD, as the 1 the handler writes does on the chip.
 * - Time: each HAL_GetTick() returns the fake RTOS tick and moves it on by
 *   1 ms; vTaskDelay() moves it too, so the ticks a call took, minus its
 *   HAL_GetTick() calls, are the time it slept. An operation lasts `op_ms`
 *   (0 by default: it ends at the next HAL_GetTick()), or forever with
 *   OP_STUCK. Each HAL_GetTick() during an operation checks that the data
 *   cache is off.
 *
 * **What is not under test**
 * - The NMI's reset for a double error inside the program image. The linker
 *   symbols that end the image are pointers the suite sets (an image ending
 *   at 0x08014100), so the data area lies outside it.
 * - An NMI that runs between the check's two reads, ECCD then the count: they
 *   are plain memory reads here, so nothing runs between them. Their order,
 *   which makes the check hold wherever the NMI runs, is kept by review.
 * - Timing on the chip: the operation times, the bus stall itself, and
 *   whether the handler's access to FLASH_ECCR waits while a program or
 *   erase runs, which is measured on the board (spec V15).
 * - What a store does with a timeout or an error: the store suites run on
 *   the fake flash driver.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `flash.c` is `#include`d below. Its HAL is
 *   mocks/stubs_hal_flash/stm32l4xx_hal.h: the FLASH registers are plain
 *   memory (`fake_flash`), with the register bits of the real stm32l476xx.h,
 *   and every write through the register macros goes to
 *   fake_flash_write_reg() below. The driver writes FLASH_ECCR directly, so
 *   after a call FLASH_ECCR holds the last value written, and the tests check
 *   that value: on the chip its flags clear on a 1, so ECCIE|ECCC clears ECCC
 *   and nothing else.
 * - The three HAL functions the driver calls (HAL_FLASH_Unlock(),
 *   HAL_FLASH_Lock(), FLASH_PageErase()) are copies of ST's code below, so
 *   their register writes reach the fake flash controller.
 * - The core and NVIC calls are faked below. They record PRIMASK, the NVIC
 *   priority and enable, and what the driver had done at each call.
 * - `map_at.c` (`SOURCES`) maps RAM at the data area's address, 0x080C0000,
 *   where the driver reads and programs. A test that needs it is ignored if
 *   the host has that address in use.
 * - fake_rtos.c gives the scheduler state, the tick and vTaskDelay().
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_flash_driver
 * @endcode
 */
#include <string.h>

#include "unity.h"
#include "fake_rtos.h"
#include "pocat_test_main.h"
#include "map_at.h"

/* The linker symbols that end the program image are pointers here, renamed so
   they cannot clash with the host linker's own _edata: the driver takes
   &_sidata, which is then the pointer's value, a chip address */
#define _sidata  (*test_sidata)
#define _sdata   (*test_sdata)
#define _edata   (*test_edata)
#include "flash.c"
#undef _sidata
#undef _sdata
#undef _edata

/* A program image whose code and constants end at 0x08014000, followed by the
   initial values of a 256-byte .data: it ends at 0x08014100, below the data area */
uint32_t *test_sidata = (uint32_t *)(uintptr_t)0x08014000u;
uint32_t *test_sdata  = (uint32_t *)(uintptr_t)0x20000000u;
uint32_t *test_edata  = (uint32_t *)(uintptr_t)0x20000100u;

/* From RM0351, not from the driver or the map, so the tests check the driver's arithmetic */
#define RM_BANK1_START  0x08000000u   /**< Without the swap, bank 1 starts at the flash base (RM0351 §9.2.1, FB_MODE = 0). */
#define RM_BANK2_START  0x08080000u   /**< And bank 2 at 512 KB (RM0351 §3.3.1, 1 MB devices); FB_MODE = 1 swaps the two. */
#define RM_PAGE_SIZE    2048u         /**< 2 KB pages (RM0351 §3.3.1). */

#define OP_STUCK        UINT32_MAX    /**< `op_ms` value: the operation never ends. */

/** @brief Flags that make the next operation fail with PGSERR when left set (RM0351 §3.3.7). */
#define FLAGS_SETTING_PGSERR  (FLASH_SR_PROGERR | FLASH_SR_SIZERR | FLASH_SR_PGAERR | FLASH_SR_MISERR | \
                               FLASH_SR_FASTERR | FLASH_SR_PGSERR)

/* ---- The data area ---- */

static uint8_t *data_area;                    /**< RAM at FLASH_DATA_BASE; NULL until a test needs it, or if the host cannot map it there. */
static bool     data_area_tried;              /**< map_at() already called. */
static uint8_t  flash_copy[FLASH_DATA_SIZE];  /**< What the fake flash holds, to tell which doubleword the driver wrote. */

/** @brief Returns true if the @p n bytes at @p p all equal @p value. */
static bool all_bytes(const uint8_t *p, uint8_t value, uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++) {
        if (p[i] != value) {
            return false;
        }
    }
    return true;
}

/** @brief The RAM, mapped at the data area's address, that stands for the flash at @p addr. */
static uint8_t *bytes_at(uint32_t addr)
{
    return data_area + (addr - FLASH_DATA_BASE);
}

/* ---- Fake flash controller ---- */

FLASH_TypeDef fake_flash;
SYSCFG_TypeDef fake_syscfg;

static bool       keys_refused;          /**< FLASH_CR stays locked whatever is written to FLASH_KEYR. */
static bool       key1_written;          /**< KEY1 written, KEY2 expected next. */
static int        key_writes;            /**< Writes to FLASH_KEYR. */

static uint32_t   op_ms;                 /**< How long the next operation lasts; OP_STUCK: forever. */
static uint32_t   erase_error;           /**< Error flags the next erase ends with, as if the chip reported them; 0: none. */
static uint32_t   wrperr_at;             /**< The doubleword the chip refuses with WRPERR, as if write-protected: 1 = the first the test writes; 0: none. */
static bool       op_running;            /**< An operation runs: BSY is set. */
static bool       op_stuck;              /**< It never ends. */
static bool       op_erases;             /**< It is an erase. */
static uint32_t   op_page_addr;          /**< The address of the page it erases. */
static TickType_t op_end;                /**< The tick at which it ends. */
static uint32_t   erases;                /**< Erases started. */
static uint32_t   cr_at_erase;           /**< FLASH_CR when the last one started. */
static uint32_t   doublewords_written;   /**< Doublewords the driver wrote, programmed or refused. */
static uint32_t   programs;              /**< Doublewords programmed. */
static uint32_t   icache_resets;         /**< Instruction cache resets (ICRST set). */
static uint32_t   dcache_resets;         /**< Data cache resets (DCRST set). */
static uint32_t   dcache_on_while_busy;  /**< Times an operation started, or was found running, with the data cache on. */

/** @brief Starts an operation: BSY set for `op_ms`. The data cache must be off. */
static void start_operation_in_chip(bool erase, uint32_t page_addr)
{
    op_running = true;
    op_stuck = (op_ms == OP_STUCK);
    op_erases = erase;
    op_page_addr = page_addr;
    op_end = xTaskGetTickCount() + (op_stuck ? 0u : op_ms);
    FLASH->SR |= FLASH_SR_BSY;
    if ((FLASH->ACR & FLASH_ACR_DCEN) != 0u) {
        dcache_on_while_busy++;
    }
}

/** @brief Ends the operation: BSY and STRT clear, and an erase erases its page unless it ends with an error. */
static void end_operation_in_chip(void)
{
    op_running = false;
    FLASH->SR &= ~FLASH_SR_BSY;
    FLASH->CR &= ~FLASH_CR_STRT;   // cleared with BSY (RM0351 §3.7.6)
    if (!op_erases) {
        return;
    }
    if (erase_error != 0u) {
        FLASH->SR |= erase_error;   // the erase is aborted (RM0351 §3.3.7)
        erase_error = 0u;
        return;
    }
    if (data_area != NULL) {
        memset(bytes_at(op_page_addr), 0xFF, RM_PAGE_SIZE);
        memset(flash_copy + (op_page_addr - FLASH_DATA_BASE), 0xFF, RM_PAGE_SIZE);
    }
}

/** @brief Programs the doubleword the driver wrote since the last check, as the chip does (RM0351 §3.3.7). */
static void program_written_doubleword(void)
{
    if ((data_area == NULL) || (memcmp(data_area, flash_copy, FLASH_DATA_SIZE) == 0)) {
        return;
    }

    uint32_t written = 0u;
    uint32_t i = 0u;
    for (uint32_t j = 0u; j < FLASH_DATA_SIZE; j += 8u) {
        if (memcmp(data_area + j, flash_copy + j, 8u) != 0) {
            written++;
            i = j;
        }
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, written, "several doublewords written without waiting for the flash");
    TEST_ASSERT_FALSE_MESSAGE(op_running, "doubleword written while the flash is busy");
    doublewords_written++;

    uint32_t error = 0u;
    if (((FLASH->CR & FLASH_CR_PG) == 0u) ||
        ((FLASH->CR & (FLASH_CR_PER | FLASH_CR_MER1 | FLASH_CR_MER2)) != 0u) ||
        ((FLASH->SR & FLAGS_SETTING_PGSERR) != 0u)) {
        error = FLASH_SR_PGSERR;
    } else if (doublewords_written == wrperr_at) {
        error = FLASH_SR_WRPERR;   // as if the page were write-protected (RM0351 §3.7.5)
    } else if (!all_bytes(flash_copy + i, 0xFFu, 8u) && !all_bytes(data_area + i, 0x00u, 8u)) {
        error = FLASH_SR_PROGERR;
    }
    if (error != 0u) {
        memcpy(data_area + i, flash_copy + i, 8u);   // the chip programs nothing
        FLASH->SR |= error;
        return;
    }

    memcpy(flash_copy + i, data_area + i, 8u);
    programs++;
    start_operation_in_chip(false, 0u);
}

/** @brief A write to FLASH_CR: refused while busy or locked; STRT starts an erase. */
static void write_cr(uint32_t value)
{
    TEST_ASSERT_TRUE_MESSAGE((FLASH->SR & FLASH_SR_BSY) == 0u,
                             "FLASH_CR written while BSY is set: on the chip the bus stalls until the operation ends (RM0351 §3.3.5)");
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(FLASH->CR, value, "FLASH_CR written while locked: the chip ignores the write");
        return;
    }

    bool start = ((value & FLASH_CR_STRT) != 0u) && ((FLASH->CR & FLASH_CR_STRT) == 0u);
    FLASH->CR = value;
    if (!start) {
        return;
    }

    TEST_ASSERT_TRUE_MESSAGE((value & FLASH_CR_PER) != 0u, "STRT set without PER: unpredictable on the chip (RM0351 §3.7.6)");
    TEST_ASSERT_TRUE_MESSAGE((value & (FLASH_CR_MER1 | FLASH_CR_MER2)) == 0u, "mass erase started");
    if (((value & (FLASH_CR_PG | FLASH_CR_FSTPG)) != 0u) || ((FLASH->SR & FLAGS_SETTING_PGSERR) != 0u)) {
        FLASH->SR |= FLASH_SR_PGSERR;   // the erase does not start (RM0351 §3.3.7)
        FLASH->CR &= ~FLASH_CR_STRT;
        return;
    }

    /* BKER selects the bank and PNB the page inside it (RM0351 §3.7.6); FB_MODE
       says where each bank is mapped (§9.2.1) */
    bool bank2 = ((value & FLASH_CR_BKER) != 0u);
    bool swapped = ((SYSCFG->MEMRMP & SYSCFG_MEMRMP_FB_MODE) != 0u);
    uint32_t page_addr = ((bank2 != swapped) ? RM_BANK2_START : RM_BANK1_START) +
                         ((value & FLASH_CR_PNB) >> FLASH_CR_PNB_Pos) * RM_PAGE_SIZE;
    TEST_ASSERT_TRUE_MESSAGE(page_addr >= FLASH_DATA_BASE, "erase outside the data area: the bank or page is wrong");
    erases++;
    cr_at_erase = value;
    start_operation_in_chip(true, page_addr);
}

/** @brief A write to FLASH_ACR: a cache reset bit can be written only while that cache is off (RM0351 §3.7.1). */
static void write_acr(uint32_t value)
{
    uint32_t changed = FLASH->ACR ^ value;
    if ((changed & FLASH_ACR_ICRST) != 0u) {
        TEST_ASSERT_TRUE_MESSAGE(((FLASH->ACR | value) & FLASH_ACR_ICEN) == 0u, "ICRST written with the instruction cache on (RM0351 §3.7.1)");
        icache_resets += ((value & FLASH_ACR_ICRST) != 0u) ? 1u : 0u;
    }
    if ((changed & FLASH_ACR_DCRST) != 0u) {
        TEST_ASSERT_TRUE_MESSAGE(((FLASH->ACR | value) & FLASH_ACR_DCEN) == 0u, "DCRST written with the data cache on (RM0351 §3.7.1)");
        dcache_resets += ((value & FLASH_ACR_DCRST) != 0u) ? 1u : 0u;
    }
    FLASH->ACR = value;
}

/**
 * @brief Writes a flash register as the chip does; every register macro of the stub calls it.
 * @details FLASH_SR: the error flags written as 1 clear (RM0351 §3.7.5).
 * FLASH_KEYR: KEY1 then KEY2 unlocks FLASH_CR (§3.3.5). FLASH_CR and
 * FLASH_ACR: see write_cr() and write_acr(). Any other register keeps the
 * value written.
 */
void fake_flash_write_reg(volatile uint32_t *reg, uint32_t value)
{
    if (reg == &FLASH->SR) {
        FLASH->SR &= ~(value & (FLASH_FLAG_SR_ERRORS | FLASH_SR_EOP));
    } else if (reg == &FLASH->KEYR) {
        key_writes++;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, __get_PRIMASK(), "FLASH_KEYR written with interrupts on");
        if (keys_refused) {
            return;
        }
        TEST_ASSERT_TRUE_MESSAGE((FLASH->CR & FLASH_CR_LOCK) != 0u, "key written while FLASH_CR is unlocked");
        if (!key1_written && (value == FLASH_KEY1)) {
            key1_written = true;
        } else if (key1_written && (value == FLASH_KEY2)) {
            key1_written = false;
            FLASH->CR &= ~FLASH_CR_LOCK;
        } else {
            TEST_FAIL_MESSAGE("wrong key sequence: on the chip FLASH_CR locks until reset");
        }
    } else if (reg == &FLASH->CR) {
        write_cr(value);
    } else if (reg == &FLASH->ACR) {
        write_acr(value);
    } else {
        *reg = value;
    }
}

/* ---- The HAL functions the driver calls ---- */

/* ST's code from the STM32L4 HAL 1.13.6 in external/, kept in ST's layout so
   it can be compared line by line: what an STM32L476 compiles of it (the
   dual-bank branch of FLASH_PageErase()), without the assert_param() checks,
   which stm32l4xx_hal_conf.h turns off. Their writes go to the fake
   controller above, as on the chip they go to the registers, so the tests
   still check how the driver calls them: the page and bank it passes, and
   that FLASH_CR is neither busy nor locked when they write it. */

/** @brief HAL_FLASH_Unlock(), from stm32l4xx_hal_flash.c. */
HAL_StatusTypeDef HAL_FLASH_Unlock(void)
{
  HAL_StatusTypeDef status = HAL_OK;

  if(READ_BIT(FLASH->CR, FLASH_CR_LOCK) != 0U)
  {
    /* Authorize the FLASH Registers access */
    WRITE_REG(FLASH->KEYR, FLASH_KEY1);
    WRITE_REG(FLASH->KEYR, FLASH_KEY2);

    /* Verify Flash is unlocked */
    if(READ_BIT(FLASH->CR, FLASH_CR_LOCK) != 0U)
    {
      status = HAL_ERROR;
    }
  }

  return status;
}

/** @brief HAL_FLASH_Lock(), from stm32l4xx_hal_flash.c. */
HAL_StatusTypeDef HAL_FLASH_Lock(void)
{
  /* Set the LOCK Bit to lock the FLASH Registers access */
  SET_BIT(FLASH->CR, FLASH_CR_LOCK);

  return HAL_OK;
}

/** @brief FLASH_PageErase(), from stm32l4xx_hal_flash_ex.c. */
void FLASH_PageErase(uint32_t Page, uint32_t Banks)
{
  if((Banks & FLASH_BANK_1) != 0U)
  {
    CLEAR_BIT(FLASH->CR, FLASH_CR_BKER);
  }
  else
  {
    SET_BIT(FLASH->CR, FLASH_CR_BKER);
  }

  /* Proceed to erase the page */
  MODIFY_REG(FLASH->CR, FLASH_CR_PNB, ((Page & 0xFFU) << FLASH_CR_PNB_Pos));
  SET_BIT(FLASH->CR, FLASH_CR_PER);
  SET_BIT(FLASH->CR, FLASH_CR_STRT);
}

/* ---- Fake core and NVIC ---- */

static uint32_t primask;                     /**< 1 while interrupts are off. */
static int      irq_off_calls;               /**< __disable_irq() calls. */
static uint32_t corrections_when_irqs_off;   /**< Corrections counted when interrupts last went off. */
static uint32_t corrections_when_irqs_on;    /**< Corrections counted when __set_PRIMASK() last turned them back on. */
static uint32_t eccr_when_irqs_on;           /**< FLASH_ECCR at that moment. */
static uint32_t corrections_at_dsb;          /**< Corrections counted at the barrier after flash_read()'s copy. */
static uint32_t double_error_at;             /**< Address of a double error that flash_read()'s copy reads: ECCD is set at the barrier after the copy; 0: none. */
static bool     nmi_before_check;            /**< Its NMI runs there, before the check; false: it has not run when flash_read() returns. */
static bool     nvic_enabled;                /**< FLASH_IRQn enabled in the NVIC. */
static int      nvic_priority;               /**< Its preemption priority; -1 if never set. */
static int      nvic_sub_priority;           /**< Its sub-priority; -1 if never set. */
static uint32_t waits;                       /**< HAL_GetTick() calls: the driver waiting for the flash. */
static uint32_t waits_with_ecc_irq_off;      /**< Those made with the correction interrupt off. */
static const uint8_t *isb_watch;             /**< 16 bytes that __ISB() records, or NULL. */
static uint8_t  isb_bytes[2][16];            /**< Those bytes at the first two __ISB() calls. */
static uint32_t isb_cr[2];                   /**< FLASH_CR at those calls. */
static uint32_t isb_calls;                   /**< __ISB() calls. */

uint32_t __get_PRIMASK(void)
{
    return primask;
}

void __set_PRIMASK(uint32_t value)
{
    if (value == 0u) {
        corrections_when_irqs_on = flash_ecc_stats().corrections;
        eccr_when_irqs_on = FLASH->ECCR;
    }
    primask = value;
}

void __disable_irq(void)
{
    corrections_when_irqs_off = flash_ecc_stats().corrections;
    primask = 1u;
    irq_off_calls++;
}

/**
 * @brief Runs the NMI as NMI_Handler does for a double error outside the program image, and clears ECCD as the chip does.
 * @details The handler must write 1 to ECCD alone. FLASH_ECCR keeps the value
 * written (see the stub), so the clear that this 1 makes on the chip is made
 * here; the address bits stay, as the chip updates them only at a new error.
 */
static void run_nmi(void)
{
    uint32_t eccr = FLASH->ECCR;
    TEST_ASSERT_TRUE(flash_ecc_nmi());
    TEST_ASSERT_EQUAL_HEX32((eccr & FLASH_ECCR_ECCIE) | FLASH_ECCR_ECCD, FLASH->ECCR);
    FLASH->ECCR = eccr & ~FLASH_ECCR_ECCD;
}

void __DSB(void)
{
    corrections_at_dsb = flash_ecc_stats().corrections;
    if (double_error_at != 0u) {
        /* The copy read it: the flash interface sets ECCD and the doubleword's address in its bank */
        FLASH->ECCR |= FLASH_ECCR_ECCD | FLASH_ECCR_BK_ECC | (double_error_at - RM_BANK2_START);
        if (nmi_before_check) {
            run_nmi();
        }
    }
}

void __ISB(void)
{
    if ((isb_watch != NULL) && (isb_calls < 2u)) {
        memcpy(isb_bytes[isb_calls], isb_watch, 16u);
        isb_cr[isb_calls] = FLASH->CR;
    }
    isb_calls++;
}

void NVIC_SystemReset(void)
{
    TEST_FAIL_MESSAGE("unexpected system reset");
}

void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t preempt_priority, uint32_t sub_priority)
{
    TEST_ASSERT_EQUAL_INT(FLASH_IRQn, irq);
    nvic_priority = (int)preempt_priority;
    nvic_sub_priority = (int)sub_priority;
}

void HAL_NVIC_EnableIRQ(IRQn_Type irq)
{
    TEST_ASSERT_EQUAL_INT(FLASH_IRQn, irq);
    nvic_enabled = true;
}

void HAL_NVIC_DisableIRQ(IRQn_Type irq)
{
    TEST_ASSERT_EQUAL_INT(FLASH_IRQn, irq);
    nvic_enabled = false;
}

/**
 * @brief Returns the tick and moves it on by 1 ms; meanwhile the fake chip programs what was written and ends an operation whose time is up.
 */
uint32_t HAL_GetTick(void)
{
    TickType_t now = xTaskGetTickCount();
    waits++;
    if (!nvic_enabled || ((FLASH->ECCR & FLASH_ECCR_ECCIE) == 0u)) {
        waits_with_ecc_irq_off++;
    }

    program_written_doubleword();
    if (op_running) {
        if ((FLASH->ACR & FLASH_ACR_DCEN) != 0u) {
            dcache_on_while_busy++;
        }
        if (!op_stuck && ((int32_t)(now - op_end) >= 0)) {
            end_operation_in_chip();
        }
    }

    fake_rtos_advance_ticks(1u);
    return now;
}

/* ---- Helpers ---- */

/**
 * @brief Gives the RAM mapped at the data area's address, erased (all 0xFF).
 * @details Maps it the first time. Ignores the calling test if the host has
 * that address in use.
 */
static uint8_t *require_data_area(void)
{
    if (!data_area_tried) {
        data_area_tried = true;
        data_area = map_at(FLASH_DATA_BASE, FLASH_DATA_SIZE);
    }
    if (data_area == NULL) {
        TEST_IGNORE_MESSAGE("the host has 0x080C0000 in use, so the data area cannot be mapped there");
    }
    memset(data_area, 0xFF, FLASH_DATA_SIZE);
    memset(flash_copy, 0xFF, FLASH_DATA_SIZE);
    return data_area;
}

/** @brief Puts @p n bytes into the fake flash at @p addr, as if programmed before the test. */
static void preload(uint32_t addr, const void *bytes, uint32_t n)
{
    memcpy(bytes_at(addr), bytes, n);
    memcpy(flash_copy + (addr - FLASH_DATA_BASE), bytes, n);
}

/** @brief Fills the page at @p addr of the fake flash with @p value, as if programmed before the test. */
static void preload_page(uint32_t addr, uint8_t value)
{
    memset(bytes_at(addr), value, RM_PAGE_SIZE);
    memset(flash_copy + (addr - FLASH_DATA_BASE), value, RM_PAGE_SIZE);
}

/**
 * @brief Leaves the chip as a task deleted in the middle of an erase leaves it.
 * @details The erase of the page at @p page_addr runs for @p ms more (or
 * forever with OP_STUCK), FLASH_CR is unlocked with the erase bits set, and
 * the data cache is off.
 */
static void leave_an_erase_running(uint32_t page_addr, uint32_t ms)
{
    uint32_t page = (page_addr - RM_BANK2_START) / RM_PAGE_SIZE;
    FLASH->CR = (FLASH->CR & ~FLASH_CR_LOCK) | FLASH_CR_BKER | (page << FLASH_CR_PNB_Pos) | FLASH_CR_PER | FLASH_CR_STRT;
    FLASH->ACR &= ~FLASH_ACR_DCEN;
    op_ms = ms;
    start_operation_in_chip(true, page_addr);
    op_ms = 0u;
}

/** @brief Lets a stuck operation end at the next HAL_GetTick(). */
static void unstick(void)
{
    op_stuck = false;
    op_end = xTaskGetTickCount();
}

/** @brief The ticks the driver slept since @p t0, when HAL_GetTick() had been called @p waits0 times. */
static uint32_t slept_since(TickType_t t0, uint32_t waits0)
{
    return (uint32_t)(xTaskGetTickCount() - t0) - (waits - waits0);
}

/**
 * @brief Raises the correction interrupt with FLASH_ECCR holding @p eccr, and checks what the handler did.
 * @details One more correction counted, at @p address. ECCIE|ECCC written,
 * which on the chip clears ECCC and nothing else. The count and the write
 * made with interrupts off, and PRIMASK back to 0 after. The double-error
 * counters untouched.
 */
static void check_correction(uint32_t eccr, uint32_t address)
{
    flash_ecc_stats_t before = flash_ecc_stats();
    int offs = irq_off_calls;
    FLASH->ECCR = eccr;

    flash_ecc_irq();

    flash_ecc_stats_t after = flash_ecc_stats();
    TEST_ASSERT_EQUAL_UINT32(before.corrections + 1u, after.corrections);
    TEST_ASSERT_EQUAL_HEX32(address, after.last_corrected);
    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC, FLASH->ECCR);
    TEST_ASSERT_EQUAL_INT(offs + 1, irq_off_calls);
    TEST_ASSERT_EQUAL_UINT32(before.corrections, corrections_when_irqs_off);
    TEST_ASSERT_EQUAL_UINT32(after.corrections, corrections_when_irqs_on);
    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC, eccr_when_irqs_on);
    TEST_ASSERT_EQUAL_UINT32(0u, primask);
    TEST_ASSERT_EQUAL_UINT32(before.detections, after.detections);
    TEST_ASSERT_EQUAL_HEX32(before.last_address, after.last_address);
}

/**
 * @brief Puts the registers in their reset state (FLASH_CR locked, caches on as HAL_Init() leaves them), the fake chip idle with its flash erased, and the fakes and flash.c's ECC counters (`ecc_*`) back to 0, so each test counts from 0.
 */
void setUp(void)
{
    fake_rtos_reset();

    memset(&fake_flash, 0, sizeof(fake_flash));
    FLASH->CR = FLASH_CR_LOCK | FLASH_CR_OPTLOCK;
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    memset(&fake_syscfg, 0, sizeof(fake_syscfg));   // FB_MODE = 0: the banks are not swapped
    keys_refused = false;
    key1_written = false;
    key_writes = 0;

    op_ms = 0u;
    erase_error = 0u;
    wrperr_at = 0u;
    op_running = false;
    op_stuck = false;
    erases = 0u;
    cr_at_erase = 0u;
    doublewords_written = 0u;
    programs = 0u;
    icache_resets = 0u;
    dcache_resets = 0u;
    dcache_on_while_busy = 0u;
    if (data_area != NULL) {
        memset(data_area, 0xFF, FLASH_DATA_SIZE);
        memset(flash_copy, 0xFF, FLASH_DATA_SIZE);
    }

    primask = 0u;
    irq_off_calls = 0;
    corrections_when_irqs_off = 0u;
    corrections_when_irqs_on = 0u;
    eccr_when_irqs_on = 0u;
    corrections_at_dsb = 0u;
    double_error_at = 0u;
    nmi_before_check = false;
    nvic_enabled = false;
    nvic_priority = -1;
    nvic_sub_priority = -1;
    waits = 0u;
    waits_with_ecc_irq_off = 0u;
    isb_watch = NULL;
    isb_calls = 0u;

    ecc_detections = 0u;
    ecc_corrections = 0u;
    ecc_last_address = 0u;
    ecc_last_corrected = 0u;
}

/** @brief Checks that the test left interrupts on and the fake RTOS balanced, and that no operation ran with the data cache on. */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, primask);
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_critical_depth());
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_suspend_all_depth());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, dcache_on_while_busy, "an operation ran with the data cache on (HAL_FLASH_Program(), HAL_FLASHEx_Erase())");
}

/* ---- The correction interrupt ---- */

/**
 * @brief With no ECC event pending, the init enables the correction interrupt at the priority just above the kernel's mask, and counts nothing.
 * @details Kernel critical sections mask configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
 * (5) and every less urgent priority. 4 is the least urgent priority they do
 * not mask: a critical section never delays the clear, and more urgent
 * interrupts keep precedence over it. main() calls the init before the
 * driver's first read (FLS-INT-040).
 */
void test_ecc_init_enables_the_interrupt_above_the_kernel_mask(void)
{
    flash_ecc_init();

    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCIE, FLASH->ECCR);
    TEST_ASSERT_TRUE(nvic_enabled);
    TEST_ASSERT_EQUAL_INT(configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY - 1, nvic_priority);
    TEST_ASSERT_EQUAL_INT(0, nvic_sub_priority);
    TEST_ASSERT_EQUAL_UINT32(0u, flash_ecc_stats().corrections);
}

/**
 * @brief A correction left pending since reset is counted, with its address, and cleared before the interrupt is enabled.
 * @details The startup code copies .data out of flash before main() runs,
 * with no interrupt enabled to take a correction there. Left set, its flag
 * would block the NMI of the next double error. The clear writes ECCC alone,
 * as ECCIE is still 0; then the init writes ECCIE, with 0 on the flags.
 */
void test_ecc_init_counts_a_correction_left_since_reset(void)
{
    FLASH->ECCR = FLASH_ECCR_ECCC | 0x00008u;

    flash_ecc_init();

    TEST_ASSERT_EQUAL_UINT32(1u, flash_ecc_stats().corrections);
    TEST_ASSERT_EQUAL_HEX32(0x08000008u, flash_ecc_stats().last_corrected);
    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCC, eccr_when_irqs_on);
    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCIE, FLASH->ECCR);
    TEST_ASSERT_TRUE(nvic_enabled);
}

/**
 * @brief The init leaves a pending double error to the NMI: it neither counts it as a correction nor clears it.
 * @details The NMI handles double errors. The init writes 0 to the ECCD
 * flag, which leaves it set on the chip.
 */
void test_ecc_init_leaves_a_double_error_to_the_nmi(void)
{
    FLASH->ECCR = FLASH_ECCR_ECCD | 0x00010u;

    flash_ecc_init();

    TEST_ASSERT_EQUAL_UINT32(0u, flash_ecc_stats().corrections);
    TEST_ASSERT_EQUAL_HEX32(0u, flash_ecc_stats().last_corrected);
    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCIE, FLASH->ECCR);
}

/**
 * @brief A correction in bank 1 is counted at its address, and its flag cleared with interrupts off.
 * @details FLASH_ECCR gives the byte address inside the bank (RM0351 §3.7.7);
 * bank 1 starts at 0x08000000. Interrupts are off from the read of FLASH_ECCR
 * to the write that clears the flag, so the interrupt and a read's clear
 * cannot both count the same correction.
 */
void test_ecc_irq_records_a_bank_1_address(void)
{
    check_correction(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | 0x01238u, 0x08001238u);
}

/**
 * @brief A correction in bank 2 is counted at its address, up to the last doubleword of the bank.
 * @details BK_ECC selects bank 2, which starts at 0x08080000 and holds the
 * data regions. Its last doubleword is at 0x080FFFF8.
 */
void test_ecc_irq_records_a_bank_2_address(void)
{
    check_correction(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | FLASH_ECCR_BK_ECC | 0x40008u, 0x080C0008u);
    check_correction(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | FLASH_ECCR_BK_ECC | 0x7FFF8u, 0x080FFFF8u);
}

/**
 * @brief A correction in system memory is counted at its address there.
 * @details SYSF_ECC marks system memory (bootloader, OTP), which starts at
 * 0x1FFF0000.
 */
void test_ecc_irq_records_a_system_memory_address(void)
{
    check_correction(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | FLASH_ECCR_SYSF_ECC | 0x00100u, 0x1FFF0100u);
}

/**
 * @brief With a double error flagged too, the handler clears the correction flag only, and leaves ECCD to the NMI.
 * @details Writing back the value read would clear ECCD too, and the NMI
 * would then not see its double error.
 */
void test_ecc_irq_leaves_a_double_error_flag_to_the_nmi(void)
{
    check_correction(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | FLASH_ECCR_ECCD | FLASH_ECCR_BK_ECC | 0x00010u, 0x08080010u);
}

/**
 * @brief An interrupt with no correction flagged counts nothing and writes nothing.
 * @details A read can clear the flag after the correction raised the
 * interrupt and before the handler runs. The NVIC keeps the interrupt
 * pending, so the handler still runs, with nothing left to do.
 */
void test_ecc_irq_without_a_correction_changes_nothing(void)
{
    FLASH->ECCR = FLASH_ECCR_ECCIE | 0x01234u;

    flash_ecc_irq();

    TEST_ASSERT_EQUAL_UINT32(0u, flash_ecc_stats().corrections);
    TEST_ASSERT_EQUAL_HEX32(0u, flash_ecc_stats().last_corrected);
    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCIE | 0x01234u, FLASH->ECCR);
}

/**
 * @brief Called with interrupts already off, the clear leaves them off.
 * @details The clear restores PRIMASK as it found it instead of turning
 * interrupts on, so a caller that holds them off keeps them off.
 */
void test_ecc_irq_keeps_interrupts_off_when_called_with_them_off(void)
{
    primask = 1u;
    FLASH->ECCR = FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | 0x00040u;

    flash_ecc_irq();

    TEST_ASSERT_EQUAL_UINT32(1u, primask);
    TEST_ASSERT_EQUAL_UINT32(1u, flash_ecc_stats().corrections);
    primask = 0u;
}

/* ---- Reads and the NMI ---- */

/**
 * @brief flash_read() counts and clears a correction still pending before it copies, then returns the bytes.
 * @details The interrupt can be held off when the correction happens (a more
 * urgent interrupt runs, or PRIMASK is set). If the flag were still set
 * during the copy, a double error in the bytes read would raise no NMI, and
 * flash_read() would return wrong bytes with HAL_OK. The fake __DSB()
 * records the count at the barrier after the copy, so a clear made only after
 * the copy's barriers fails this test.
 */
void test_flash_read_clears_a_pending_correction_before_the_copy(void)
{
    require_data_area();
    uint8_t data[16];
    uint8_t back[16];
    for (uint32_t i = 0u; i < sizeof(data); i++) {
        data[i] = (uint8_t)(0xA0u + i);
    }
    preload(FLASH_HT_BASE + 0x40u, data, sizeof(data));
    FLASH->ECCR = FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | FLASH_ECCR_BK_ECC | 0x40040u;

    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_read(FLASH_REGION_HT, 0x40u, back, sizeof(back)));

    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, back, sizeof(data));
    TEST_ASSERT_EQUAL_UINT32(1u, flash_ecc_stats().corrections);
    TEST_ASSERT_EQUAL_HEX32(0x080C0040u, flash_ecc_stats().last_corrected);
    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC, FLASH->ECCR);
    TEST_ASSERT_EQUAL_UINT32(1u, corrections_at_dsb);
}

/** @brief Puts 16 known bytes at offset 0x40 of the HT region, for a read to copy. */
static void preload_read_bytes(void)
{
    uint8_t data[16];
    for (uint32_t i = 0u; i < sizeof(data); i++) {
        data[i] = (uint8_t)(0xB0u + i);
    }
    preload(FLASH_HT_BASE + 0x40u, data, sizeof(data));
}

/**
 * @brief A double error whose NMI ran before the check is reported: the count shows it, as ECCD is cleared again.
 * @details The copy's read finds the error and sets ECCD, and its NMI runs at
 * the barrier: it counts the error, records the address and writes 1 to ECCD
 * alone, which clears it. Only the count shows the error then (spec
 * FLS-INT-040).
 */
void test_flash_read_reports_a_double_error_the_nmi_counted_before_the_check(void)
{
    require_data_area();
    preload_read_bytes();
    uint8_t back[16];
    FLASH->ECCR = FLASH_ECCR_ECCIE;
    double_error_at = FLASH_HT_BASE + 0x48u;
    nmi_before_check = true;

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_read(FLASH_REGION_HT, 0x40u, back, sizeof(back)));

    TEST_ASSERT_EQUAL_UINT32(1u, flash_ecc_stats().detections);
    TEST_ASSERT_EQUAL_HEX32(FLASH_HT_BASE + 0x48u, flash_ecc_stats().last_address);
    TEST_ASSERT_BITS_LOW(FLASH_ECCR_ECCD, FLASH->ECCR);
}

/**
 * @brief A double error whose NMI has not run by the check is reported: ECCD shows it, and the driver leaves the flag to the NMI.
 * @details Nothing specifies how soon the NMI arrives after the read that
 * found the error (Arm AN321 §3.6). Until it runs, only ECCD shows the error.
 * The driver must not clear the flag: the NMI would then find no double error,
 * and NMI_Handler would loop until the watchdog resets the MCU. When the NMI
 * runs, it counts the error once.
 */
void test_flash_read_reports_a_double_error_whose_nmi_has_not_run_yet(void)
{
    require_data_area();
    preload_read_bytes();
    uint8_t back[16];
    FLASH->ECCR = FLASH_ECCR_ECCIE;
    double_error_at = FLASH_HT_BASE + 0x48u;

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_read(FLASH_REGION_HT, 0x40u, back, sizeof(back)));

    TEST_ASSERT_EQUAL_UINT32(0u, flash_ecc_stats().detections);
    TEST_ASSERT_BITS_HIGH(FLASH_ECCR_ECCD, FLASH->ECCR);
    run_nmi();
    TEST_ASSERT_EQUAL_UINT32(1u, flash_ecc_stats().detections);
    TEST_ASSERT_EQUAL_HEX32(FLASH_HT_BASE + 0x48u, flash_ecc_stats().last_address);
}

/**
 * @brief A double error counted before a read does not fail it: the read compares the count with its value before the copy.
 * @details Otherwise one double error would fail every later read.
 */
void test_flash_read_after_an_earlier_double_error_succeeds(void)
{
    require_data_area();
    preload_read_bytes();
    uint8_t back[16];
    FLASH->ECCR = FLASH_ECCR_ECCIE | FLASH_ECCR_ECCD | FLASH_ECCR_BK_ECC | 0x40100u;
    run_nmi();

    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_read(FLASH_REGION_HT, 0x40u, back, sizeof(back)));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(bytes_at(FLASH_HT_BASE + 0x40u), back, sizeof(back));
    TEST_ASSERT_EQUAL_UINT32(1u, flash_ecc_stats().detections);
    TEST_ASSERT_EQUAL_HEX32(0x080C0100u, flash_ecc_stats().last_address);
}

/**
 * @brief An NMI without a double error flagged is not the driver's: flash_ecc_nmi() returns false, counts nothing and writes nothing.
 * @details NMI_Handler calls flash_ecc_nmi() first. When it returns false the
 * NMI has another cause, and NMI_Handler loops until the watchdog resets the
 * MCU. A pending correction is the interrupt's to clear.
 */
void test_ecc_nmi_ignores_an_nmi_without_a_double_error(void)
{
    FLASH->ECCR = FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | 0x00100u;

    TEST_ASSERT_FALSE(flash_ecc_nmi());

    TEST_ASSERT_EQUAL_UINT32(0u, flash_ecc_stats().detections);
    TEST_ASSERT_EQUAL_UINT32(0u, flash_ecc_stats().corrections);
    TEST_ASSERT_EQUAL_HEX32(FLASH_ECCR_ECCIE | FLASH_ECCR_ECCC | 0x00100u, FLASH->ECCR);
}

/* ---- Program ---- */

/**
 * @brief Each doubleword is programmed as RM0351 §3.3.7 and FLASH_Program_DoubleWord() do it, and then FLASH_CR and the caches are left as HAL_FLASH_Program() and HAL_FLASH_Lock() leave them.
 * @details PG is set, the first word written, then the HAL's barrier, then
 * the second word, which starts the programming. The fake __ISB() records the
 * two doublewords at each barrier: the first word written, the second not
 * yet. Each doubleword takes 1 ms here, and the wait polls without sleeping,
 * since on the chip a doubleword takes under 0.1 ms. After the call PG is
 * clear, FLASH_CR locked, and the data cache has been reset once and is on;
 * the instruction cache is left alone, as HAL_FLASH_Program() leaves it.
 */
void test_program_writes_each_doubleword_first_word_first(void)
{
    require_data_area();
    uint8_t data[16];
    for (uint32_t i = 0u; i < sizeof(data); i++) {
        data[i] = (uint8_t)(0xA0u + i);
    }
    isb_watch = bytes_at(FLASH_HT_BASE + 0x40u);
    op_ms = 1u;
    TickType_t t0 = xTaskGetTickCount();
    uint32_t w0 = waits;

    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_program(FLASH_REGION_HT, 0x40u, data, sizeof(data)));

    uint8_t expected[16];
    memset(expected, 0xFF, sizeof(expected));
    memcpy(expected, data, 4u);
    TEST_ASSERT_EQUAL_UINT32(2u, isb_calls);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, isb_bytes[0], sizeof(expected));
    memcpy(expected, data, 12u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, isb_bytes[1], sizeof(expected));
    TEST_ASSERT_BITS_HIGH(FLASH_CR_PG, isb_cr[0]);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_PG, isb_cr[1]);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, bytes_at(FLASH_HT_BASE + 0x40u), sizeof(data));
    TEST_ASSERT_EQUAL_UINT32(2u, programs);
    TEST_ASSERT_EQUAL_UINT32(0u, slept_since(t0, w0));
    TEST_ASSERT_BITS_LOW(FLASH_CR_PG, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_EQUAL_UINT32(1u, dcache_resets);
    TEST_ASSERT_EQUAL_UINT32(0u, icache_resets);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
}

/**
 * @brief A program that is not made of whole doublewords at doubleword addresses is refused before anything is touched; an empty one succeeds without touching anything.
 * @details Only doublewords can be programmed, at doubleword addresses
 * (RM0351 §3.3.7); the chip would set PGAERR or SIZERR. The driver refuses
 * them first, and a length that is not a multiple of 8 would also make it
 * read past the caller's buffer. The ring module programs empty tails, so an
 * empty program is legal.
 */
void test_program_refuses_what_is_not_whole_doublewords(void)
{
    require_data_area();
    const uint8_t data[16] = { 0u };

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_program(FLASH_REGION_HT, 0x44u, data, 8u));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_program(FLASH_REGION_HT, 0x40u, data, 12u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_program(FLASH_REGION_HT, 0x40u, data, 0u));

    TEST_ASSERT_EQUAL_INT(0, key_writes);
    TEST_ASSERT_EQUAL_UINT32(0u, waits);
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE), 0xFFu, 0x100u));
}

/**
 * @brief A target that is not erased is refused before anything is programmed, even with all-zero data, which the chip would accept (spec FLS-DRV-120).
 * @details Of three doublewords, the second holds data. The chip checks each
 * doubleword only as it programs it, and programs all-zero data over
 * anything (PROGERR, RM0351 §3.3.7): left to the chip, all three would be
 * programmed here, and the stored data zeroed. The driver reads the target
 * first and refuses: nothing is written, and FLASH_CR is never unlocked.
 */
void test_program_over_data_is_refused_before_anything_is_written(void)
{
    require_data_area();
    const uint8_t used[8] = { 0x12u, 0x34u, 0x56u, 0x78u, 0x9Au, 0xBCu, 0xDEu, 0xF0u };
    const uint8_t zeros[24] = { 0u };
    preload(FLASH_HT_BASE + 0x48u, used, sizeof(used));

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_program(FLASH_REGION_HT, 0x40u, zeros, sizeof(zeros)));

    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE + 0x40u), 0xFFu, 8u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(used, bytes_at(FLASH_HT_BASE + 0x48u), 8u);
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE + 0x50u), 0xFFu, 8u));
    TEST_ASSERT_EQUAL_UINT32(0u, doublewords_written);
    TEST_ASSERT_EQUAL_INT(0, key_writes);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
}

/**
 * @brief An error the chip reports while programming stops the program at that doubleword with HAL_ERROR: the ones before it stay programmed, and the flag is cleared.
 * @details WRPERR (the page is write-protected, RM0351 §3.7.5) stands for any
 * error the chip reports, which the driver's check of the target cannot
 * foresee. Of three doublewords, the chip refuses the second: the first is
 * programmed, and the third is not attempted. Then FLASH_CR and the caches
 * are left as after a program that succeeds.
 */
void test_program_stops_at_a_doubleword_the_chip_refuses(void)
{
    require_data_area();
    uint8_t data[24];
    memset(data, 0x5Au, sizeof(data));
    wrperr_at = 2u;

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_program(FLASH_REGION_HT, 0x40u, data, sizeof(data)));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, bytes_at(FLASH_HT_BASE + 0x40u), 8u);
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE + 0x48u), 0xFFu, 16u));
    TEST_ASSERT_EQUAL_UINT32(2u, doublewords_written);
    TEST_ASSERT_EQUAL_UINT32(1u, programs);
    TEST_ASSERT_BITS_LOW(FLASH_FLAG_SR_ERRORS, FLASH->SR);
    TEST_ASSERT_BITS_LOW(FLASH_CR_PG, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
}

/**
 * @brief A program that stays busy returns HAL_TIMEOUT at its limit without writing FLASH_CR; once the flash is free, the next operation clears what it left and succeeds (spec V13).
 * @details While BSY is set, a write to FLASH_CR stalls the bus until the
 * operation ends (RM0351 §3.3.5); the fake fails the test on such a write.
 * So the timed-out call leaves PG set, FLASH_CR unlocked and the data cache
 * off. The next operation is an erase, which would fail with PGSERR if PG
 * were still set (§3.3.7); it clears PG first, and turns the data cache back
 * on at its end.
 */
void test_program_that_stays_busy_returns_at_its_limit_without_writing_flash_cr(void)
{
    require_data_area();
    const uint8_t data[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    op_ms = OP_STUCK;
    TickType_t t0 = xTaskGetTickCount();

    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, flash_program(FLASH_REGION_HT, 0x40u, data, sizeof(data)));

    uint32_t took = (uint32_t)(xTaskGetTickCount() - t0);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(FLASH_PROGRAM_LIMIT_MS, took);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(FLASH_PROGRAM_LIMIT_MS + 3u, took);
    TEST_ASSERT_BITS_HIGH(FLASH_SR_BSY, FLASH->SR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_PG, FLASH->CR);
    TEST_ASSERT_BITS_LOW(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_BITS_LOW(FLASH_ACR_DCEN, FLASH->ACR);

    unstick();
    op_ms = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_erase_page(FLASH_REGION_HT, 0u));

    TEST_ASSERT_EQUAL_UINT32(1u, erases);
    TEST_ASSERT_BITS_LOW(FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_PNB, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
    TEST_ASSERT_EQUAL_INT(2, key_writes);
}

/* ---- Erase ---- */

/**
 * @brief An erase selects its page in bank 2 as FLASH_PageErase() does, erases that page only, sleeps while it runs, and leaves FLASH_CR and the caches as HAL_FLASHEx_Erase() and HAL_FLASH_Lock() leave them.
 * @details 0x080C0800 is page 129 of bank 2 (RM0351 §3.3.1): BKER = 1 and
 * PNB = 129 when STRT is set (§3.7.6). Any offset inside the page selects it.
 * The erase takes 10 ms here, and the task sleeps between checks (FLS-DRV-100).
 * After the call PER and PNB are clear, FLASH_CR is locked, and both caches
 * have been reset once and are on.
 */
void test_erase_selects_its_page_in_bank_2_and_erases_only_it(void)
{
    require_data_area();
    preload_page(FLASH_HT_BASE, 0x11u);
    preload_page(FLASH_HT_BASE + 0x800u, 0x22u);
    preload_page(FLASH_HT_BASE + 0x1000u, 0x33u);
    op_ms = 10u;
    TickType_t t0 = xTaskGetTickCount();
    uint32_t w0 = waits;

    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_erase_page(FLASH_REGION_HT, 0x800u + 0x123u));

    TEST_ASSERT_EQUAL_UINT32(1u, erases);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_BKER | FLASH_CR_PER, cr_at_erase);
    TEST_ASSERT_EQUAL_UINT32((FLASH_HT_BASE + 0x800u - RM_BANK2_START) / RM_PAGE_SIZE, (cr_at_erase & FLASH_CR_PNB) >> FLASH_CR_PNB_Pos);
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE), 0x11u, RM_PAGE_SIZE));
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE + 0x800u), 0xFFu, RM_PAGE_SIZE));
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE + 0x1000u), 0x33u, RM_PAGE_SIZE));
    TEST_ASSERT_GREATER_THAN_UINT32(0u, slept_since(t0, w0));
    TEST_ASSERT_BITS_LOW(FLASH_CR_PER | FLASH_CR_PNB | FLASH_CR_STRT, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_EQUAL_UINT32(1u, icache_resets);
    TEST_ASSERT_EQUAL_UINT32(1u, dcache_resets);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
}

/**
 * @brief With the banks swapped, an erase selects its page in bank 1, as get_bank() does, and erases the page addressed.
 * @details FB_MODE = 1 maps bank 1 at 0x08080000 (RM0351 §9.2.1), so
 * 0x080C0800 is page 129 of bank 1: BKER = 0 and PNB = 129. BKER is set
 * before the call, as an erase in bank 2 leaves it (HAL_FLASHEx_Erase()
 * clears only PER and PNB), so the driver must clear it. The spec never
 * swaps the banks (FLS-GEN-100); this checks the driver would still erase
 * the right page if they were.
 */
void test_erase_with_the_banks_swapped_selects_bank_1(void)
{
    require_data_area();
    preload_page(FLASH_HT_BASE, 0x11u);
    preload_page(FLASH_HT_BASE + 0x800u, 0x22u);
    preload_page(FLASH_HT_BASE + 0x1000u, 0x33u);
    fake_syscfg.MEMRMP = SYSCFG_MEMRMP_FB_MODE;
    FLASH->CR |= FLASH_CR_BKER;

    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_erase_page(FLASH_REGION_HT, 0x800u + 0x123u));

    TEST_ASSERT_EQUAL_UINT32(1u, erases);
    TEST_ASSERT_BITS_LOW(FLASH_CR_BKER, cr_at_erase);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_PER, cr_at_erase);
    TEST_ASSERT_EQUAL_UINT32((FLASH_HT_BASE + 0x800u - RM_BANK2_START) / RM_PAGE_SIZE, (cr_at_erase & FLASH_CR_PNB) >> FLASH_CR_PNB_Pos);
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE), 0x11u, RM_PAGE_SIZE));
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE + 0x800u), 0xFFu, RM_PAGE_SIZE));
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE + 0x1000u), 0x33u, RM_PAGE_SIZE));
}

/**
 * @brief An erase that stays busy returns HAL_TIMEOUT at its limit without writing FLASH_CR; once the flash is free, the next operation clears what it left and succeeds (spec V13).
 * @details As for the program: the timed-out call leaves PER set, FLASH_CR
 * unlocked and the data cache off. The next operation is a program, which
 * would fail with PGSERR if PER were still set (RM0351 §3.3.7).
 */
void test_erase_that_stays_busy_returns_at_its_limit_without_writing_flash_cr(void)
{
    require_data_area();
    const uint8_t data[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    op_ms = OP_STUCK;
    TickType_t t0 = xTaskGetTickCount();

    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, flash_erase_page(FLASH_REGION_HT, 0u));

    uint32_t took = (uint32_t)(xTaskGetTickCount() - t0);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(FLASH_ERASE_LIMIT_MS, took);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(FLASH_ERASE_LIMIT_MS + 4u, took);
    TEST_ASSERT_BITS_HIGH(FLASH_SR_BSY, FLASH->SR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_PER, FLASH->CR);
    TEST_ASSERT_BITS_LOW(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_BITS_LOW(FLASH_ACR_DCEN, FLASH->ACR);

    unstick();
    op_ms = 0u;
    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_program(FLASH_REGION_HT, 0x40u, data, sizeof(data)));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, bytes_at(FLASH_HT_BASE + 0x40u), sizeof(data));
    TEST_ASSERT_BITS_LOW(FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_PNB, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
}

/**
 * @brief A doubleword program or an erase that ends just as its limit passes succeeds: BSY is read once more after the limit.
 * @details The doubleword lasts exactly FLASH_PROGRAM_LIMIT_MS and the erase
 * exactly FLASH_ERASE_LIMIT_MS. The check that finds the limit passed is
 * also the one at which the operation has ended. On the chip the same
 * happens when a higher-priority task keeps the waiting task from running
 * until after the limit.
 */
void test_an_operation_that_lasts_exactly_its_limit_succeeds(void)
{
    require_data_area();
    const uint8_t data[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };

    op_ms = FLASH_PROGRAM_LIMIT_MS;
    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_program(FLASH_REGION_HT, 0x40u, data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT32(1u, programs);

    op_ms = FLASH_ERASE_LIMIT_MS;
    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_erase_page(FLASH_REGION_HT, 0u));
    TEST_ASSERT_EQUAL_UINT32(1u, erases);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
}

/**
 * @brief An error the chip reports at the end of an erase is returned as HAL_ERROR and cleared, and FLASH_CR and the caches are left as after an erase that succeeds.
 * @details WRPERR stands for any error flag (RM0351 §3.7.5); the erase is
 * aborted, so the page keeps its content. FLASH_WaitForLastOperation() reads
 * and clears the flags the same way.
 */
void test_erase_reports_an_error_and_clears_it(void)
{
    require_data_area();
    preload_page(FLASH_HT_BASE, 0x44u);
    erase_error = FLASH_SR_WRPERR;

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_erase_page(FLASH_REGION_HT, 0u));

    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE), 0x44u, RM_PAGE_SIZE));
    TEST_ASSERT_BITS_LOW(FLASH_FLAG_SR_ERRORS, FLASH->SR);
    TEST_ASSERT_BITS_LOW(FLASH_CR_PER | FLASH_CR_PNB, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
}

/**
 * @brief Error flags an earlier operation left are cleared before the next program or erase starts, which then succeeds.
 * @details RM0351 §3.3.6 and §3.3.7, step 2: a flag left set makes the next
 * operation fail with PGSERR. An operation that timed out leaves its flags
 * unread. The fake sets PGSERR if a flag is still set when the erase starts
 * or the doubleword is written.
 */
void test_error_flags_left_by_an_earlier_operation_are_cleared_first(void)
{
    require_data_area();
    const uint8_t data[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };

    FLASH->SR = FLASH_SR_PROGERR | FLASH_SR_PGSERR;
    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_erase_page(FLASH_REGION_HT, 0u));
    TEST_ASSERT_EQUAL_UINT32(1u, erases);

    FLASH->SR = FLASH_SR_SIZERR;
    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_program(FLASH_REGION_HT, 0x40u, data, sizeof(data)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, bytes_at(FLASH_HT_BASE + 0x40u), sizeof(data));
}

/**
 * @brief An erase that cannot unlock FLASH_CR returns HAL_ERROR, starts nothing, and leaves the correction interrupt on.
 * @details After a wrong key sequence, FLASH_CR stays locked until reset
 * (RM0351 §3.3.5). The driver must report it instead of setting the erase
 * bits.
 */
void test_erase_fails_when_flash_cr_stays_locked(void)
{
    flash_ecc_init();
    keys_refused = true;

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_erase_page(FLASH_REGION_HT, 0u));

    TEST_ASSERT_BITS_LOW(FLASH_CR_PER | FLASH_CR_STRT, FLASH->CR);
    TEST_ASSERT_EQUAL_UINT32(0u, erases);
    TEST_ASSERT_TRUE(nvic_enabled);
    TEST_ASSERT_BITS_HIGH(FLASH_ECCR_ECCIE, FLASH->ECCR);
}

/* ---- The correction interrupt during program and erase ---- */

/**
 * @brief A page erase leaves the correction interrupt on while it waits.
 * @details A correction during an erase (up to 24.47 ms) is cleared at once,
 * as at any other time. Whether the handler's access to FLASH_ECCR waits
 * while the flash is busy is measured on the board (spec V15); if it waits,
 * this behaviour is to be revisited, and this test with it.
 */
void test_erase_leaves_the_ecc_interrupt_on(void)
{
    flash_ecc_init();
    op_ms = 10u;

    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_erase_page(FLASH_REGION_HT, 0x800u));

    TEST_ASSERT_GREATER_THAN_UINT32(1u, waits);
    TEST_ASSERT_EQUAL_UINT32(0u, waits_with_ecc_irq_off);
    TEST_ASSERT_TRUE(nvic_enabled);
    TEST_ASSERT_BITS_HIGH(FLASH_ECCR_ECCIE, FLASH->ECCR);
}

/**
 * @brief Programming doublewords leaves the correction interrupt on while it waits.
 * @details As for the erase (spec V15).
 */
void test_program_leaves_the_ecc_interrupt_on(void)
{
    require_data_area();
    uint8_t data[16];
    memset(data, 0xA5u, sizeof(data));
    flash_ecc_init();
    op_ms = 1u;

    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_program(FLASH_REGION_HT, 0x40u, data, sizeof(data)));

    TEST_ASSERT_GREATER_THAN_UINT32(1u, waits);
    TEST_ASSERT_EQUAL_UINT32(0u, waits_with_ecc_irq_off);
    TEST_ASSERT_TRUE(nvic_enabled);
    TEST_ASSERT_BITS_HIGH(FLASH_ECCR_ECCIE, FLASH->ECCR);
}

/* ---- Interface reset ---- */

/**
 * @brief After a task deleted in the middle of an erase, the reset waits for the erase, clears the erase bits, locks FLASH_CR and flushes both caches, which are on again.
 * @details The health monitor deletes and recreates a task that misses its
 * period, and the OBDH task calls flash_interface_reset() each time it starts
 * (FLS-REQ-070). The erase it finds runs for 20 ms more; the reset sleeps
 * while it waits, and writes FLASH_CR only once BSY is clear. FLASH_CR was
 * left unlocked, so no key is written.
 */
void test_interface_reset_after_a_task_deleted_during_an_erase(void)
{
    require_data_area();
    preload_page(FLASH_HT_BASE + 0x800u, 0x55u);
    leave_an_erase_running(FLASH_HT_BASE + 0x800u, 20u);
    TickType_t t0 = xTaskGetTickCount();
    uint32_t w0 = waits;

    flash_interface_reset();

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(20u, (uint32_t)(xTaskGetTickCount() - t0));
    TEST_ASSERT_GREATER_THAN_UINT32(0u, slept_since(t0, w0));
    TEST_ASSERT_TRUE(all_bytes(bytes_at(FLASH_HT_BASE + 0x800u), 0xFFu, RM_PAGE_SIZE));
    TEST_ASSERT_BITS_LOW(FLASH_SR_BSY, FLASH->SR);
    TEST_ASSERT_BITS_LOW(FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_PNB | FLASH_CR_STRT, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_EQUAL_UINT32(1u, icache_resets);
    TEST_ASSERT_EQUAL_UINT32(1u, dcache_resets);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
    TEST_ASSERT_EQUAL_INT(0, key_writes);
}

/**
 * @brief After a task deleted in the middle of a program the chip refused, the reset clears the error flag and PG, and locks FLASH_CR.
 * @details The task can be preempted and deleted anywhere, here between the
 * refused doubleword and the check of its result. Nobody reads that result:
 * the request it served gets no answer. PROGERR stands for any error flag
 * (RM0351 §3.7.5); the chip starts nothing, so BSY stays clear.
 */
void test_interface_reset_after_a_task_deleted_during_a_refused_program(void)
{
    FLASH->CR = (FLASH->CR & ~FLASH_CR_LOCK) | FLASH_CR_PG;
    FLASH->SR |= FLASH_SR_PROGERR;
    FLASH->ACR &= ~FLASH_ACR_DCEN;

    flash_interface_reset();

    TEST_ASSERT_BITS_LOW(FLASH_FLAG_SR_ERRORS, FLASH->SR);
    TEST_ASSERT_BITS_LOW(FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_PNB, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
    TEST_ASSERT_EQUAL_INT(0, key_writes);
}

/**
 * @brief After a task deleted between operations, the reset writes no key and leaves FLASH_CR locked with no operation bit, and flushes both caches, turning each off before its reset.
 * @details The usual case: the OBDH task restarted while the flash was idle,
 * so FLASH_CR is locked and both caches are on. A locked FLASH_CR has no
 * operation bit set, so the reset does not unlock it only to lock it again.
 * Each reset bit can be written only while its cache is off (RM0351 §3.7.1),
 * which the fake checks.
 */
void test_interface_reset_when_idle_flushes_both_caches(void)
{
    flash_interface_reset();

    TEST_ASSERT_EQUAL_INT(0, key_writes);
    TEST_ASSERT_BITS_LOW(FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_PNB | FLASH_CR_STRT, FLASH->CR);
    TEST_ASSERT_BITS_HIGH(FLASH_CR_LOCK, FLASH->CR);
    TEST_ASSERT_EQUAL_UINT32(1u, icache_resets);
    TEST_ASSERT_EQUAL_UINT32(1u, dcache_resets);
    TEST_ASSERT_BITS_HIGH(FLASH_ACR_ICEN | FLASH_ACR_DCEN, FLASH->ACR);
}

/**
 * @brief With the flash stuck busy, the reset returns at the erase limit and changes nothing: no FLASH_CR write, no cache change.
 * @details The fake fails the test on a FLASH_CR write while BSY is set
 * (RM0351 §3.3.5). The data cache stays off, as an operation may still run.
 * The next program or erase waits again in its turn.
 */
void test_interface_reset_with_the_flash_stuck_busy_changes_nothing(void)
{
    leave_an_erase_running(FLASH_HT_BASE, OP_STUCK);
    uint32_t cr = FLASH->CR;
    TickType_t t0 = xTaskGetTickCount();

    flash_interface_reset();

    uint32_t took = (uint32_t)(xTaskGetTickCount() - t0);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(FLASH_ERASE_LIMIT_MS, took);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(FLASH_ERASE_LIMIT_MS + 3u, took);
    TEST_ASSERT_EQUAL_HEX32(cr, FLASH->CR);
    TEST_ASSERT_BITS_LOW(FLASH_ACR_DCEN, FLASH->ACR);
    TEST_ASSERT_EQUAL_UINT32(0u, icache_resets + dcache_resets);
}

/* ---- Region guard ---- */

/**
 * @brief Unit data, written on the ground, can be read but not programmed or erased.
 * @details Unit data is programmed once on the ground with the ST-Link, then
 * write-protected (flash_map.h). A refused program or erase does not even
 * unlock FLASH_CR.
 */
void test_unit_data_is_read_only(void)
{
    require_data_area();
    const uint8_t data[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint8_t back[8];
    preload(FLASH_UNIT_BASE, data, sizeof(data));

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_program(FLASH_REGION_UNIT, 0x100u, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_erase_page(FLASH_REGION_UNIT, 0u));
    TEST_ASSERT_EQUAL_INT(HAL_OK, flash_read(FLASH_REGION_UNIT, 0u, back, sizeof(back)));

    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, back, sizeof(data));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFFu, bytes_at(FLASH_UNIT_BASE + 0x100u), sizeof(data));
    TEST_ASSERT_EQUAL_INT(0, key_writes);
}

/**
 * @brief A range that does not lie wholly inside its region is refused before any flash is touched.
 * @details Each call names a region and an offset, and the driver refuses
 * anything outside that region (FLS-DRV-010), so a wrong offset cannot reach
 * another store's data or the program image.
 */
void test_a_range_outside_its_region_is_refused(void)
{
    uint8_t buf[16] = { 0u };

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_read(FLASH_REGION_HT, FLASH_HT_SIZE - 8u, buf, 16u));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_read(FLASH_REGION_HT, 8u, buf, UINT32_MAX));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_program(FLASH_REGION_PARAM_A, FLASH_PARAM_SIZE, buf, 8u));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_erase_page(FLASH_REGION_OBC_LOG, FLASH_OBC_LOG_SIZE));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, flash_read(FLASH_REGION_COUNT, 0u, buf, 8u));
    TEST_ASSERT_EQUAL_UINT32(0u, flash_region_size(FLASH_REGION_COUNT));

    TEST_ASSERT_EQUAL_INT(0, key_writes);
    TEST_ASSERT_EQUAL_UINT32(0u, flash_ecc_stats().corrections);
}

/** @brief Runs the suite (see support/pocat_test_main.h for the options). */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    POCAT_RUN_TEST(test_ecc_init_enables_the_interrupt_above_the_kernel_mask);
    POCAT_RUN_TEST(test_ecc_init_counts_a_correction_left_since_reset);
    POCAT_RUN_TEST(test_ecc_init_leaves_a_double_error_to_the_nmi);
    POCAT_RUN_TEST(test_ecc_irq_records_a_bank_1_address);
    POCAT_RUN_TEST(test_ecc_irq_records_a_bank_2_address);
    POCAT_RUN_TEST(test_ecc_irq_records_a_system_memory_address);
    POCAT_RUN_TEST(test_ecc_irq_leaves_a_double_error_flag_to_the_nmi);
    POCAT_RUN_TEST(test_ecc_irq_without_a_correction_changes_nothing);
    POCAT_RUN_TEST(test_ecc_irq_keeps_interrupts_off_when_called_with_them_off);
    POCAT_RUN_TEST(test_flash_read_clears_a_pending_correction_before_the_copy);
    POCAT_RUN_TEST(test_flash_read_reports_a_double_error_the_nmi_counted_before_the_check);
    POCAT_RUN_TEST(test_flash_read_reports_a_double_error_whose_nmi_has_not_run_yet);
    POCAT_RUN_TEST(test_flash_read_after_an_earlier_double_error_succeeds);
    POCAT_RUN_TEST(test_ecc_nmi_ignores_an_nmi_without_a_double_error);
    POCAT_RUN_TEST(test_program_writes_each_doubleword_first_word_first);
    POCAT_RUN_TEST(test_program_refuses_what_is_not_whole_doublewords);
    POCAT_RUN_TEST(test_program_over_data_is_refused_before_anything_is_written);
    POCAT_RUN_TEST(test_program_stops_at_a_doubleword_the_chip_refuses);
    POCAT_RUN_TEST(test_program_that_stays_busy_returns_at_its_limit_without_writing_flash_cr);
    POCAT_RUN_TEST(test_erase_selects_its_page_in_bank_2_and_erases_only_it);
    POCAT_RUN_TEST(test_erase_with_the_banks_swapped_selects_bank_1);
    POCAT_RUN_TEST(test_erase_that_stays_busy_returns_at_its_limit_without_writing_flash_cr);
    POCAT_RUN_TEST(test_an_operation_that_lasts_exactly_its_limit_succeeds);
    POCAT_RUN_TEST(test_erase_reports_an_error_and_clears_it);
    POCAT_RUN_TEST(test_error_flags_left_by_an_earlier_operation_are_cleared_first);
    POCAT_RUN_TEST(test_erase_fails_when_flash_cr_stays_locked);
    POCAT_RUN_TEST(test_erase_leaves_the_ecc_interrupt_on);
    POCAT_RUN_TEST(test_program_leaves_the_ecc_interrupt_on);
    POCAT_RUN_TEST(test_interface_reset_after_a_task_deleted_during_an_erase);
    POCAT_RUN_TEST(test_interface_reset_after_a_task_deleted_during_a_refused_program);
    POCAT_RUN_TEST(test_interface_reset_when_idle_flushes_both_caches);
    POCAT_RUN_TEST(test_interface_reset_with_the_flash_stuck_busy_changes_nothing);
    POCAT_RUN_TEST(test_unit_data_is_read_only);
    POCAT_RUN_TEST(test_a_range_outside_its_region_is_refused);
    return POCAT_END();
}
