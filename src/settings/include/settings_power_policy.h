#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool show_seconds;
    bool automatic_network;
    uint32_t rtc_read_interval_ms;
    uint32_t sensor_read_interval_ms;
    uint32_t battery_read_interval_ms;
} app_power_policy_t;

bool app_power_policy_for_mode(app_power_mode_t mode,
                               app_power_policy_t *policy);
uint32_t app_power_policy_next_clock_delay_ms(
    const app_power_policy_t *policy, uint8_t current_second);

#ifdef __cplusplus
}
#endif
