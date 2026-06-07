/**
 * @file health.c
 * @brief Software watchdog implementation for subsystem health monitoring.
 * @author Jaume Cortés Grimalt
 * @version 0.1
 * @date 2026-01-26
 *
 * This module uses a FreeRTOS event group as a software watchdog mechanism.
 * Subsystem tasks set their corresponding bit via health_kick(). The OBC
 * periodically calls health_check() which, after the configured period, checks
 * which expected bits are missing and clears all bits for the next period.
 */

#include "health.h"
#include "log.h"
#include "stm32l4xx_hal.h"

/** @brief Event group handle for health bit tracking. */
static EventGroupHandle_t health_eg = NULL;

/** @brief Pointer to the IWDG handle for watchdog refresh. */
static IWDG_HandleTypeDef *iwdg_handle = NULL;

/** @brief Bitmask of subsystems expected to kick each period. */
static EventBits_t expected_bits = 0;

/** @brief Health check period in ticks. */
static TickType_t period_ticks = 0;

/** @brief Tick count when current period expires. */
static TickType_t next_deadline = 0;

static inline void lock(void);
static inline void unlock(void);
static void start_new_period(TickType_t now);
static EventBits_t system_health(BaseType_t *period_elapsed);

void health_init(void)
{
    if (health_eg == NULL)
    {
        health_eg = xEventGroupCreate();
    }
}

void health_kick(EventBits_t bit)
{
    if (health_eg != NULL)
    {
        xEventGroupSetBits(health_eg, bit);
    }
}

void health_config(TickType_t period)
{
    if (period == 0) period = 1;

    lock();
    period_ticks = period;
    unlock();

    start_new_period(xTaskGetTickCount());
}

void health_set_expected(EventBits_t exp_bits)
{
    lock();
    expected_bits = exp_bits;
    unlock();

    /* Restart window when mode expectations change */
    start_new_period(xTaskGetTickCount());
}

EventBits_t health_get_expected(void)
{
    lock();
    EventBits_t exp = expected_bits;
    unlock();
    return exp;
}

void health_register_iwdg(void *hiwdg)
{
    iwdg_handle = (IWDG_HandleTypeDef *)hiwdg;
}

EventBits_t health_check(void)
{
    BaseType_t period_elapsed;
    EventBits_t faults = system_health(&period_elapsed);

    // Only refresh IWDG after an actual health check passed (not during wait)
    if (period_elapsed && faults == 0 && iwdg_handle != NULL)
    {
        //printf("Health check OK, refreshing IWDG\r\n");
        HAL_IWDG_Refresh(iwdg_handle);
    }

    return faults;
}

/**
 * @brief Check if a deadline has been reached (handles tick overflow).
 * @param now Current tick count.
 * @param target Target tick count.
 * @return pdTRUE if now >= target (with overflow handling).
 */
static inline BaseType_t time_reached(TickType_t now, TickType_t target)
{
    return ((int32_t)(now - target) >= 0);
}

/** @brief Enter a critical section while accessing health module state. */
static inline void lock(void)   { taskENTER_CRITICAL(); }

/** @brief Exit the critical section entered by lock(). */
static inline void unlock(void) { taskEXIT_CRITICAL();  }

/**
 * @brief Start a new health check period.
 *
 * Clears all expected bits in the event group and sets the next deadline.
 *
 * @param now Current tick count.
 */
static void start_new_period(TickType_t now)
{
    lock();
    EventBits_t expected = expected_bits;
    TickType_t period    = period_ticks;
    unlock();

    if (expected != 0 && health_eg != NULL)
    {
        xEventGroupClearBits(health_eg, expected);
    }

    next_deadline = now + period;
}

/**
 * @brief Get faulty subsystems.
 *
 * When the health check period has elapsed, returns which expected subsystems
 * failed to kick and starts a new health period.
 *
 * @param period_elapsed Output parameter set to pdTRUE if the health check
 *                       period elapsed and a check was performed, pdFALSE
 *                       if still waiting. Can be NULL if not needed.
 * @return Bitmask of faulty subsystems. Returns 0 if the check period has not
 *         elapsed yet or if all expected subsystems have kicked.
 */
static EventBits_t system_health(BaseType_t *period_elapsed)
{
    TickType_t now = xTaskGetTickCount();

    if (time_reached(now, next_deadline))
    {
        if (period_elapsed != NULL)
        {
            *period_elapsed = pdTRUE;
        }

        lock();
        EventBits_t expected = expected_bits;
        unlock();

        EventBits_t got = xEventGroupGetBits(health_eg);

        EventBits_t missing = expected & ~got;

        start_new_period(now);

        return missing;
    }

    if (period_elapsed != NULL)
    {
        *period_elapsed = pdFALSE;
    }

    return 0;
}
