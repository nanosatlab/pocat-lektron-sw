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

/* ---- Constants ---- */

#define COMMS_PKT_SIZE    48
#define ARQ_MAX_RETRIES   3
#define ACK_TIMEOUT_MS    4000

/* Protocol identifiers (link-layer) */
#define COMMS_MISSION_ID  0xC8
#define COMMS_PQ_ID       0x9D
#define COMMS_ACK_TYPE    2

/* ---- Type definitions ---- */

typedef enum {
        SLEEP,
        PROCESS,
        TRANSMIT,

} CommsState_t;


/**
 * @brief Received packet with metadata.
 */
typedef struct {
    uint8_t  data[COMMS_PKT_SIZE];
    uint16_t length;
    int16_t  rssi;
    int8_t   snr;
} RxPacket_t;

/**
 * @brief TX queue entry (packet with metadata for transmission).
 */
typedef struct {
    uint8_t data[COMMS_PKT_SIZE];
    uint8_t length;
    uint8_t tries;
    uint8_t seq_num;
    uint8_t needs_ack;
} TxQueueEntry_t;

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