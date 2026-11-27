/**
 * @file param_store.h
 * @brief Parameter store: persistent values by ID, in two flash copies with a CRC on every record.
 * @details
 * Every value that must survive a reset or a power cut (mode, flags,
 * configuration, TLE, calibration) is a parameter with a fixed ID and size,
 * listed in PARAM_TABLE. Any task reads a value from RAM with param_get() and
 * changes it with param_set_request() or param_reset_request(), which go
 * through the OBDH task.
 *
 * A parameter that was never written has no record and takes its default from
 * PARAM_TABLE.
 * @date 2026-10-27
 */

#ifndef INC_PARAM_STORE_H_
#define INC_PARAM_STORE_H_

#include <stdint.h>
#include <stddef.h>

#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"

/**
 * @brief Every parameter, as X(id, name, size, default bytes...).
 * @details
 * - Each record stores the one-byte ID, which the load uses to find the
 *   parameter, so moving a row must keep its ID. Once an ID has flown, never
 *   renumber or reuse it; give an incompatible layout a new ID.
 * - Rows are grouped by owning subsystem. FSS (0xA0-0xBF) and OBDH
 *   (0xC0-0xDF) have ranges but no entries yet.
 * - The size is fixed, 1 to 255 bytes. Before flight, changing a size only
 *   makes the records already stored for that ID be ignored.
 * - The default gives the first bytes; the rest are zero. "TBD" defaults are
 *   still to be agreed with each owner.
 */
#define PARAM_TABLE(X) \
    /* OBC (on-board computer), IDs 0x01-0x1F. Owns both deployment flags. */ \
    X(0x02u, PARAM_DEPLOY_COMMS,      1u, 0x00u)        /* not deployed */ \
    X(0x03u, PARAM_DEPLOY_PAYLOAD,    1u, 0x00u)        /* not deployed */ \
    X(0x04u, PARAM_POL_MASK,          1u, 0x00u)        /* TBD (proposed: all enabled) */ \
    X(0x05u, PARAM_STANDBY_DONE,      1u, 0x00u)        /* not done */ \
    /* EPS (electrical power system), IDs 0x20-0x3F. */ \
    X(0x20u, PARAM_EPS_THRESHOLDS,    3u, 0x00u)        /* nominal, contingency, survival: TBD; size TBC */ \
    X(0x21u, PARAM_HEATER_AUTO,       1u, 0x00u)        /* TBD */ \
    /* COMMS (communications), IDs 0x40-0x5F. */ \
    X(0x40u, PARAM_COMMS_CONFIG,     21u, 0x00u)        /* TBD (proposed: built-in LoRa settings); size TBC */ \
    X(0x41u, PARAM_TX_INHIBIT,        1u, 0x00u)        /* off */ \
    X(0x42u, PARAM_BEACON_PERIOD,     2u, 30u, 0x00u)   /* 30 s = BEACON_PERIOD_DEFAULT_S, uint16_t little-endian; 10 to 3,600 s (beacon_period.h) */ \
    /* ADCS (attitude determination and control system), IDs 0x60-0x7F. */ \
    X(0x60u, PARAM_ADCS_CALIBRATION, 81u, 0x00u)        /* TBD (proposed: identity / zero); size TBC */ \
    X(0x61u, PARAM_TLE,             138u, 0x00u)        /* none; size TBC */ \
    /* Payload, IDs 0x80-0x9F. */ \
    X(0x80u, PARAM_RFI_CONFIG,        8u, 0x00u)        /* TBD */ \
    X(0x81u, PARAM_PL_SCHEDULE,       8u, 0x00u)        /* none */

/** @brief Parameter IDs. */
typedef enum {
#define PARAM_ID(id, name, size, ...) name = (id),
    PARAM_TABLE(PARAM_ID)
#undef PARAM_ID
} param_id_t;

/** @brief Parameter store events since boot, for flash health telemetry. */
typedef struct {
    uint32_t copy_failures[2];   /**< Writes that did not reach copy A / copy B. */
    uint32_t compactions[2];     /**< Compactions of copy A / copy B. */
} param_store_stats_t;

/* ---- Any task ---- */

/**
 * @brief Copies the current value of a parameter from RAM. Any task, any time.
 * @details The value may be a default or a stored value; its owner checks
 *          that it makes sense.
 * @param id Parameter.
 * @param value Destination buffer.
 * @param size Must equal the parameter's size in PARAM_TABLE.
 * @return HAL_OK; HAL_ERROR for an unknown ID, a NULL buffer or a wrong size (the buffer is untouched).
 */
HAL_StatusTypeDef param_get(param_id_t id, void *value, size_t size);

/**
 * @brief Stores a new value through the OBDH task, waiting at most ticks_to_wait for the answer.
 * @details A value equal to the current one is not written. The record goes
 *          to copy A, is read back, then goes to copy B. A copy whose active
 *          half is full is compacted first (about 0.1 s per copy). Not for use
 *          by the OBDH task, which would wait for itself.
 * @param id Parameter.
 * @param value New value.
 * @param size Must equal the parameter's size in PARAM_TABLE.
 * @param ticks_to_wait Block time; normally OBDH_REQUEST_WAIT (obdh_requests.h).
 * @return HAL_OK if at least one copy holds the new value; HAL_ERROR if neither
 *         copy took it, or for an unknown ID, NULL value or wrong size (nothing
 *         sent); HAL_BUSY if the request could not be queued (nothing sent);
 *         HAL_TIMEOUT if no answer came in time. After HAL_ERROR or HAL_TIMEOUT
 *         the stored value is either the old or the new one.
 */
HAL_StatusTypeDef param_set_request(param_id_t id, const void *value, size_t size, TickType_t ticks_to_wait);

/**
 * @brief Returns a parameter to its default from PARAM_TABLE: param_set_request() with the default value.
 * @details Nothing is written when the value already equals the default.
 * @param id Parameter.
 * @param ticks_to_wait Block time; normally OBDH_REQUEST_WAIT (obdh_requests.h).
 * @return As param_set_request().
 */
HAL_StatusTypeDef param_reset_request(param_id_t id, TickType_t ticks_to_wait);

/**
 * @brief Returns the store's event counters since boot.
 */
param_store_stats_t param_store_stats(void);

/* ---- OBDH task, or main() before the scheduler starts: other callers get HAL_ERROR ---- */

/**
 * @brief Rebuilds the values in RAM by scanning both copies; reads flash only.
 * @details main() calls it before the scheduler starts, and the OBDH task each
 *          time it starts. A copy without a valid half header contributes no
 *          values and is rebuilt at the next write. The values are published in
 *          one step, under the lock param_get() uses.
 * @return HAL_OK; HAL_ERROR for another caller, with nothing read or changed.
 */
HAL_StatusTypeDef param_store_load(void);

/**
 * @brief Writes one parameter record to both copies. The OBDH task runs it for param_set_request() and param_reset_request().
 * @param id Parameter ID.
 * @param value The new value.
 * @param size The parameter's size.
 * @return HAL_OK if at least one copy holds the record or the value was
 *         already the current one; HAL_ERROR otherwise, and for another
 *         caller, with nothing written or changed.
 */
HAL_StatusTypeDef param_store_write(uint8_t id, const uint8_t *value, size_t size);

#endif /* INC_PARAM_STORE_H_ */
