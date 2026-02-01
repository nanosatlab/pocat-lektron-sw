/*
 * flash2.c
 *
 *  Created on: 17 ene. 2023
 *      Author: NilRi
 */


#include "flash.h"
#include "stm32l4xx_hal.h"
#include "string.h"
#include "stdio.h"

#include "obc.h"
#include "obdh.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "FreeRTOS.h"
extern QueueHandle_t obdh_queue_handle;


static uint32_t Get_Page(uint32_t Addr)
{
  uint32_t page = 0;

  if (Addr <(FLASH_BASE + FLASH_BANK_SIZE))
  {
   /* Bank 1 */
    page = (Addr-FLASH_BASE)/FLASH_PAGE_SIZE;
  }
  else
  {
   /* Bank 2 */
    page = (Addr-(FLASH_BASE + FLASH_BANK_SIZE))/FLASH_PAGE_SIZE;
  }

  return page;
}

static uint32_t Get_Bank(uint32_t Addr)
{
	if (Addr < (FLASH_BASE + FLASH_BANK_SIZE))
    {
        return FLASH_BANK_1;
    }
    else
    {
        return FLASH_BANK_2;
    }
}

void Write_Flash(uint32_t data_addr, uint8_t *data,uint16_t n_bytes) {
	static FLASH_EraseInitTypeDef EraseInitStruct={0};
	uint32_t PAGEError;
	static uint64_t  dataSave[2048];

	HAL_FLASH_Unlock(); // Unlock the Flash to enable the flash control register access

	uint32_t StartPage = Get_Page(data_addr);
	//uint32_t EndPageAdress = data_addr + n_bytes;
	//uint32_t EndPage = Get_Page(EndPageAdress);
	uint32_t n_pages = 1; //we directly assigned to 1 because we don't want to use more than 16KB

	/******SAVE THE DATA STORED IN THE PAGE (THAT WILL BE ERASED) & THE NEW DATA IN AN ARRAY******/
	uint32_t first_page_addr, addr, n_addr;
    
    
    if (Get_Bank(data_addr) == FLASH_BANK_1) {
        first_page_addr = (StartPage * FLASH_PAGE_SIZE) + FLASH_BASE;
    } else {
        first_page_addr = (StartPage * FLASH_PAGE_SIZE) + FLASH_BASE + FLASH_BANK_SIZE;
    }

    addr = first_page_addr;
      
    n_addr = FLASH_PAGE_SIZE; //2048
   

    unsigned long long i = 0, j = 0;

    while(i < n_addr){
        if (addr == data_addr && j < n_bytes){ 
            // Save data we want to write
            while(j < n_bytes){
                dataSave[i] = data[j];
                i++; j++; addr++;
            }
        }else{
            // Save past data
            dataSave[i] = *(__IO uint8_t*)addr;
            i++; addr++;
        }
    }

	/******ERASE THE PAGE WHERE THE ADDR DATA IS CONTAINED******/

      EraseInitStruct.Banks = Get_Bank(data_addr);       
      EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES; 
      EraseInitStruct.Page = StartPage;                  
      EraseInitStruct.NbPages = n_pages;                 

      while(HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError) != HAL_OK); 

    /******WRITE THE WHOLE PAGE IN DOUBLEWORDS FORM WITH THE NEW DATA******/
    uint64_t doubleWord;
    addr = first_page_addr;
    i = 0;

    while (i < n_addr) {

        doubleWord = ((dataSave[i]) |(dataSave[i+1] << 8) | (dataSave[i+2]) << 16 |(dataSave[i+3] << 24)|
                      (dataSave[i+4] << 32) | (dataSave[i+5] << 40) | (dataSave[i+6] << 48) | (dataSave[i+7] << 56));

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, doubleWord) == HAL_OK) {
            addr+=8;
            i+=8;;
        } else {
            
            break; 
            
        }
    }

    HAL_FLASH_Lock();// Lock the Flash to disable the flash control register access (protectagainst unwanted operation).

}

void Read_Flash(uint32_t data_addr, uint8_t *RxBuf,
		uint16_t n_bytes) {

	//xSemaphoreTake(xMutex,portMAX_DELAY);
	while (1) {
		*RxBuf = *(__IO uint8_t*) data_addr;
		data_addr += 1;
		RxBuf++;
		n_bytes--;
		if (n_bytes == 0)
			break;
	}
	//xSemaphoreGive(xMutex);
}

