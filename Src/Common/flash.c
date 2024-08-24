/*
 * flash2.c
 *
 *  Created on: 17 ene. 2023
 *      Author: NilRi
 */

#include "flash.h"
#include "stdio.h"
#include "stm32l4xx_hal.h"
#include "string.h"

#include "Subsystems/obc.h"

#include "FreeRTOS.h"

// Define the pointers to the memory addresses
volatile uint8_t *State = (uint8_t *)CURRENT_STATE_ADDR;
volatile uint8_t *previousState = (uint8_t *)PREVIOUS_STATE_ADDR;

static uint32_t Get_Page(uint32_t Addr) {
  uint32_t page = 0;

  if (Addr < (FLASH_BASE + FLASH_BANK_SIZE)) {
    /* Bank 1 */
    page = (Addr - FLASH_BASE) / FLASH_PAGE_SIZE;
  } else {
    /* Bank 2 */
    page = (Addr - (FLASH_BASE + FLASH_BANK_SIZE)) / FLASH_PAGE_SIZE;
  }

  return page;
}

static uint32_t Get_Bank(uint32_t Addr) { return FLASH_BANK_1; }

void Write_Flash(uint32_t data_addr, uint8_t *data, uint16_t n_bytes) {
  static FLASH_EraseInitTypeDef EraseInitStruct;
  uint32_t PAGEError;

  HAL_FLASH_Unlock(); // Unlock the Flash to enable the flash control register
                      // access

  uint32_t StartPage = Get_Page(data_addr);
  uint32_t EndPageAdress = data_addr + n_bytes;
  uint32_t EndPage = Get_Page(EndPageAdress);
  uint32_t n_pages = (EndPage - StartPage) + 1;

  /******SAVE THE DATA STORED IN THE PAGE (THAT WILL BE ERASED) & THE NEW DATA
   * IN AN ARRAY******/

  uint32_t first_page_addr, addr, n_addr;
  first_page_addr = StartPage * 1024 * 2 + 0x08000000;
  addr = first_page_addr;
  n_addr = 1024 * 2 * n_pages; // Number of addresses we want to save.
                               // 2KB for storing a whole PAGE.

  uint64_t
      dataSave[n_addr]; // Array that stores the data we want to preserve from
                        // the past plus the new one. The type of the array
                        // elements should be uint64_t or higher since we are
                        // performing bit shifts with it at the last loop.
  unsigned long long i = 0, j = 0;

  while (i < n_addr) {
    if (addr == data_addr) {
      // Save data we want to write
      while (j < n_bytes) {
        dataSave[i] = data[j];
        i++;
        j++;
        addr++;
      }
    } else {
      // Save past data
      dataSave[i] = *(__IO uint8_t *)addr;
      i++;
      addr++;
    }
  }

  /******ERASE THE PAGE WHERE THE ADDR DATA IS CONTAINED******/

  EraseInitStruct.Banks =
      Get_Bank(data_addr); // Get the bank where the data addr is located
  EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES; // Erase by page
  EraseInitStruct.Page = StartPage; // Get the first page position
  EraseInitStruct.NbPages =
      n_pages; // Erase the pages we are going to write on.

  while (HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError) != HAL_OK) {

  }; // loop that attempts to erase the page until it returns HAL_OK

  /******WRITE THE WHOLE PAGE IN DOUBLEWORDS FORM WITH THE NEW DATA******/
  uint64_t doubleWord;
  addr = first_page_addr;
  i = 0;

  while (i < n_addr) {

    doubleWord = ((dataSave[i]) | (dataSave[i + 1] << 8) |
                  (dataSave[i + 2]) << 16 | (dataSave[i + 3] << 24) |
                  (dataSave[i + 4] << 32) | (dataSave[i + 5] << 40) |
                  (dataSave[i + 6] << 48) | (dataSave[i + 7] << 56));

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, doubleWord) ==
        HAL_OK) {
      addr += 8;
      i += 8;
      ;
    } else {
      HAL_FLASH_GetError();
      // return HAL_FLASH_GetError();
    }
  }

  HAL_FLASH_Lock(); // Lock the Flash to disable the flash control register
                    // access (protectagainst unwanted operation).
}

void Read_Flash(uint32_t data_addr, uint8_t *RxBuf, uint16_t n_bytes) {

  // xSemaphoreTake(xMutex,portMAX_DELAY);
  while (1) {
    *RxBuf = *(__IO uint8_t *)data_addr;
    data_addr += 1;
    RxBuf++;
    n_bytes--;
    if (n_bytes == 0)
      break;
  }
  // xSemaphoreGive(xMutex);
}

void Send_to_WFQueue(uint8_t *pointer, uint32_t arrayLength, uint32_t addr,
                     DataSource_t DataSource) {
  QueueData_t TxQueueData = {pointer, arrayLength, addr, DataSource};
  BaseType_t __attribute__((unused)) xQueueStatus =
      xQueueSendToBack(FLASH_Queue, &TxQueueData, portMAX_DELAY);
}
