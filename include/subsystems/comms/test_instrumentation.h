/**
 * @file test_instrumentation.h
 * @brief Optional microsecond timing prints for the bench test campaign
 *        (see ir-report/test-campaign.md). Compiled out unless built with
 *        POCAT_TEST_INSTRUMENTATION (./build.sh --test-instr).
 *
 * Reads the free-running 1 MHz TIM5 counter (periph.c) directly; no extra
 * peripheral setup required. Output goes over the existing UART4 debug
 * console (log.c) and is meant to be captured by a serial terminal on the
 * bench, not parsed automatically.
 */
#ifndef INC_SUBSYSTEMS_COMMS_TEST_INSTRUMENTATION_H_
#define INC_SUBSYSTEMS_COMMS_TEST_INSTRUMENTATION_H_

#ifdef POCAT_TEST_INSTRUMENTATION

#include "periph.h"
#include <stdint.h>
#include <stdio.h>

static inline uint32_t tinstr_now_us(void)
{
    return __HAL_TIM_GET_COUNTER(&htim5);
}

#define TINSTR_US() tinstr_now_us()
#define TINSTR_LOG(fmt, ...) printf("[TINSTR] " fmt "\r\n", ##__VA_ARGS__)

#else

#include <stdio.h>

#define TINSTR_US() (0u)
/* sizeof() discards the printf call (no codegen, no actual output) while
 * still referencing fmt/args so disabled builds don't warn about timing
 * variables that are now otherwise unused. */
#define TINSTR_LOG(fmt, ...) ((void)sizeof(printf(fmt, ##__VA_ARGS__)))

#endif /* POCAT_TEST_INSTRUMENTATION */

#endif /* INC_SUBSYSTEMS_COMMS_TEST_INSTRUMENTATION_H_ */
