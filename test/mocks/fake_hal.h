/**
 * @file fake_hal.h
 * @brief Peripheral handle variables and a counted `HAL_IWDG_Refresh()` for host tests.
 * @date 2026-10-23
 *
 * @details
 * periph.h declares the peripheral handles as `extern` (`hiwdg`, `huart4`,
 * `hspi2`, `htim2`, `htim5`, `hrtc`, `hadc1`). On the target they are defined
 * in periph.c, which can't be compiled on a PC, so fake_hal.c defines them
 * instead. Their contents are dummies (see stubs_hal/stm32l4xx_hal.h); only
 * their addresses matter.
 *
 * It also implements `HAL_IWDG_Refresh()` and counts the calls, so a test can
 * check when the health monitor refreshes the hardware watchdog.
 *
 * Linked into every suite unless it uses `NO_FAKES` in test/CMakeLists.txt.
 */
#ifndef FAKE_HAL_H
#define FAKE_HAL_H
#include "stm32l4xx_hal.h"
#include "periph.h"

/** @brief Sets the IWDG refresh counter to 0. Call it in `setUp()`. */
void fake_hal_reset(void);

/** @brief Returns the number of `HAL_IWDG_Refresh()` calls since the last reset. */
int  fake_hal_iwdg_refresh_count(void);

#endif
