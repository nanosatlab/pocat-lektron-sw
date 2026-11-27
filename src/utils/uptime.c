/**
 * @file uptime.c
 * @brief Time since boot, from the kernel tick counter.
 * @date 2026-11-21
 */

#include "uptime.h"
#include "FreeRTOS.h"
#include "task.h"

_Static_assert(sizeof(TickType_t) == 4u, "uptime_get_s() joins the overflow count to a 32-bit tick count");

uint32_t uptime_get_s(void)
{
    /* vTaskSetTimeOutState() copies the tick count and the kernel's count of
     * its overflows together, in one critical section. */
    TimeOut_t now;
    vTaskSetTimeOutState(&now);

    uint64_t ticks = ((uint64_t)(uint32_t)now.xOverflowCount << 32) | (uint64_t)now.xTimeOnEntering;
    return (uint32_t)(ticks / (uint64_t)configTICK_RATE_HZ);
}
