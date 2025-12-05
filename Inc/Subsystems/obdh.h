#ifndef OBDH_H
#define OBDH_H

/* --- Includes obligatoris --- */
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* ---- Type definitions ---- */
// Definim els tipus AQUÍ perquè tothom (obc.c i obdh.c) els pugui veure

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
} obdh_request;

/* ---- Module-level variables (Exposed) ---- */
extern QueueHandle_t obdh_queue_handle;

/* ---- Function Prototypes ---- */
void obdh_task(void *pv_parameters);

#endif /* OBDH_H */