/**
 * @file stm32l4xx_hal.h
 * @brief HAL stub for test_flash_driver: the flash registers as memory that the suite's fake flash controller watches, and the HAL, core and NVIC calls the flash driver makes.
 * @date 2026-11-24
 *
 * @details
 * test_flash_driver lists this directory first (`INCLUDES` in
 * test/CMakeLists.txt), so the real flash.c, which the suite `#include`s,
 * gets this header instead of stubs_hal/stm32l4xx_hal.h. The shared stub is
 * included first, for the types every suite uses.
 *
 * - **Registers.** `FLASH` points to `fake_flash`, which the suite defines.
 *   Every write through `WRITE_REG()`, `SET_BIT()`, `CLEAR_BIT()` or
 *   `MODIFY_REG()` calls the suite's fake_flash_write_reg(), which acts as
 *   the chip does (the flags that clear on a 1, the key sequence, an erase
 *   started by STRT, the cache resets) and fails the test where the chip
 *   would misbehave (FLASH_CR written while busy or locked, a wrong key
 *   sequence, a cache reset while the cache is on). Reads are plain reads.
 *   The driver writes FLASH_ECCR without these macros, so FLASH_ECCR keeps
 *   the value written, and the suite checks that value. `SYSCFG` points to
 *   `fake_syscfg`: the suite sets FB_MODE in SYSCFG_MEMRMP to swap the banks.
 * - **Register bits** come from the real stm32l476xx.h: test/CMakeLists.txt
 *   copies their `#define` lines into generated/flash_reg_bits.h. The other
 *   constants and macros are as in stm32l476xx.h, stm32l4xx.h,
 *   stm32l4xx_hal_flash.h and include/core/stm32l4xx_hal_conf.h.
 * - **HAL flash functions** the driver calls (HAL_FLASH_Unlock(),
 *   HAL_FLASH_Lock(), FLASH_PageErase()) are only declared here. The suite
 *   defines them as copies of ST's code, so their register writes reach the
 *   fake flash controller as they reach the chip.
 * - **Core and NVIC calls** (PRIMASK, barriers, system reset, NVIC priority
 *   and enable, HAL_GetTick()) are only declared here; the suite fakes them.
 */
#ifndef STUBS_HAL_FLASH_STM32L4XX_HAL_H
#define STUBS_HAL_FLASH_STM32L4XX_HAL_H

#include <stdint.h>
#include "../stubs_hal/stm32l4xx_hal.h"   /* HAL_StatusTypeDef, FLASH_PAGE_SIZE, the peripheral handles */

#define __IO volatile

/** @brief The one interrupt the flash driver uses; FLASH_IRQn is 4 in stm32l476xx.h. */
typedef enum {
    FLASH_IRQn = 4
} IRQn_Type;

/** @brief The FLASH registers up to ECCR, at the offsets of the real FLASH_TypeDef. */
typedef struct {
    __IO uint32_t ACR;       /**< Access control, offset 0x00. */
    __IO uint32_t PDKEYR;    /**< Power-down key, offset 0x04. */
    __IO uint32_t KEYR;      /**< Key, offset 0x08. */
    __IO uint32_t OPTKEYR;   /**< Option key, offset 0x0C. */
    __IO uint32_t SR;        /**< Status, offset 0x10. */
    __IO uint32_t CR;        /**< Control, offset 0x14. */
    __IO uint32_t ECCR;      /**< ECC, offset 0x18. */
} FLASH_TypeDef;

extern FLASH_TypeDef fake_flash;   /**< The FLASH registers; defined by the suite. */

#define FLASH  (&fake_flash)

/** @brief The SYSCFG register the flash driver reads, at its offset in the real SYSCFG_TypeDef. */
typedef struct {
    __IO uint32_t MEMRMP;    /**< Memory remap, offset 0x00: FB_MODE swaps the banks. */
} SYSCFG_TypeDef;

extern SYSCFG_TypeDef fake_syscfg;   /**< Defined by the suite. */

#define SYSCFG  (&fake_syscfg)

#include "flash_reg_bits.h"   /* generated from stm32l476xx.h by test/CMakeLists.txt */

/* As in stm32l476xx.h and stm32l4xx_hal_flash.h, for the 1 MB STM32L476RG
   (the HAL reads the flash size from the chip; FLASH_PAGE_SIZE comes from the shared stub) */
