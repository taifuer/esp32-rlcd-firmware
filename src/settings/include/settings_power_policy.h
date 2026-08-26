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

#define APP_POWER_AUTO_ENTER_PERCENT 20U
#define APP_POWER_AUTO_EXIT_PERCENT 25U
#define APP_POWER_AUTO_CONFIRM_SAMPLES 2U

/* AUTO is a configured strategy; the effective mode is always NORMAL or
 * SAVING. Consecutive readings and a 5-point hysteresis keep ADC noise from
 * repeatedly switching near the threshold. A detected USB data host holds
 * AUTO in NORMAL until the data connection is no longer detected. */
typedef struct {
    app_power_mode_t configured_mode;
    app_power_mode_t effective_mode;
    app_power_mode_t pending_mode;
    uint8_t pending_samples;
    bool battery_observed;
    bool usb_data_host_connected;
} app_power_runtime_t;

bool app_power_runtime_init(app_power_runtime_t *runtime,
                            app_power_mode_t configured_mode);
bool app_power_runtime_set_configured(
    app_power_runtime_t *runtime, app_power_mode_t configured_mode,
    bool battery_valid, uint8_t battery_percent,
    bool *effective_mode_changed);
bool app_power_runtime_observe_battery(
    app_power_runtime_t *runtime, bool battery_valid,
    uint8_t battery_percent, bool *effective_mode_changed);
/* The stock board has no VBUS or charger-status GPIO. This input therefore
 * means an enumerating USB data host, not every charger or power bank. */
bool app_power_runtime_observe_usb_data_host(
    app_power_runtime_t *runtime, bool connected,
    bool *effective_mode_changed);
bool app_power_policy_for_runtime(const app_power_runtime_t *runtime,
                                  app_power_policy_t *policy);
uint32_t app_power_policy_next_clock_delay_ms(
    const app_power_policy_t *policy, uint8_t current_second);

#ifdef __cplusplus
}
#endif
