/**
 * @file time.c
 * @brief RTC time helpers — Unix timestamp get/set.
 * @author Jaume Cortés Grimalt
 * @date 2026-03-18
 *
 * Converts between the STM32 HAL RTC calendar representation and a 32-bit
 * Unix timestamp (seconds since 1970-01-01 00:00:00 UTC).
 *
 * The HAL RTC year register holds an offset from 2000, so year 0 = 2000.
 * All conversions assume UTC with no daylight-saving adjustments.
 */

#include "time.h"
#include "periph.h"
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>

/** @brief Mutex protecting RTC hardware access. */
static SemaphoreHandle_t time_mutex = NULL;

/** @brief Days from Jan 1 to the 1st of each month (non-leap year). */
static const uint16_t days_before_month[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

/** @brief Seconds in one day. */
#define SECS_PER_DAY  86400UL

/** @brief Seconds in one hour. */
#define SECS_PER_HOUR 3600UL

/** @brief Seconds in one minute. */
#define SECS_PER_MIN  60UL

/** @brief Unix timestamp at 2000-01-01 00:00:00 UTC. */
#define EPOCH_2000    946684800UL

/**
 * @brief Initialize time module (creates mutex for RTC access).
 */
void time_init(void)
{
    time_mutex = xSemaphoreCreateMutex();
    configASSERT(time_mutex);
}

/**
 * @brief Return 1 if @p year is a leap year, 0 otherwise.
 */
static int is_leap(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * @brief Days in a full year.
 */
static uint16_t days_in_year(uint16_t year)
{
    return is_leap(year) ? 366 : 365;
}

/**
 * @brief Convert a calendar date/time to seconds since 2000-01-01.
 * @param year  Absolute year (e.g. 2026).
 * @param month 1-12.
 * @param day   1-31.
 * @param h     Hours 0-23.
 * @param m     Minutes 0-59.
 * @param s     Seconds 0-59.
 */
static uint32_t calendar_to_secs2000(uint16_t year, uint8_t month,
                                     uint8_t day, uint8_t h,
                                     uint8_t m, uint8_t s)
{
    uint32_t days = 0;

    for (uint16_t y = 2000; y < year; y++) {
        days += days_in_year(y);
    }

    days += days_before_month[month - 1];
    if (month > 2 && is_leap(year)) {
        days++;
    }
    days += day - 1;

    return days * SECS_PER_DAY + h * SECS_PER_HOUR + m * SECS_PER_MIN + s;
}

/**
 * @brief Convert seconds since 2000-01-01 back to calendar components.
 */
static void secs2000_to_calendar(uint32_t secs, uint16_t *year,
                                 uint8_t *month, uint8_t *day,
                                 uint8_t *h, uint8_t *m, uint8_t *s)
{
    *s = secs % 60; secs /= 60;
    *m = secs % 60; secs /= 60;
    *h = secs % 24;
    uint32_t rem_days = secs / 24;

    uint16_t y = 2000;
    while (rem_days >= days_in_year(y)) {
        rem_days -= days_in_year(y);
        y++;
    }
    *year = y;

    uint8_t mon = 1;
    while (mon < 12) {
        uint16_t dim = days_before_month[mon] - days_before_month[mon - 1];
        if (mon == 2 && is_leap(y)) {
            dim++;
        }
        if (rem_days < dim) {
            break;
        }
        rem_days -= dim;
        mon++;
    }
    *month = mon;
    *day   = (uint8_t)(rem_days + 1);
}

uint32_t time_get_unix(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    if (time_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(time_mutex, portMAX_DELAY);
    }

    /* HAL requires reading time first, then date (latches shadow regs). */
    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

    uint16_t year = 2000 + d.Year;
    uint32_t result = EPOCH_2000 + calendar_to_secs2000(year, d.Month, d.Date,
                                                        t.Hours, t.Minutes, t.Seconds);

    if (time_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGive(time_mutex);
    }

    return result;
}

void time_set_unix(uint32_t epoch)
{
    if (time_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(time_mutex, portMAX_DELAY);
    }

    uint32_t secs2000 = epoch - EPOCH_2000;

    uint16_t year;
    uint8_t month, day, h, m, s;
    secs2000_to_calendar(secs2000, &year, &month, &day, &h, &m, &s);

    RTC_TimeTypeDef t = {0};
    t.Hours          = h;
    t.Minutes        = m;
    t.Seconds        = s;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);

    RTC_DateTypeDef d = {0};
    d.Year  = (uint8_t)(year - 2000);
    d.Month = month;
    d.Date  = day;
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);

    if (time_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGive(time_mutex);
    }
}

void time_print_epoch(uint32_t epoch)
{
    uint32_t secs2000 = epoch - EPOCH_2000;

    uint16_t year;
    uint8_t month, day, h, m, s;
    secs2000_to_calendar(secs2000, &year, &month, &day, &h, &m, &s);

    printf("%04u-%02u-%02u %02u:%02u:%02u \r\n",
           year, month, day, h, m, s);
}
