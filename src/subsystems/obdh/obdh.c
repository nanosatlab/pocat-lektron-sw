/**
 * @file obdh.c
 * @author Medir Segura medir.segura@estudiantat.upc.edu
 * @brief Implementation of the OBDH task.
 * 
 */


#include "obdh.h"
#include <stdbool.h>
#include <stdio.h>
#include "health.h"
#include <stdint.h>
#include <string.h>
#include "main.h"
#include "queue.h"
#include "flash.h"
#include "notifications.h"
#include "task_management.h"

QueueHandle_t obdh_queue_handle;
static uint32_t deferred_notifications;
CircularFlashHandler telemetry_handler;

static void setup_obdh(void);
static void process_obdh(void);


void obdh_task(void *pv_parameters) {
    (void)pv_parameters;
    setup_obdh();

    for (;;) {
        /* process_obdh() blocks in xQueueReceive (up to 1 s), which paces this loop */
        process_obdh();
        health_kick(HEALTH_BIT_OBDH);
    }

}

void obdh_save_pointers_flash(void)
{
    Write_Flash(HT_POINTER_ADDR, (uint8_t*)&telemetry_handler, sizeof(CircularFlashHandler));
}

/**
 * @brief Initialize OBDH task state.
 */
static void setup_obdh(void) {
    deferred_notifications = 0;
    // Apply the default configuration
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
        printf("Telemetria circular creada\n");
    }
    else    
    {
        printf("Telemetria circular no creada\n");
        telemetry_handler.flag=telemetry_circular_flag;
        telemetry_handler.current_ht_count=0;
        telemetry_handler.reading_pointer=0;
        telemetry_handler.writing_pointer=0;
        obdh_save_pointers_flash();
        

    }
}

/**
 * @brief Execute one OBDH task processing cycle.
 *
 * Handles pause/resume notifications, receives one pending flash request from
 * the OBDH queue, performs the requested read or write operation, stores the
 * operation status in the request result pointer, and notifies the requesting
 * task when the operation is complete.
 */
static void process_obdh(void) {

    uint32_t notifications = 0;
    // Non-blocking poll: OBDH paces on its request queue (xQueueReceive) below.
    notifications = wait_for_notification(0);

    if (tm_check_pause(notifications, &deferred_notifications))
        return;

    notifications |= deferred_notifications;
    deferred_notifications = 0;
    
    obdh_request request;
    HAL_StatusTypeDef status=HAL_OK;

    BaseType_t result_queue= xQueueReceive(obdh_queue_handle,&request,pdMS_TO_TICKS(1000));
    if (result_queue== pdPASS)
    {
        if(request.op==FLASH_READ)
        {
            if(request.buf.dst!=NULL)
            {
                Read_Flash(request.addr, request.buf.dst, request.len);
            }
            /*if(request.client != NULL) {
                xTaskNotify(request.client, OBC_EVENT_OBDH_DONE, eSetBits);//We send a notification to the task
                //vTaskDelay(100/portTICK_PERIOD_MS);
            }*/
            status=HAL_OK;
        }
        else if(request.op == FLASH_WRITE)

        {
            if(request.buf.src != NULL)
            {
                Write_Flash(request.addr, request.buf.src, request.len);
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

        if (request.client != NULL)
        {
            uint32_t completion = (request.token << OBDH_STATUS_BITS)
                                | ((uint32_t)status & OBDH_STATUS_MASK);
            xTaskNotifyIndexed(request.client, OBDH_NOTIFY_IDX,
                               completion, eSetValueWithOverwrite);
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
