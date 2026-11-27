/**
 * @file obdh.h
 * @author Medir Segura medir.segura@estudiantat.upc.edu
 * @brief Resposible for the data management (housekeeping data, scientific data, and configurations) within the spacecraft.
 * Primary focus now is saving and retrieving data from flash.
 * 
 */

#ifndef INC_OBDH_H_
#define INC_OBDH_H_

#include <stdbool.h>

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "task_management.h"



#define OBDH_TELEMETRY_PERIOD_MS 10000 //To be defined

#define OBDH_QUEUE_LEN   10u                    /**< Entries the OBDH queue holds, requests and posts together. */
#define OBDH_DATA_MAX    256u                   /**< Largest data a request carries or an answer returns (spec §5.2.1 rule 2). */
#define OBDH_CLIENTS     (TM_TASK_COUNT + 1u)   /**< Tasks that may send requests: the task table, then the OBC task (TM_OBC_INDEX). */

_Static_assert(OBDH_QUEUE_LEN > OBDH_CLIENTS, "posts may use only the entries beyond one per client (V14)");


/**
 * @brief What a queue entry asks OBDH to do.
 *
 * Each operation runs a store: no task asks for raw flash access. Every
 * operation is a request, answered on the client's answer queue, except
 * HT_ADD_BEACON, a post, which OBDH never answers.
 */
typedef enum {
    FLASH_PARAM_WRITE = 5,   /**< Store a parameter value with param_store_write() (param_set_request(), param_reset_request()). */
    HT_ADD_BEACON = 6,       /**< Post: add the beacon in data to the telemetry history with ht_store_add() (ht_beacon_post()). */
    HT_READ_BLOCK = 7        /**< Read the oldest telemetry-history block numbered seq or later with ht_store_read() (ht_read_request()). */
} obdh_flash_op;

/** @brief What a request asks for: everything in the queue entry but its data. */
typedef struct {
    obdh_flash_op op;         // Operació: quin magatzem i què s'hi fa
    uint32_t number;          // Número de la petició: la resposta el retorna
    uint32_t seq;             // HT_READ_BLOCK: primer número de seqüència que es vol
    uint16_t len;             // Bytes de dades que porta la petició (com a màxim OBDH_DATA_MAX); 0 en una lectura (HT_READ_BLOCK)
    uint8_t param_id;         // FLASH_PARAM_WRITE: identificador del paràmetre
    uint8_t client;           // Índex del client: la seva cua de resposta
} obdh_request_header;

/**
 * @brief One entry of the OBDH request queue (spec §5.2.1).
 *
 * The data travels by copy, so nothing in the queue points into a client's
 * memory. OBDH answers on the client's answer queue, never through a task handle.
 */
typedef struct {
    obdh_request_header header;
    uint8_t data[OBDH_DATA_MAX];   // Escriptures: les dades a escriure
} obdh_request;

/** @brief OBDH's answer to one request. */
typedef struct {
    uint32_t number;               // Número de la petició que respon
    HAL_StatusTypeDef status;      // Resultat de l'operació
    uint32_t len;                  // HT_READ_BLOCK: longitud del bloc llegit
    uint32_t seq;                  // HT_READ_BLOCK: número de seqüència del bloc llegit
    uint8_t data[OBDH_DATA_MAX];   // Lectures: les dades llegides
} obdh_answer;

/** @brief Queue used to send flash access requests to the OBDH task. */
extern QueueHandle_t obdh_queue_handle;

/** @brief One-entry answer queue per client, indexed like OBDH_CLIENTS; created once, never deleted. */
extern QueueHandle_t obdh_answer_queues[OBDH_CLIENTS];

/**
 * @brief Creates the request queue and the answer queues. The OBC task calls it once, before it creates any task.
 * @return true if every queue was created.
 */
bool obdh_queues_create(void);

/**
 * @brief Whether the caller may run a store's OBDH-side functions: the OBDH task, or main() before the scheduler starts.
 * @details Spec §5.2.1 rule 9 (FLS-REQ-110). Each such function calls it
 *          first and refuses any other caller with HAL_ERROR, before it
 *          changes anything. Before the scheduler starts, main() is the only
 *          code running. After that, the caller must be the task in OBDH's row
 *          of the task table: xTaskCreate() stores a restarted OBDH's handle
 *          there before the task can run, and while OBDH is deleted the row is
 *          NULL, which no running task's handle equals.
 * @return true in the OBDH task, and in main() before the scheduler starts.
 */
static inline bool obdh_is_caller(void)
{
    return (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) ||
           (xTaskGetCurrentTaskHandle() == tm_get_task_handle(TM_TASK_OBDH));
}

/**
 * @brief OBDH FreeRTOS task entry point.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void obdh_task(void *pv_parameters);

#endif /* INC_OBDH_H_ */
