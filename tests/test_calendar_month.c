#include <assert.h>
#include <stdio.h>

#include "calendar_month.h"

static void expect_month(uint16_t year, uint8_t month, uint8_t days,
                         uint8_t first_weekday)
{
    calendar_month_info_t actual = {0};
    assert(calendar_month_info(year, month, &actual));
    assert(actual.days == days);
    assert(actual.first_weekday == first_weekday);
}

int main(void)
{
    expect_month(2000U, 1U, 31U, 6U);
    expect_month(2024U, 2U, 29U, 4U);
    expect_month(2026U, 8U, 31U, 6U);
    expect_month(2099U, 12U, 31U, 2U);

    calendar_month_info_t unused = {0};
    assert(!calendar_month_info(1999U, 12U, &unused));
    assert(!calendar_month_info(2100U, 1U, &unused));
    assert(!calendar_month_info(2026U, 0U, &unused));
    assert(!calendar_month_info(2026U, 13U, &unused));
    assert(!calendar_month_info(2026U, 8U, NULL));

    puts("calendar month tests passed");
    return 0;
}
