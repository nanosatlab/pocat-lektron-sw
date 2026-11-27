/**
 * @file beacon_period.h
 * @brief Beacon period: the limits a telecommand must respect, the stored value, and the timer that runs with it.
 * @details
 * The period is the parameter PARAM_BEACON_PERIOD (param_store.h), in
 * seconds. TC_SET_BEACON_PERIOD sets it with beacon_period_set_request(): a
 * period outside BEACON_PERIOD_MIN_S to BEACON_PERIOD_MAX_S is refused, and a
 * period takes effect only once it is stored. The beacon task runs its timer
 * with the stored period, through beacon_period_apply(), when it starts and
 * when told that a new one is stored (docs/FLASH-USAGE-SPECIFICATION.md
 * FLS-RATE-010).
 * @date 2026-11-24
 */

#ifndef INC_BEACON_PERIOD_H_
#define INC_BEACON_PERIOD_H_

#include <stdint.h>

#include "FreeRTOS.h"

#define BEACON_PERIOD_MIN_S       10u   /**< Shortest period a telecommand may set (FLS-RATE-010, TBC). */
#define BEACON_PERIOD_MAX_S     3600u   /**< Longest period a telecommand may set (FLS-RATE-010, TBC). */
#define BEACON_PERIOD_DEFAULT_S   30u   /**< The default of PARAM_BEACON_PERIOD in PARAM_TABLE; test_beacon_period checks that they agree. */

/** @brief What became of a new beacon period. */
typedef enum {
    BEACON_PERIOD_STORED = 0,     /**< Stored; the beacon task was told to apply it. */
    BEACON_PERIOD_OUT_OF_RANGE,   /**< Outside the limits: nothing stored, nothing changed. */
    BEACON_PERIOD_NOT_STORED,     /**< The store did not take it within the block time: the beacon keeps its period. */
} beacon_period_result_t;

/**
 * @brief Stores a new beacon period, then tells the beacon task to apply it. The COMMS task calls it for TC_SET_BEACON_PERIOD.
 * @details A request (spec §5.2.1): it waits at most ticks_to_wait for the
 *          parameter store. A period equal to the stored one is not written
 *          again but is applied again, so the ground's repeat of a
 *          telecommand whose answer it missed takes effect. If OBDH stores
 *          the period after the block time has run out, it takes effect at
 *          the beacon task's next start.
 * @param period_s The new period, in seconds.
 * @param ticks_to_wait Block time; normally OBDH_REQUEST_WAIT (obdh_requests.h).
 * @return What became of it.
 */
beacon_period_result_t beacon_period_set_request(uint16_t period_s, TickType_t ticks_to_wait);

/**
 * @brief Returns the stored beacon period in seconds, or the default if the stored one is outside the limits. Any task (plain function).
 * @details The owner's check of a stored value (spec §5.4): only a build with
 *          other limits, or a fault, can leave one outside them.
 */
uint16_t beacon_period_stored_s(void);

/**
 * @brief Runs the beacon timer with the stored period: creates the timer the first time, and otherwise changes its period.
 * @details Only the beacon task calls it: when it starts, also after a restart
 *          by the health monitor (the timer outlives the task), and when told
 *          that a new period is stored. Changing the period restarts the
 *          timer: the next beacon comes one new period later.
 */
void beacon_period_apply(void);

#endif /* INC_BEACON_PERIOD_H_ */
