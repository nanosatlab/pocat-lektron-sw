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
    FLASH_READ = 0,     /**< Read data from flash into the destination buffer. */
    FLASH_WRITE = 1,    /**< Write source buffer data to flash (read-modify-erase-rewrite of each page touched). */
    FLASH_STORE_HT = 2  /**< Store one built HT block in the circular queue; OBDH picks the slot and erases pages when needed (addr unused). */
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
        const uint8_t *src;  // FLASH_WRITE and FLASH_STORE_HT: dades a escriure
        uint8_t       *dst;  // FLASH_READ:  buffer a omplir
    } buf;
    TaskHandle_t client; // Tarea que demana l'operació (per notificar-la)
} obdh_request;

/** @brief Queue used to send flash access requests to the OBDH task. */
extern QueueHandle_t obdh_queue_handle;
extern CircularFlashHandler telemetry_handler;

/**
 * @brief OBDH FreeRTOS task entry point.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void obdh_task(void *pv_parameters);

#endif /* INC_OBDH_H_ */
