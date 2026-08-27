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

typedef enum {
    APP_POWER_STATE_NORMAL = 0,
    APP_POWER_STATE_SAVING,
} app_power_state_t;

/* Automatic battery saving is always active. A manual request can force the
 * saving state, but a detected USB data host temporarily holds the effective
 * state in NORMAL. Consecutive readings and a 5-point hysteresis keep ADC
 * noise from repeatedly switching near the threshold. */
typedef struct {
    bool manual_saving_requested;
    bool automatic_saving_active;
    bool pending_automatic_saving;
    app_power_state_t effective_state;
    uint8_t pending_samples;
    bool battery_observed;
    bool usb_data_host_connected;
} app_power_runtime_t;

bool app_power_runtime_init(app_power_runtime_t *runtime,
                            bool manual_saving_requested);
bool app_power_runtime_set_manual_saving_requested(
    app_power_runtime_t *runtime, bool manual_saving_requested,
    bool *effective_state_changed);
bool app_power_runtime_observe_battery(
    app_power_runtime_t *runtime, bool battery_valid,
    uint8_t battery_percent, bool *effective_state_changed);
/* The stock board has no VBUS or charger-status GPIO. This input therefore
 * means an enumerating USB data host, not every charger or power bank. */
bool app_power_runtime_observe_usb_data_host(
    app_power_runtime_t *runtime, bool connected,
    bool *effective_state_changed);
bool app_power_policy_for_runtime(const app_power_runtime_t *runtime,
                                  app_power_policy_t *policy);
uint32_t app_power_policy_next_clock_delay_ms(
    const app_power_policy_t *policy, uint8_t current_second);

#ifdef __cplusplus
}
#endif
