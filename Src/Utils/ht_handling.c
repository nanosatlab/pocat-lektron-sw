/**
 * @file ht_handling.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2026-05-12
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include "ht_handling.h"
#include "obdh.h"
#include "flash.h"
#include "compression.h" 
#include <string.h>

static uint8_t ht_temporal_buffer[POCKET_PLUS_PERIOD][BEACON_SIZE];
static uint8_t temporal_count=0;

static uint_fast16_t pocket_plus_get_binary_data(uint8_t * binaryBuffer,
                                                 uint32_t * binaryData,
                                                 uint_fast16_t size)
{
    uint_fast16_t arrayLenght = (size + 3 ) / 4; //lenght for the 32 bit uint32_t array.
    memset(binaryData, 0, sizeof(*binaryData)*arrayLenght);

    uint_fast8_t i = 0; //counter the bitshift. (i = 0 -> bitshift 8, i = 1 -> bitshift 16 ...) must be reseted when reaching 4
    uint32_t bytesToInt = 0; //variable for casting 4 bytes into one 32 bit unsigned integer
    int_fast16_t currentWord = arrayLenght - 1;

    for(int_fast32_t currentByte = size - 1; currentByte >= 0; currentByte-- ) {

        bytesToInt |= binaryBuffer[currentByte] << (i * 8);
        i += 1;
        if (i == 4){
            memcpy(binaryData+currentWord , &bytesToInt, sizeof(*binaryData));
            bytesToInt = 0;
            i = 0;
            currentWord -=1;
        }
    }
    if( (i < 4) && (currentWord == 0) ){
        memcpy(binaryData+currentWord , &bytesToInt, sizeof(*binaryData));
    }
    return arrayLenght;
}

int save_ht_to_circular_storage(CircularFlashHandler *ch)
{
    //Aqui fem tota la compressió 
    //Falta implementar

    //adreça a escriure
    HAL_StatusTypeDef status= OBDH_Write_Request();//escrivim el bloc comprimit
    if (status==HAL_OK)
    {
        ch.writing_pointer=(ch.writing_pointer+1)%OBDH_MAX_HT12_MESSAGES;
        if(ch.current_ht_count<OBDH_MAX_HT12_MESSAGES)
        {
            ch.current_ht_count++;
        }
        obdh_save_pointers_flash();
        return 0;
    }
    else
    {
        return -1;
    }
}

void fill_ht_from_it(CircularFlashHandler *ch, uint8_t *it)
{
    //copiem beacon que ha arribat de it
    //* falta implementar
    temporal_count++;
    if(temporal_count>=POCKET_PLUS_PERIOD)
    {
        save_ht_to_circular_storage(ch);
        temporal_count=0;
    }
}
