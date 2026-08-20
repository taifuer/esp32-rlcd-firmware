#include "calendar_month.h"

#include "chinese_lunar.h"

static const uint8_t MONTH_DAYS[] = {
    31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
};

static bool is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) ||
           (year % 400U) == 0U;
}

static uint8_t month_days(uint16_t year, uint8_t month)
{
    return month == 2U && is_leap_year(year) ? 29U : MONTH_DAYS[month - 1U];
}

bool calendar_month_info(uint16_t year, uint8_t month,
                         calendar_month_info_t *result)
{
    if (result == NULL ||
        year < CHINESE_LUNAR_GREGORIAN_FIRST_YEAR ||
        year > CHINESE_LUNAR_GREGORIAN_LAST_YEAR ||
        month < 1U || month > 12U) {
        return false;
    }

    /* 2000-01-01 was Saturday (6). */
    uint32_t elapsed_days = 0U;
    for (uint16_t current_year = CHINESE_LUNAR_GREGORIAN_FIRST_YEAR;
         current_year < year; ++current_year) {
        elapsed_days += is_leap_year(current_year) ? 366U : 365U;
    }
    for (uint8_t current_month = 1U; current_month < month; ++current_month) {
        elapsed_days += month_days(year, current_month);
    }

    *result = (calendar_month_info_t){
        .days = month_days(year, month),
        .first_weekday = (uint8_t)((6U + elapsed_days) % 7U),
    };
    return true;
}
