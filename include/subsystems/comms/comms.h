#ifndef INC_COMMS_H_
#define INC_COMMS_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "frame.h"

#define COMMS_QUEUE_LEN    2

typedef struct {
    uint8_t frame[AIR_FRAME_MAX];
    uint8_t frame_len;
    uint8_t needs_ack;
} TxQueueEntry_t;

typedef struct {
    AirFrame_t air;
    int16_t    rssi;
    int8_t     snr;
} RxAirFrame_t;

uint8_t comms_next_seq(void);

void comms_task(void *pv_parameters);
QueueHandle_t comms_get_rx_queue(void);
QueueHandle_t comms_get_tx_queue(void);

#endif /* INC_COMMS_H_ */
