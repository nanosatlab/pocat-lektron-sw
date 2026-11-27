/**
 * @file uptime.h
 * @brief Time since boot, from the kernel tick counter.
 * @details
 * The uptime counts the kernel's ticks together with the kernel's count of
 * tick overflows, so it does not wrap when the 32-bit tick count does (every
 * 49.7 days at 1 kHz), and it does not jump when the RTC is set. It lives in
 * RAM only: nothing that changes at every boot is stored in flash (spec
 * FLS-BOOT-040, F3).
 * @date 2026-11-21
 */

#ifndef UTILS_UPTIME_H
#define UTILS_UPTIME_H

#include <stdint.h>

/**
 * @brief Returns the whole seconds since the scheduler started. Any task (plain function); not from an interrupt.
 */
uint32_t uptime_get_s(void);

#endif /* UTILS_UPTIME_H */
