/**
 * @file flash.c
 * @brief Internal flash memory access implementation.
 * @details
 * Implements raw STM32 flash page erase/write helpers and request functions
 * that route task flash access through the OBDH queue.
 * @author Medir Segura
 * @date 2023-01-17
 * @note Modified on 2026-03-04.
 */


#include "flash.h"
#include "stm32l4xx_hal.h"
#include "stm32l4xx_it.h"
#include "string.h"
#include "stdio.h"

#include "obc.h"
#include "obdh.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "FreeRTOS.h"
#include "notifications.h"

extern QueueHandle_t obdh_queue_handle;

static uint32_t get_page(uint32_t Addr);
static uint32_t get_bank(uint32_t Addr);
static HAL_StatusTypeDef obdh_submit_request(obdh_request *request);


void Write_Flash(uint32_t data_addr, const uint8_t *data, uint16_t n_bytes) {
	
    static uint8_t  dataSave[FLASH_PAGE_SIZE];

	  HAL_FLASH_Unlock(); // Unlock the Flash to enable the flash control register access

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS); // Clear error flags from previous operations. We assume that they have been handled already

    if (n_bytes == 0) {
        return;
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
            return;
        }

        /* Write the page back, one doubleword at a time */
        for (uint32_t i = 0; i < FLASH_PAGE_SIZE; i += 8) {
            uint64_t doubleWord;
            memcpy(&doubleWord, dataSave + i, sizeof(doubleWord));
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, page_addr + i, doubleWord) != HAL_OK) {
                HAL_FLASH_Lock();
                printf("Error programming flash at address 0x%08lX, error code %lu\n", (unsigned long)(page_addr + i), (unsigned long)HAL_FLASH_GetError());
                return;
            }
        }
    }

    HAL_FLASH_Lock(); // Lock the Flash to disable the flash control register access (protectagainst unwanted operation).

}

void Read_Flash(uint32_t data_addr, uint8_t *data, uint16_t n_bytes) {
    memcpy(data, (const void *)data_addr, n_bytes);
}

HAL_StatusTypeDef OBDH_Write_Request(uint32_t address, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0)
        return HAL_ERROR;

    obdh_request request = {
        .op      = FLASH_WRITE,
        .addr    = address,
        .len     = len,
        .buf.src = data,
    };
    return obdh_submit_request(&request);
}

HAL_StatusTypeDef OBDH_Read_Request(uint32_t address, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0)
        return HAL_ERROR;

    obdh_request request = {
        .op      = FLASH_READ,
        .addr    = address,
        .len     = len,
        .buf.dst = data,
    };
    return obdh_submit_request(&request);
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


/**
 * @brief Post a prepared flash request to the OBDH task and block for its result.
 *
 * Stamps the calling task handle, enqueues the request, and waits on the
 * dedicated flash notification index (OBDH_NOTIFY_IDX) for OBDH to return the
 * HAL status as the notification value. Using a dedicated index keeps this
 * round-trip off the task's general notification slot (index 0), so it can
 * never collide with the task's own notifications.
 *
 * @param request Flash request with op/addr/len/buf already populated.
 * @retval HAL_OK / HAL_ERROR  Status reported by the OBDH task.
 * @retval HAL_BUSY            Request queue still full after the send timeout.
 * @retval HAL_TIMEOUT         OBDH did not answer within the timeout.
 */
static HAL_StatusTypeDef obdh_submit_request(obdh_request *request)
{
    request->client = xTaskGetCurrentTaskHandle();

    if (xQueueSend(obdh_queue_handle, request, pdMS_TO_TICKS(FLASH_QUEUE_SEND_TIMEOUT_MS)) != pdPASS)
        return HAL_BUSY;

    uint32_t value = 0;
    if (xTaskNotifyWaitIndexed(OBDH_NOTIFY_IDX, UINT32_MAX, UINT32_MAX, &value,
                               pdMS_TO_TICKS(FLASH_OP_TIMEOUT_MS)) != pdPASS)
        return HAL_TIMEOUT;

    return (HAL_StatusTypeDef)value;
}
