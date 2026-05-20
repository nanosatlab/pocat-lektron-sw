#ifndef OBDH_H
#define OBDH_H

/* --- Includes obligatoris --- */
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "ht_handling.h"

/* ---- Type definitions ---- */

#define OBDH_TELEMETRY_PERIOD_MS 10000 //To be defined
#define MAX_HT_BEACONS 10 //Arbitrary for testing, to be discussed
#define HT_BEACON_SIZE 16 // following PoCat TM_TC DATABASE, this might change
#define HT_BASE_ADDR 0x080FE000 //To be defined
#define HT_POINTER_ADDR 0x080FD000


typedef enum {
    FLASH_READ = 0,
    FLASH_WRITE = 1
} read_write;

typedef struct {
    read_write op;       // Operació: llegir o escriure
    uint32_t addr;       // Adreça de la Flash
    size_t len;          // Longitud en bytes
    uint8_t *buf;        // Punter al buffer de dades
    TaskHandle_t client; // Tarea que demana l'operació (per notificar-la)
    HAL_StatusTypeDef *res; //Punter que retorna l'estatus de la escriptura/lectura
} obdh_request;




/* ---- Module-level variables (Exposed) ---- */
extern QueueHandle_t obdh_queue_handle;
extern CircularFlashHandler telemetry_handler;

/* ---- Function Prototypes ---- */
void obdh_task(void *pv_parameters);
void obdh_save_pointers_flash(void);
HAL_StatusTypeDef obdh_get_telemetry(uint8_t *buffer);
HAL_StatusTypeDef obdh_insert_telemetry(uint8_t *buffer);
#endif /* OBDH_H */