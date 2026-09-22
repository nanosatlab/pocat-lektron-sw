/**
 * @file comms.h
 * @brief Communications subsystem: protocol logic and packet queuing.
 * @version 0.2
 * @date 2026-03-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef INC_COMMS_H_
#define INC_COMMS_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "frame.h"

/* ---- Constants ---- */

#define COMMS_QUEUE_LEN    2

/* ---- Type definitions ---- */

/**
 * @brief TX queue entry (air frame with metadata for transmission).
 */
typedef struct {
    uint8_t frame[AIR_FRAME_MAX];
    uint8_t frame_len;
    uint8_t needs_ack;
} TxQueueEntry_t;

/**
 * @brief Received air frame with link-quality metadata.
 */
typedef struct {
    AirFrame_t air;
    int16_t    rssi;
    int8_t     snr;
} RxAirFrame_t;

/**
 * @brief Allocate the next SAT->GS frame sequence number.
 */
uint8_t comms_next_seq(void);

/**
 * @brief Communications FreeRTOS task entry point: handles protocol logic and packet processing.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void comms_task(void *pv_parameters);

/**
 * @brief Get the RX queue handle (for receiving packets from transceiver_task).
 */
QueueHandle_t comms_get_rx_queue(void);

/**
 * @brief Get the TX queue handle (for sending packets to transceiver_task).
 */
QueueHandle_t comms_get_tx_queue(void);

#endif /* INC_COMMS_H_ */
