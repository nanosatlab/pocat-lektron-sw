/**
 * @file beacon_period.c
 * @brief Beacon period: the limits, the stored value and the beacon timer (beacon_period.h).
 * @details
 * The period is stored as two bytes, little-endian, as PARAM_TABLE says.
 * The beacon task is the only task that touches the timer: other tasks store
 * a period and tell it. The timer was in beacon.c until step 5b of the flash
 * plan, which created a new one at every start of the beacon task, so each
 * restart by the health monitor added one more timer and more beacons.
 * @date 2026-11-24
 */

#include "beacon_period.h"
#include "param_store.h"
#include "notifications.h"
#include "task_management.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include <stdbool.h>

/* The default is what the timer runs with when the stored period is outside the limits; FreeRTOS refuses a period of 0 */
_Static_assert((BEACON_PERIOD_MIN_S > 0u) && (BEACON_PERIOD_MIN_S <= BEACON_PERIOD_DEFAULT_S) &&
               (BEACON_PERIOD_DEFAULT_S <= BEACON_PERIOD_MAX_S), "the default must be a period a telecommand may set");
_Static_assert((uint64_t)BEACON_PERIOD_MAX_S * configTICK_RATE_HZ <= (uint64_t)portMAX_DELAY,
               "the longest period must fit in ticks");

static TimerHandle_t timer = NULL;   /**< The beacon timer, created at the first start of the beacon task; it outlives a restarted task. */

static bool in_limits(uint16_t period_s);
static void timer_expired(TimerHandle_t expired);


beacon_period_result_t beacon_period_set_request(uint16_t period_s, TickType_t ticks_to_wait)
{
    if (!in_limits(period_s)) {
        return BEACON_PERIOD_OUT_OF_RANGE;
    }

    const uint8_t value[2] = { (uint8_t)period_s, (uint8_t)(period_s >> 8) };
    if (param_set_request(PARAM_BEACON_PERIOD, value, sizeof(value), ticks_to_wait) != HAL_OK) {
        return BEACON_PERIOD_NOT_STORED;
    }

    /* A beacon task that is not running applies the stored period when it starts */
    TaskHandle_t beacon = tm_get_task_handle(TM_TASK_BEACON);
    if (beacon != NULL) {
        xTaskNotify(beacon, N_COMMS_NEW_BEACON_PERIOD, eSetBits);
    }
    return BEACON_PERIOD_STORED;
}

uint16_t beacon_period_stored_s(void)
{
    uint8_t value[2];
    if (param_get(PARAM_BEACON_PERIOD, value, sizeof(value)) != HAL_OK) {
        return BEACON_PERIOD_DEFAULT_S;
    }

    uint16_t period_s = (uint16_t)(value[0] | ((uint16_t)value[1] << 8));
    return in_limits(period_s) ? period_s : BEACON_PERIOD_DEFAULT_S;
}

void beacon_period_apply(void)
{
    TickType_t period = (TickType_t)beacon_period_stored_s() * configTICK_RATE_HZ;

    if (timer == NULL) {
        timer = xTimerCreate("beacon", period, pdTRUE, NULL, timer_expired);   /* dormant until its period is set below */
    }
    if (timer != NULL) {
        (void)xTimerChangePeriod(timer, period, portMAX_DELAY);   /* starts a dormant timer, restarts a running one */
    }
}

/**
 * @brief Tells whether a telecommand may set this period.
 * @param period_s Period in seconds.
 * @return true from BEACON_PERIOD_MIN_S to BEACON_PERIOD_MAX_S.
 */
static bool in_limits(uint16_t period_s)
{
    return (period_s >= BEACON_PERIOD_MIN_S) && (period_s <= BEACON_PERIOD_MAX_S);
}

/**
 * @brief Timer callback, in the timer service task: tells the beacon task to transmit.
 * @param expired The beacon timer.
 */
static void timer_expired(TimerHandle_t expired)
{
    (void)expired;
    TaskHandle_t beacon = tm_get_task_handle(TM_TASK_BEACON);
    if (beacon != NULL) {
        xTaskNotify(beacon, N_COMMS_TRANSMIT_BEACON, eSetBits);
    }
}