void Send_to_WFQueue(uint8_t* pointer,uint32_t arrayLength,uint32_t addr,DataSource_t DataSource)
{
	QueueData_t TxQueueData = {pointer,arrayLength,addr,DataSource};
	BaseType_t __attribute__((unused)) xQueueStatus = xQueueSendToBack(FLASH_Queue,&TxQueueData,portMAX_DELAY);
}

void erase_page(uint32_t data_addr)
{
	static FLASH_EraseInitTypeDef EraseInitStruct;

	uint32_t StartPage = Get_Page(data_addr);
	uint32_t PAGEError;

	HAL_FLASH_Unlock();

	EraseInitStruct.Banks = Get_Bank(data_addr);
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Page = StartPage;
	EraseInitStruct.NbPages = 1;

	while(HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError) != HAL_OK);

	HAL_FLASH_Lock();
}

typedef uint64_t flash_datatype;
#define DATA_SIZE sizeof(flash_datatype)


void store_flash_memory(uint32_t memory_address, uint8_t *data, uint16_t data_length)
{
   uint8_t double_word_data[DATA_SIZE];
   FLASH_EraseInitTypeDef flash_erase_struct = {0};
   HAL_FLASH_Unlock();
   // defining the members of a struct
   flash_erase_struct.TypeErase = FLASH_TYPEERASE_PAGES;
   // defining an onset number page to be erased
   flash_erase_struct.Page = (memory_address - FLASH_BASE) / FLASH_PAGE_SIZE;
   // number of pages to remove
   flash_erase_struct.NbPages = 1 + data_length / FLASH_PAGE_SIZE;
   // identify the flash bank
   if(memory_address >  FLASH_BANK1_END && memory_address < FLASH_BANK2_END )
   {
	flash_erase_struct.Banks = FLASH_BANK_2;
   }
   else if(memory_address >  FLASH_BASE && memory_address < FLASH_BANK1_END)
   {
	flash_erase_struct.Banks = FLASH_BANK_1;
   }
   else
   {
	printf("illegal memory address \n");
	UsageFault_Handler();
   }
   uint32_t  error_status = 0;

   // erase the pages, this step is mandatory
   HAL_FLASHEx_Erase(&flash_erase_struct, &error_status);
   int i = 0;
   // using while loop, convey all data to the flash memory
   while ( i <= data_length)
   {
	double_word_data[i % DATA_SIZE] = data[i];
	i++;
	if (i % DATA_SIZE == 0)
	{
	  HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, memory_address + i -
		DATA_SIZE, *((uint64_t *)double_word_data));

	}
   }
   // convey data if something left
   if (i % DATA_SIZE != 0)
   {
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, memory_address + i
		- i % DATA_SIZE, *((flash_datatype *)double_word_data));
   }
	// lock the memory
   HAL_FLASH_Lock();
}


HAL_StatusTypeDef OBDH_Write_Request(uint32_t address, uint8_t *data, size_t len)
{
    obdh_request request;
    HAL_StatusTypeDef operation_status = HAL_ERROR; //Variable that indicates the feedback
    uint32_t received_events =0;
    request.op=FLASH_WRITE;
    request.addr=address;
    request.buf=data;
    request.len=len;
    request.client=xTaskGetCurrentTaskHandle();
    request.res=&operation_status;
//Timeout 100ms
    if(xQueueSend(obdh_queue_handle,&request,pdMS_TO_TICKS(100))!=pdPASS)
    {
        return HAL_BUSY; //Queue full
    }
    
    //Block and return result. Timeout of 2 seconds
    
    BaseType_t result_wait= xTaskNotifyWait(0,OBC_EVENT_OBDH_DONE,&received_events,pdMS_TO_TICKS(2000));
    if (result_wait == pdPASS)
    {
        if (received_events & OBC_EVENT_OBDH_DONE)
        {
            return operation_status;
        }
    }
    return HAL_TIMEOUT; //If after 2 seconds nothing is recieved, timeout. 

    //HAL_BUSY
}