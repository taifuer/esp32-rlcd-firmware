#include <assert.h>
#include <stdio.h>

#include "battery_level.h"

int main(void)
{
    assert(battery_level_from_voltage_mv(2500U) == 0U);
    assert(battery_level_from_voltage_mv(3300U) == 0U);
    assert(battery_level_from_voltage_mv(3400U) == 3U);
    assert(battery_level_from_voltage_mv(3750U) == 30U);
    assert(battery_level_from_voltage_mv(3825U) == 45U);
    assert(battery_level_from_voltage_mv(4050U) == 85U);
    assert(battery_level_from_voltage_mv(4200U) == 100U);
    assert(battery_level_from_voltage_mv(4500U) == 100U);

    puts("battery level tests passed");
    return 0;
}
