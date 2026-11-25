/**
 * @file param_store.h
 * @brief Parameter store: persistent values by ID, in two copies with a CRC on every record.
 * @details
 * Every value that must survive a reset or a power cut (mode, flags,
 * configuration, TLE, calibration) is a parameter with a fixed ID and size,
 * listed in PARAM_TABLE. Any task reads a value with param_get(), which copies
 * it from RAM and never touches flash, and changes it with param_set() or
 * param_reset(), which go through the OBDH task.
 *
 * A value is the newest one written in flight, or the default from
 * PARAM_TABLE when none was written (or after param_reset()). The layout in
 * flash is in docs/FLASH-USAGE-SPECIFICATION.md §5.4.
 * @date 2026-10-27
 */

#ifndef INC_PARAM_STORE_H_
#define INC_PARAM_STORE_H_

#include <stdint.h>
#include <stddef.h>

#include "stm32l4xx_hal.h"

/**
 * @brief Every parameter, as X(id, name, size, default bytes...) (spec §6).
 * @details
 * - Each subsystem picks IDs inside its own range of 32: OBC 0x01, EPS 0x20,
 *   COMMS 0x40, ADCS 0x60, Payload 0x80, FSS 0xA0, OBDH 0xC0. An ID is never
 *   renumbered or reused once it has flown; a new layout gets a new ID.
 * - The size is fixed, 1 to 255 bytes. Before flight, changing a size only
 *   makes the records already stored for that ID be ignored.
 * - The default gives the first bytes; the rest are zero. "TBD" defaults are
 *   set with the owner when the parameter moves to the store (step 3).
 */
#define PARAM_TABLE(X) \
    X(0x01u, PARAM_OBC_MODE,          2u, 0x00u, 0x00u) /* current and previous mode: OBC_STATE_BOOT */ \
    X(0x02u, PARAM_DEPLOY_COMMS,      1u, 0x00u)        /* not deployed */ \
    X(0x03u, PARAM_DEPLOY_PAYLOAD,    1u, 0x00u)        /* not deployed */ \
    X(0x04u, PARAM_POL_MASK,          1u, 0x00u)        /* TBD (spec: all enabled) */ \
    X(0x05u, PARAM_STANDBY_DONE,      1u, 0x00u)        /* not done */ \
    X(0x20u, PARAM_EPS_THRESHOLDS,    4u, 0x00u)        /* TBD; size TBC */ \
    X(0x21u, PARAM_HEATER_AUTO,       1u, 0x00u)        /* TBD */ \
    X(0x40u, PARAM_COMMS_CONFIG,     21u, 0x00u)        /* TBD (spec: built-in LoRa settings); size TBC */ \
    X(0x41u, PARAM_TX_INHIBIT,        1u, 0x00u)        /* off */ \
    X(0x42u, PARAM_BEACON_PERIOD,     2u, 30u, 0x00u)   /* 30 s, uint16_t little-endian */ \
    X(0x60u, PARAM_ADCS_CALIBRATION, 81u, 0x00u)        /* TBD (spec: identity / zero); size TBC */ \
    X(0x61u, PARAM_TLE,             138u, 0x00u)        /* none; size TBC */ \
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

/** @brief Where a value from param_get() comes from. */
typedef enum {
    PARAM_FROM_FLASH,     /**< A value written in flight that differs from the default. */
    PARAM_FROM_DEFAULT,   /**< The default from PARAM_TABLE: never changed, or reset. */
    PARAM_INVALID         /**< Unknown ID or wrong size; the buffer is untouched. */
} param_source_t;

/* ---- Any task ---- */

/**
 * @brief Copies the current value of a parameter, from RAM. Any task, any time.
 * @param id Parameter.
 * @param value Destination buffer.
 * @param size Must equal the parameter's size in PARAM_TABLE.
 * @return Where the value comes from; PARAM_INVALID for an unknown ID or a wrong size.
 */
param_source_t param_get(param_id_t id, void *value, size_t size);

/**
 * @brief Stores a new value; blocks until the OBDH task has written it.
 * @details A value equal to the current one is not written. The record
 *          goes to copy A, is read back, then goes to copy B. When a copy's
 *          active half is full, the same call compacts that copy (about 0.1 s).
 * @param id Parameter.
 * @param value New value.
 * @param size Must equal the parameter's size in PARAM_TABLE.
 * @return HAL_OK if at least one copy holds the new value (it survives a
 *         reset); HAL_ERROR if neither does, or for an unknown ID, NULL value
 *         or wrong size; HAL_BUSY if the OBDH request queue stayed full.
 */
HAL_StatusTypeDef param_set(param_id_t id, const void *value, size_t size);

/**
 * @brief Returns a parameter to its default from PARAM_TABLE: param_set() with the default value.
 * @param id Parameter.
 * @return As param_set().
 */
HAL_StatusTypeDef param_reset(param_id_t id);

/**
 * @brief Returns the store's event counters since boot.
 */
param_store_stats_t param_store_stats(void);

/* ---- Boot and OBDH task only ---- */

/**
 * @brief Rebuilds the values in RAM by scanning both copies; reads flash only.
 * @details main() calls it once, before the scheduler starts. It never programs
 *          or erases (spec FLS-BOOT-010): a blank, foreign or damaged copy
 *          gives the defaults, and is repaired at the next write.
 */
void param_store_init(void);

/**
 * @brief Writes one parameter record to both copies. The OBDH task runs it for param_set() and param_reset().
 * @param id Parameter ID.
 * @param value The new value.
 * @param size The parameter's size.
 * @return HAL_OK if at least one copy holds the record or the value was
 *         already the current one; HAL_ERROR otherwise.
 */
HAL_StatusTypeDef param_store_write(uint8_t id, const uint8_t *value, size_t size);

#endif /* INC_PARAM_STORE_H_ */