#define FLASH_BASE        0x08000000UL
#define FLASH_BANK_SIZE   0x00080000UL
#define FLASH_BANK_1      ((uint32_t)0x01)
#define FLASH_BANK_2      ((uint32_t)0x02)
#define FLASH_KEY1        0x45670123U
#define FLASH_KEY2        0xCDEF89ABU
#define FLASH_FLAG_SR_ERRORS  (FLASH_SR_OPERR  | FLASH_SR_PROGERR | FLASH_SR_WRPERR  | FLASH_SR_PGAERR | \
                               FLASH_SR_SIZERR | FLASH_SR_PGSERR  | FLASH_SR_MISERR  | FLASH_SR_FASTERR | \
                               FLASH_SR_RDERR  | FLASH_SR_OPTVERR)

/* As in include/core/stm32l4xx_hal_conf.h */
#define INSTRUCTION_CACHE_ENABLE  1U
#define DATA_CACHE_ENABLE         1U

/* As in stm32l4xx.h, except that every write goes through fake_flash_write_reg() */
#define READ_BIT(REG, BIT)   ((REG) & (BIT))
#define READ_REG(REG)        ((REG))
#define WRITE_REG(REG, VAL)  fake_flash_write_reg(&(REG), (VAL))
#define SET_BIT(REG, BIT)    WRITE_REG((REG), READ_REG(REG) | (BIT))
#define CLEAR_BIT(REG, BIT)  WRITE_REG((REG), READ_REG(REG) & ~(BIT))
#define MODIFY_REG(REG, CLEARMASK, SETMASK)  WRITE_REG((REG), (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))

/* As in stm32l4xx_hal_flash.h */
#define FLASH_FLAG_BSY          FLASH_SR_BSY
#define FLASH_FLAG_ECCC         FLASH_ECCR_ECCC
#define FLASH_FLAG_ECCD         FLASH_ECCR_ECCD
#define FLASH_FLAG_ECCR_ERRORS  (FLASH_FLAG_ECCD | FLASH_FLAG_ECCC)
#define __HAL_FLASH_GET_FLAG(__FLAG__)           ((((__FLAG__) & FLASH_FLAG_ECCR_ERRORS) != 0U) ? \
                                                  (READ_BIT(FLASH->ECCR, (__FLAG__)) != 0U) : \
                                                  (READ_BIT(FLASH->SR,   (__FLAG__)) != 0U))
#define __HAL_FLASH_CLEAR_FLAG(__FLAG__)         do { if(((__FLAG__) & FLASH_FLAG_ECCR_ERRORS) != 0U) { SET_BIT(FLASH->ECCR, ((__FLAG__) & FLASH_FLAG_ECCR_ERRORS)); }\
                                                      if(((__FLAG__) & ~(FLASH_FLAG_ECCR_ERRORS)) != 0U) { WRITE_REG(FLASH->SR, ((__FLAG__) & ~(FLASH_FLAG_ECCR_ERRORS))); }\
                                                    } while(0)
#define __HAL_FLASH_INSTRUCTION_CACHE_ENABLE()   SET_BIT(FLASH->ACR, FLASH_ACR_ICEN)
#define __HAL_FLASH_INSTRUCTION_CACHE_DISABLE()  CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICEN)
#define __HAL_FLASH_DATA_CACHE_ENABLE()          SET_BIT(FLASH->ACR, FLASH_ACR_DCEN)
#define __HAL_FLASH_DATA_CACHE_DISABLE()         CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCEN)
#define __HAL_FLASH_INSTRUCTION_CACHE_RESET()    do { SET_BIT(FLASH->ACR, FLASH_ACR_ICRST); \
                                                      CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICRST); } while (0)
#define __HAL_FLASH_DATA_CACHE_RESET()           do { SET_BIT(FLASH->ACR, FLASH_ACR_DCRST); \
                                                      CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCRST); } while (0)

/**
 * @brief Writes a flash register as the chip does; every register macro above calls it.
 * @details See the suite's fake flash controller for what each register does.
 */
void fake_flash_write_reg(volatile uint32_t *reg, uint32_t value);

/* HAL flash functions, as declared in stm32l4xx_hal_flash.h and stm32l4xx_hal_flash_ex.h; the suite copies ST's code */
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
void              FLASH_PageErase(uint32_t Page, uint32_t Banks);

/* Core (CMSIS) and HAL calls, faked by the suite */
uint32_t __get_PRIMASK(void);
void     __set_PRIMASK(uint32_t primask);
void     __disable_irq(void);
void     __DSB(void);
void     __ISB(void);
void     NVIC_SystemReset(void);
void     HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t preempt_priority, uint32_t sub_priority);
void     HAL_NVIC_EnableIRQ(IRQn_Type irq);
void     HAL_NVIC_DisableIRQ(IRQn_Type irq);
uint32_t HAL_GetTick(void);

#endif /* STUBS_HAL_FLASH_STM32L4XX_HAL_H */
