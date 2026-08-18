#include "chinese_lunar.h"

#include <stdio.h>

#define LUNAR_DATA_FIRST_YEAR 1999U
#define LUNAR_DATA_LAST_YEAR 2099U
#define BASE_GREGORIAN_YEAR 1999U
#define BASE_GREGORIAN_MONTH 2U
#define BASE_GREGORIAN_DAY 16U

/*
 * One 17-bit value describes each lunar year. Bits 15..4 contain the length
 * of regular months 1..12 (1 means 30 days, 0 means 29), bits 3..0 contain
 * the leap-month number, and bit 16 contains the leap-month length.
 *
 * The factual month boundaries follow the Hong Kong Observatory's published
 * Gregorian-Lunar conversion tables. The table deliberately uses HKO's
 * published choice for the near-midnight new moon on 2057-09-28: that date is
 * lunar month 9, day 1.
 */
static const uint32_t LUNAR_YEAR_INFO[] = {
    0x092e0U, 0x0c960U, 0x0d954U, 0x0d4a0U, 0x0da50U, 0x07552U, 0x056a0U, 0x0abb7U,
    0x025d0U, 0x092d0U, 0x0cab5U, 0x0a950U, 0x0b4a0U, 0x0bca4U, 0x0ad50U, 0x055d9U,
    0x04ba0U, 0x0a5b0U, 0x15176U, 0x05270U, 0x0a930U, 0x07954U, 0x06aa0U, 0x0ad50U,
    0x05b52U, 0x04b60U, 0x0a6e6U, 0x0a4f0U, 0x05260U, 0x0ea65U, 0x0d520U, 0x0daa0U,
    0x076a3U, 0x096d0U, 0x04afbU, 0x04ad0U, 0x0a4d0U, 0x1d0b6U, 0x0d250U, 0x0d520U,
    0x0dd45U, 0x0b5a0U, 0x056d0U, 0x055b2U, 0x049b0U, 0x0a577U, 0x0a4b0U, 0x0aa50U,
    0x1b255U, 0x06d20U, 0x0ada0U, 0x14b63U, 0x09370U, 0x049f8U, 0x04970U, 0x064b0U,
    0x168a6U, 0x0ea50U, 0x06aa0U, 0x1a6c4U, 0x0aae0U, 0x092e0U, 0x0d2e3U, 0x0c960U,
    0x0d557U, 0x0d4a0U, 0x0da50U, 0x05d55U, 0x056a0U, 0x0a6d0U, 0x055d4U, 0x092d0U,
    0x0a9b8U, 0x0a950U, 0x0b4a0U, 0x0b6a6U, 0x0ad50U, 0x055a0U, 0x0aba4U, 0x0a5b0U,
    0x052b0U, 0x0b273U, 0x06930U, 0x07337U, 0x06aa0U, 0x0ad50U, 0x14b55U, 0x04b60U,
    0x0a570U, 0x054e4U, 0x0d160U, 0x0e968U, 0x0d520U, 0x0daa0U, 0x16aa6U, 0x056d0U,
    0x04ae0U, 0x0a9d4U, 0x0a2d0U, 0x0d150U, 0x0f252U,
};

_Static_assert(sizeof(LUNAR_YEAR_INFO) / sizeof(LUNAR_YEAR_INFO[0]) ==
                   LUNAR_DATA_LAST_YEAR - LUNAR_DATA_FIRST_YEAR + 1U,
               "lunar year table must cover the declared range");

static const uint8_t GREGORIAN_MONTH_DAYS[] = {
    31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
};

static bool is_gregorian_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || (year % 400U) == 0U;
}

static uint8_t gregorian_month_days(uint16_t year, uint8_t month)
{
    if (month == 2U && is_gregorian_leap_year(year)) {
        return 29U;
    }
    return GREGORIAN_MONTH_DAYS[month - 1U];
}

static bool gregorian_date_valid(uint16_t year, uint8_t month, uint8_t day)
{
    return year >= CHINESE_LUNAR_GREGORIAN_FIRST_YEAR &&
           year <= CHINESE_LUNAR_GREGORIAN_LAST_YEAR && month >= 1U && month <= 12U &&
           day >= 1U && day <= gregorian_month_days(year, month);
}

