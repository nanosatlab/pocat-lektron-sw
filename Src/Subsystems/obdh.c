// oversees the management of internal data within the spacecraft. Its primary focus includes
// housekeeping data, scientific data, and configurations, as well as managing access to flash
// memory. (primary focus now is saving and retrieving data from flash)


/* ---- Includes ---- */
#include "obdh.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "main.h"
#include "queue.h"
/* ---- Macros and constants ---- */
/* ---- Module-level variables ---- */
QueueHandle_t obdh_queue_handle;
//extern QueueHandle_t obdh_queue_handle;//Extern queue declared in obc

/* ---- Private function prototypes ---- */
void setup_obdh(void);
void process_obdh(void);
/* ---- Public function definitions ---- */
void obdh_task(void *pv_parameters) {
    setup_obdh();

    for (;;) {
        process_obdh();
    }
}

/* ---- Private function definitions ---- */


void setup_obdh(void) {


    printf("Setting up OBDH...\n");
    // Apply the default configuration
    


}


void process_obdh(void) {
    obdh_request request;
    HAL_StatusTypeDef status;
    printf("Processing OBDH...\n");


    if (xQueueReceive(obdh_queue_handle,&request,portMAX_DELAY)== pdPASS)
    {
        if(request.op==FLASH_READ)
        {
            
            
            if(request.buf!=NULL)
            {
                Read_Flash(request.addr, request.buf,
		request.len);
               
            }
            if(request.client != NULL) {
                xTaskNotifyGive(request.client);//We send a notification to the task
                //vTaskDelay(100/portTICK_PERIOD_MS);
            }
        }
        else if(request.op == FLASH_WRITE)
        {
            if(request.buf != NULL)
            {
                Write_Flash(request.addr, request.buf, request.len);
                

                // Avisem al client que hem acabat
                if(request.client != NULL) {
                    xTaskNotifyGive(request.client);
                }
            }
        }
        
    }
    /*else
    {
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
  */  
    //
    // Gestión de la flash:
    // Leemos datos de la cola de la tarea (donde habran peticiones de read o write de otras tareas
    // que quieran acceder a la memoria flash)
    // La información de cada elemento en la cola será el siguiente struct:
    //  *   - op      : FLASH_READ o FLASH_WRITE
    //  *   - addr    : dirección en flash
    //  *   - len     : número de bytes
    //  *   - buf     : puntero al buffer (src en WRITE, dst en READ)
    //  *   - client  : TaskHandle_t de la tarea solicitante (para notificación de fin)
    // A partir de esto si es FLASH_READ leemos la flash y ponemos la info en buf, si es FLASH_WRITE
    // escribimos la info de buf en la flash
    // Si es FLASH_READ notificamos a la tarea solicitante que la información esta disponible en la
    // posición que nos ha pasado con buf (aquí suponemos que la tarea solicitante sabe cuanto ocupa la
    // información que pide de flash).


}

/*
                HAL_FLASH_Unlock(); // Desbloquegem la flash

                // 1. NETEJAR FLAGS D'ERROR 
                __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
              
                //Esborrem tot el que hi ha a la flash per evitar errors per testejar el codi
                FLASH_EraseInitTypeDef EraseInitStruct;
                uint32_t PageError;
                
                // Calculem la pàgina (STM32L4 té pàgines de 2KB)
                uint32_t page = (request.addr - FLASH_BASE) / FLASH_PAGE_SIZE;
               
                EraseInitStruct.TypeErase   = FLASH_TYPEERASE_PAGES;
                EraseInitStruct.Banks       = FLASH_BANK_2; // 0x08080000 cau al Banc 2 normalment
                EraseInitStruct.Page        = page;
                EraseInitStruct.NbPages     = 1;

                if (HAL_FLASHEx_Erase(&EraseInitStruct, &PageError) != HAL_OK) {
                    printf("Error esborrant la Flash: %lu\n", HAL_FLASH_GetError());
                }
                 else {
                    
                   
                    size_t bytes_left = request.len;
                    uint64_t data_to_write;
                    size_t temporal_size_bytes;
                    uint32_t temporal_adress = request.addr;
                    uint8_t *temporal_pointer = request.buf;
                    
                    while (bytes_left > 0)
                    {
                        data_to_write = 0xFFFFFFFFFFFFFFFF; 

                        if (bytes_left >= 8) {
                            temporal_size_bytes = 8;
                        } else {
                            temporal_size_bytes = bytes_left;
                        }

                        memcpy(&data_to_write, temporal_pointer, temporal_size_bytes);
                        
                        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, temporal_adress, data_to_write);
                    
                        if (status != HAL_OK)
                        {
                            
                            printf("Hal error: %lu\n", HAL_FLASH_GetError());
                            break;
                        }
                    
                        if(bytes_left >= 8){
                            bytes_left = bytes_left - 8;
                        } else {
                            bytes_left = 0;
                        }
                        
                        temporal_adress += 8;
                        temporal_pointer += 8;
                    }
                }
         
                HAL_FLASH_Lock(); // Bloquegem la flash
                */
