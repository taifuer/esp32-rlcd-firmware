#include "clock_service_policy.h"

#include <limits.h>
#include <stddef.h>
#include <time.h>

static bool leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) ||
           (year % 400U) == 0U;
}

static uint8_t month_days(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (month == 0U || month > 12U) {
        return 0U;
    }
    return month == 2U && leap_year(year)
               ? (uint8_t)(days[month - 1U] + 1U)
               : days[month - 1U];
}

bool clock_service_unix_to_local(int64_t unix_seconds,
                                 clock_service_datetime_t *datetime)
{
    if (datetime == NULL) {
        return false;
    }

    const time_t value = (time_t)unix_seconds;
    if ((int64_t)value != unix_seconds) {
        return false;
    }

    struct tm local = {0};
    if (localtime_r(&value, &local) == NULL) {
        return false;
    }

    const int year = local.tm_year + 1900;
    const int month = local.tm_mon + 1;
    if (year < 2000 || year > 2099 || month < 1 || month > 12 ||
        local.tm_mday < 1 ||
        local.tm_mday > month_days((uint16_t)year, (uint8_t)month) ||
        local.tm_wday < 0 || local.tm_wday > 6 || local.tm_hour < 0 ||
        local.tm_hour > 23 || local.tm_min < 0 || local.tm_min > 59 ||
        local.tm_sec < 0 || local.tm_sec > 59) {
        return false;
    }

    *datetime = (clock_service_datetime_t){
        .year = (uint16_t)year,
        .month = (uint8_t)month,
        .day = (uint8_t)local.tm_mday,
        .weekday = (uint8_t)local.tm_wday,
        .hour = (uint8_t)local.tm_hour,
        .minute = (uint8_t)local.tm_min,
        .second = (uint8_t)local.tm_sec,
    };
    return true;
}
