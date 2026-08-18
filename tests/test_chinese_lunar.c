#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chinese_lunar.h"

static bool is_gregorian_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || (year % 400U) == 0U;
}

static uint8_t gregorian_month_days(uint16_t year, uint8_t month)
{
    static const uint8_t MONTH_DAYS[] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
    };
    return month == 2U && is_gregorian_leap_year(year) ? 29U : MONTH_DAYS[month - 1U];
}

static void test_entire_supported_range(void)
{
    size_t converted_dates = 0U;

    for (uint16_t year = CHINESE_LUNAR_GREGORIAN_FIRST_YEAR;
         year <= CHINESE_LUNAR_GREGORIAN_LAST_YEAR; ++year) {
        for (uint8_t month = 1U; month <= 12U; ++month) {
            const uint8_t days = gregorian_month_days(year, month);
            for (uint8_t day = 1U; day <= days; ++day) {
                chinese_lunar_date_t lunar = {0};
                assert(chinese_lunar_from_gregorian(year, month, day, &lunar));
                assert(lunar.year >= 1999U && lunar.year <= 2099U);
                assert(lunar.month >= 1U && lunar.month <= 12U);
                assert(lunar.day >= 1U && lunar.day <= 30U);
                ++converted_dates;
            }
        }
    }

    assert(converted_dates == 36525U);
}

static void expect_date(uint16_t gregorian_year, uint8_t gregorian_month,
                        uint8_t gregorian_day, uint16_t lunar_year, uint8_t lunar_month,
                        uint8_t lunar_day, bool leap_month, const char *formatted)
{
    chinese_lunar_date_t actual = {0};
    assert(chinese_lunar_from_gregorian(gregorian_year, gregorian_month, gregorian_day,
                                        &actual));
    assert(actual.year == lunar_year);
    assert(actual.month == lunar_month);
    assert(actual.day == lunar_day);
    assert(actual.leap_month == leap_month);

    char text[64];
    assert(chinese_lunar_format(&actual, text, sizeof(text)));
    assert(strcmp(text, formatted) == 0);
}

int main(void)
{
    test_entire_supported_range();

    /* Supported-range boundary and representative HKO conversion-table dates. */
    expect_date(2000U, 1U, 1U, 1999U, 11U, 25U, false, "十一月廿五");
    expect_date(2000U, 2U, 5U, 2000U, 1U, 1U, false, "正月初一");
    expect_date(2012U, 5U, 21U, 2012U, 4U, 1U, true, "闰四月初一");
    expect_date(2023U, 3U, 22U, 2023U, 2U, 1U, true, "闰二月初一");
    expect_date(2026U, 2U, 17U, 2026U, 1U, 1U, false, "正月初一");
    expect_date(2026U, 8U, 18U, 2026U, 7U, 6U, false, "七月初六");

    /* HKO's published resolution of three near-midnight future new moons. */
    expect_date(2057U, 9U, 27U, 2057U, 8U, 29U, false, "八月廿九");
    expect_date(2057U, 9U, 28U, 2057U, 9U, 1U, false, "九月初一");
    expect_date(2089U, 9U, 4U, 2089U, 8U, 1U, false, "八月初一");
    expect_date(2097U, 8U, 7U, 2097U, 7U, 1U, false, "七月初一");
    expect_date(2099U, 12U, 31U, 2099U, 11U, 20U, false, "十一月二十");

    chinese_lunar_date_t unused;
    assert(!chinese_lunar_from_gregorian(1999U, 12U, 31U, &unused));
    assert(!chinese_lunar_from_gregorian(2100U, 1U, 1U, &unused));
    assert(!chinese_lunar_from_gregorian(2026U, 2U, 29U, &unused));
    assert(!chinese_lunar_from_gregorian(2026U, 8U, 18U, NULL));

    char too_small[4] = "abc";
    const chinese_lunar_date_t date = {
        .year = 2026U,
        .month = 7U,
        .day = 6U,
        .leap_month = false,
    };
    assert(!chinese_lunar_format(&date, too_small, sizeof(too_small)));
    assert(too_small[0] == '\0');

    puts("chinese lunar tests passed");
    return 0;
}
