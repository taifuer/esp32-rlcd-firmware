#include "settings_power_policy.h"

#include <stddef.h>

bool app_power_policy_for_mode(app_power_mode_t mode,
                               app_power_policy_t *policy)
{
    if (policy == NULL) {
        return false;
    }

    if (mode == APP_POWER_MODE_NORMAL) {
        *policy = (app_power_policy_t){
            .show_seconds = true,
            .automatic_network = true,
            .rtc_read_interval_ms = 1000U,
            .sensor_read_interval_ms = 5000U,
            .battery_read_interval_ms = 30000U,
        };
        return true;
    }
    if (mode == APP_POWER_MODE_SAVING) {
        *policy = (app_power_policy_t){
            .show_seconds = false,
            .automatic_network = false,
            .rtc_read_interval_ms = 60000U,
            .sensor_read_interval_ms = 60000U,
            .battery_read_interval_ms = 300000U,
        };
        return true;
    }
    return false;
}

uint32_t app_power_policy_next_clock_delay_ms(
    const app_power_policy_t *policy, uint8_t current_second)
{
    if (policy == NULL || current_second > 59U) {
        return 0U;
    }
    if (policy->show_seconds) {
        return policy->rtc_read_interval_ms;
    }
    return (uint32_t)(60U - current_second) * 1000U;
}
