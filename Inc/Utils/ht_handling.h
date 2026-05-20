/**
 * @file ht_handling.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2026-05-12
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#ifndef __HT_HANDLING_H__
#define __HT_HANDLING_H__

#include <stdint.h>

#define POCKET_PLUS_PERIOD 12
#define BEACON_SIZE 16
#define OBDH_MAX_HT12_MESSAGES 256 //256 blocs*12beacons/bloc=3072 beacons

#define telemetry_circular_flag 0x1234567

//Potser falta per definir mida block a la flash que ocupara

typedef struct __attribute__ ((__packed__)) CircularFileHandler {
    int current_ht_count;
    int reading_pointer;
    int writing_pointer;
    int flag;
}CircularFlashHandler;


/**
 * @brief Take an instant telemetry (IT), accomulate it at RAN and if it reaches POCKET_PLUS_PERIOD it is stored
 * at the flash-
 * 
 * @param ch circular flash handler
 * @param it instant telemetry from beacon
 */
void fill_ht_from_it(CircularFlashHandler *ch, uint8_t *it);

int save_ht_to_circular_storage(CircularFlashHandler *ch);