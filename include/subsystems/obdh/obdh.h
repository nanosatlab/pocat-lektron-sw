/**
 * @file obdh.h
 * @author Medir Segura medir.segura@estudiantat.upc.edu
 * @brief Resposible for the data management (housekeeping data, scientific data, and configurations) within the spacecraft.
 * Primary focus now is saving and retrieving data from flash.
 * 
 */

#ifndef INC_OBDH_H_
#define INC_OBDH_H_

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "ht_handling.h"



#define OBDH_TELEMETRY_PERIOD_MS 10000 //To be defined


/**
 * @brief Flash operation type.
 */
typedef enum {
    FLASH_READ = 0,    /**< Read data from flash into the destination buffer. */
    FLASH_WRITE = 1,   /**< Write source buffer data to flash (read-modify-erase-rewrite of each page touched). */
    FLASH_PROGRAM = 2, /**< Program source buffer into already-erased flash, no erase. */
    FLASH_ERASE = 3    /**< Erase the 2 KB page containing addr; len and buf are unused. */
} obdh_flash_op;

/**
 * @brief Flash access request processed by the OBDH task.
 *
 * Requesters send this structure to obdh_queue_handle. The OBDH task performs
 * the selected operation and notifies client on OBDH_NOTIFY_IDX with the
 * resulting HAL status carried as the notification value.
 */
typedef struct {
    obdh_flash_op op;    // Operació: llegir, escriure, programar o esborrar
    uint32_t addr;       // Adreça de la Flash
    size_t len;          // Longitud en bytes
    union {
        const uint8_t *src;  // FLASH_WRITE: dades a escriure (només lectura)
        uint8_t       *dst;  // FLASH_READ:  buffer a omplir
    } buf;
    TaskHandle_t client; // Tarea que demana l'operació (per notificar-la)
    uint32_t token;      // Identifies this request so a late/stale completion can be rejected
} obdh_request;

/*
 * The OBDH completion notification (sent on OBDH_NOTIFY_IDX) packs the request
 * token and the HAL status into one 32-bit value, so a requester can tell its
 * own completion apart from a late one left over from a request that already
 * timed out:  value = (token << OBDH_STATUS_BITS) | status.
 */
#define OBDH_STATUS_BITS  4u
#define OBDH_STATUS_MASK  0x0Fu
#define OBDH_TOKEN_MASK   (0xFFFFFFFFu >> OBDH_STATUS_BITS)   /* 28-bit token space */

/** @brief Queue used to send flash access requests to the OBDH task. */
extern QueueHandle_t obdh_queue_handle;
extern CircularFlashHandler telemetry_handler;

/**
 * @brief OBDH FreeRTOS task entry point.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void obdh_task(void *pv_parameters);

#endif /* INC_OBDH_H_ */
