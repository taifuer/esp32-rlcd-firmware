#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clock_service_policy.h"

static void set_timezone(const char *timezone)
{
    assert(setenv("TZ", timezone, 1) == 0);
    tzset();
}

static void assert_datetime(const clock_service_datetime_t *datetime,
                            uint16_t year, uint8_t month, uint8_t day,
                            uint8_t weekday, uint8_t hour, uint8_t minute,
                            uint8_t second)
{
    assert(datetime->year == year);
    assert(datetime->month == month);
    assert(datetime->day == day);
    assert(datetime->weekday == weekday);
    assert(datetime->hour == hour);
    assert(datetime->minute == minute);
    assert(datetime->second == second);
}

int main(void)
{
    clock_service_datetime_t datetime = {0};

    set_timezone("UTC0");
    assert(clock_service_unix_to_local(INT64_C(946684800), &datetime));
    assert_datetime(&datetime, 2000U, 1U, 1U, 6U, 0U, 0U, 0U);
    assert(clock_service_unix_to_local(INT64_C(4102444799), &datetime));
    assert_datetime(&datetime, 2099U, 12U, 31U, 4U, 23U, 59U, 59U);
    assert(!clock_service_unix_to_local(INT64_C(946684799), &datetime));
    assert(!clock_service_unix_to_local(INT64_C(4102444800), &datetime));

    set_timezone("CST-8");
    assert(clock_service_unix_to_local(INT64_C(946656000), &datetime));
    assert_datetime(&datetime, 2000U, 1U, 1U, 6U, 0U, 0U, 0U);
    assert(clock_service_unix_to_local(INT64_C(1704067200), &datetime));
    assert_datetime(&datetime, 2024U, 1U, 1U, 1U, 8U, 0U, 0U);
    assert(!clock_service_unix_to_local(INT64_C(946655999), &datetime));

    memset(&datetime, 0, sizeof(datetime));
    assert(!clock_service_unix_to_local(INT64_MAX, &datetime));
    assert(!clock_service_unix_to_local(INT64_C(1704067200), NULL));

    puts("clock service policy tests passed");
    return 0;
}