static int32_t gregorian_ordinal(uint16_t year, uint8_t month, uint8_t day)
{
    int32_t ordinal = 0;
    for (uint16_t current_year = BASE_GREGORIAN_YEAR; current_year < year; ++current_year) {
        ordinal += is_gregorian_leap_year(current_year) ? 366 : 365;
    }
    for (uint8_t current_month = 1U; current_month < month; ++current_month) {
        ordinal += gregorian_month_days(year, current_month);
    }
    return ordinal + (int32_t)day - 1;
}

static uint32_t lunar_year_info(uint16_t year)
{
    return LUNAR_YEAR_INFO[year - LUNAR_DATA_FIRST_YEAR];
}

static uint8_t lunar_regular_month_days(uint32_t info, uint8_t month)
{
    return (info & (1U << (16U - month))) != 0U ? 30U : 29U;
}

static uint8_t lunar_leap_month(uint32_t info)
{
    return (uint8_t)(info & 0x0fU);
}

static uint8_t lunar_leap_month_days(uint32_t info)
{
    return (info & 0x10000U) != 0U ? 30U : 29U;
}

static uint16_t lunar_year_days(uint32_t info)
{
    uint16_t days = 0U;
    for (uint8_t month = 1U; month <= 12U; ++month) {
        days += lunar_regular_month_days(info, month);
    }
    if (lunar_leap_month(info) != 0U) {
        days += lunar_leap_month_days(info);
    }
    return days;
}

bool chinese_lunar_from_gregorian(uint16_t year, uint8_t month, uint8_t day,
                                  chinese_lunar_date_t *result)
{
    if (result == NULL || !gregorian_date_valid(year, month, day)) {
        return false;
    }

    const int32_t base_ordinal = gregorian_ordinal(BASE_GREGORIAN_YEAR,
                                                    BASE_GREGORIAN_MONTH,
                                                    BASE_GREGORIAN_DAY);
    int32_t remaining_days = gregorian_ordinal(year, month, day) - base_ordinal;
    uint16_t lunar_year = LUNAR_DATA_FIRST_YEAR;

    while (lunar_year <= LUNAR_DATA_LAST_YEAR) {
        const uint16_t year_days = lunar_year_days(lunar_year_info(lunar_year));
        if (remaining_days < (int32_t)year_days) {
            break;
        }
        remaining_days -= year_days;
        ++lunar_year;
    }
    if (lunar_year > LUNAR_DATA_LAST_YEAR) {
        return false;
    }

    const uint32_t info = lunar_year_info(lunar_year);
    const uint8_t leap_month = lunar_leap_month(info);
    for (uint8_t lunar_month = 1U; lunar_month <= 12U; ++lunar_month) {
        const uint8_t regular_days = lunar_regular_month_days(info, lunar_month);
        if (remaining_days < (int32_t)regular_days) {
            *result = (chinese_lunar_date_t){
                .year = lunar_year,
                .month = lunar_month,
                .day = (uint8_t)(remaining_days + 1),
                .leap_month = false,
            };
            return true;
        }
        remaining_days -= regular_days;

        if (leap_month == lunar_month) {
            const uint8_t leap_days = lunar_leap_month_days(info);
            if (remaining_days < (int32_t)leap_days) {
                *result = (chinese_lunar_date_t){
                    .year = lunar_year,
                    .month = lunar_month,
                    .day = (uint8_t)(remaining_days + 1),
                    .leap_month = true,
                };
                return true;
            }
            remaining_days -= leap_days;
        }
    }
    return false;
}

bool chinese_lunar_format(const chinese_lunar_date_t *date, char *buffer, size_t buffer_size)
{
    static const char *const MONTH_NAMES[] = {
        "正月", "二月", "三月", "四月", "五月", "六月",
        "七月", "八月", "九月", "十月", "十一月", "十二月",
    };
    static const char *const DAY_NAMES[] = {
        "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
        "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
        "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
    };

    if (date == NULL || buffer == NULL || buffer_size == 0U || date->month < 1U ||
        date->month > 12U || date->day < 1U || date->day > 30U) {
        return false;
    }

    const int written = snprintf(buffer, buffer_size, "%s%s%s",
                                 date->leap_month ? "闰" : "",
                                 MONTH_NAMES[date->month - 1U], DAY_NAMES[date->day - 1U]);
    if (written < 0 || (size_t)written >= buffer_size) {
        buffer[0] = '\0';
        return false;
    }
    return true;
}
