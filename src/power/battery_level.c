#include "battery_level.h"

#include <stddef.h>

typedef struct {
    uint16_t voltage_mv;
    uint8_t percent;
} battery_curve_point_t;

uint8_t battery_level_from_voltage_mv(uint16_t voltage_mv)
{
    /* Approximate resting-voltage curve for a protected single-cell 18650. */
    static const battery_curve_point_t CURVE[] = {
        {3300U, 0U},  {3500U, 5U},  {3600U, 10U}, {3700U, 20U},
        {3750U, 30U}, {3800U, 40U}, {3850U, 50U}, {3900U, 60U},
        {3950U, 70U}, {4000U, 80U}, {4100U, 90U}, {4200U, 100U},
    };

    if (voltage_mv <= CURVE[0].voltage_mv) {
        return CURVE[0].percent;
    }
    const size_t last = sizeof(CURVE) / sizeof(CURVE[0]) - 1U;
    if (voltage_mv >= CURVE[last].voltage_mv) {
        return CURVE[last].percent;
    }

    for (size_t index = 1U; index <= last; ++index) {
        if (voltage_mv <= CURVE[index].voltage_mv) {
            const battery_curve_point_t lower = CURVE[index - 1U];
            const battery_curve_point_t upper = CURVE[index];
            const uint32_t voltage_span = (uint32_t)upper.voltage_mv - lower.voltage_mv;
            const uint32_t percent_span = (uint32_t)upper.percent - lower.percent;
            const uint32_t position = (uint32_t)voltage_mv - lower.voltage_mv;
            return (uint8_t)(lower.percent +
                             (percent_span * position + voltage_span / 2U) / voltage_span);
        }
    }
    return 0U;
}
