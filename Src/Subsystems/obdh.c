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
#include "flash.h"
/* ---- Macros and constants ---- */
/* ---- Module-level variables ---- */
QueueHandle_t obdh_queue_handle;
CircularFlashHandler telemetry_handler;


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

void obdh_save_pointers_flash(void)
{
    Write_Flash(HT_POINTER_ADDR, (uint8_t*)&telemetry_handler, sizeof(CircularFlashHandler));
}

/* ---- Private function definitions ---- */


void setup_obdh(void) {

    /*
    //mirem on ens haviem quedat en memoria. 
    ht_count=0;
    ht_head=0;
    uint32_t prev_epoch=0;
    uint8_t buf[4]; // First 4 bytes are epoch

    for (uint8_t i=0;i<MAX_HT_BEACONS;i++)
    {
        uint32_t addr=HT_BASE_ADDR+(i*HT_BEACON_SIZE);
        
        Read_Flash(addr,buf,4);
        uint32_t curr_epoch=buf[0]<<24 |buf[1]<<16| buf[2]<<8| buf[3] ;
        if(curr_epoch==0xFFFFFFFF || curr_epoch==0)
        {
            ht_head=i;
            break;//Posició on acaba la cua
        }
        else if (curr_epoch < prev_epoch) {
            
            ht_head = i;
            ht_count = MAX_HT_BEACONS; //Hem fet la volta
            break;
        }

        else
        {
            ht_count++;
            prev_epoch=curr_epoch;
        }

    }

*/

    printf("Setting up OBDH...\n");
    Read_Flash(HT_POINTER_ADDR, (uint8_t*)&telemetry_handler, sizeof(CircularFlashHandler));
    if(telemetry_handler.flag==telemetry_circular_flag)
    {
        printf("telemetria circular creada\n");
    }
    else    
    {
        printf("telemetria circular no creada\n");
        telemetry_handler.flag=telemetry_circular_flag;
        telemetry_handler.current_ht_count=0;
        telemetry_handler.reading_pointer=0;
        telemetry_handler.writing_pointer=0;
        obdh_save_pointers_flash();
        

    }
    


}

/**
 * @brief This process waits for an element of the queue to be recived,
 * a request. The request can be to read flash or to write flash.
 * When operations are done, then a notification(with flags) is
 * given to the OBC with an event. 
 * 
 * 
 */
void process_obdh(void) {
    obdh_request request;
    HAL_StatusTypeDef status=HAL_OK;
    printf("Processing OBDH...\n");


    if (xQueueReceive(obdh_queue_handle,&request,pdMS_TO_TICKS(OBDH_TELEMETRY_PERIOD_MS))== pdPASS)
    {
        if(request.op==FLASH_READ)
        {
            
            
            if(request.buf!=NULL)
            {
                Read_Flash(request.addr, request.buf,
		request.len);
               
            }
            /*if(request.client != NULL) {
                xTaskNotify(request.client, OBC_EVENT_OBDH_DONE, eSetBits);//We send a notification to the task
                //vTaskDelay(100/portTICK_PERIOD_MS);
            }*/
            status=HAL_OK;
        }
        else if(request.op == FLASH_WRITE)
        
        {
            if(request.buf != NULL)
            {
                Write_Flash(request.addr, request.buf, request.len);
                status=HAL_OK;

               /*
                if(request.client != NULL) {
                    xTaskNotify(request.client, OBC_EVENT_OBDH_DONE, eSetBits);
                }
                */
            }
            else
            {
                status=HAL_ERROR;

            }
        }

        if (request.res != NULL)
        {
            *(request.res)=status;
        }
        if (request.client!=NULL)
        {
            xTaskNotify(request.client,OBC_EVENT_OBDH_DONE,eSetBits);
        }
        
    }
    
 } 
/*
HAL_StatusTypeDef obdh_get_telemetry(uint8_t *buffer)
{
    uint8_t index;
    uint32_t address;
    HAL_StatusTypeDef read_state;

    if(ht_count==0)
    {
        return HAL_ERROR;
        
    }
    index=(ht_head+MAX_HT_BEACONS-ht_count)%MAX_HT_BEACONS;
    address=HT_BASE_ADDR+(index*HT_BEACON_SIZE);
    read_state=OBDH_Read_Request(address,buffer,HT_BEACON_SIZE);
    if(read_state==HAL_OK)
    {
        ht_count--;
    }
    return read_state;
}
*/
/*
HAL_StatusTypeDef obdh_insert_telemetry(uint8_t *buffer)
{ 
    uint32_t address;
    HAL_StatusTypeDef write_state;

    address=HT_BASE_ADDR+(ht_head*HT_BEACON_SIZE);
    write_state=OBDH_Write_Request(address,buffer,HT_BEACON_SIZE);
    if (write_state==HAL_OK)
    {
        ht_head=(ht_head+1)%HT_BEACON_SIZE;
        if(ht_head<HT_BEACON_SIZE)
        {
            ht_count++;
        }
    }
    return write_state;  


}

*/