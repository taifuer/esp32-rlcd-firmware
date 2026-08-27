#include "settings_power_policy.h"

#include <stddef.h>

static bool power_state_is_valid(app_power_state_t state)
{
    return state == APP_POWER_STATE_NORMAL ||
           state == APP_POWER_STATE_SAVING;
}

static app_power_state_t requested_effective_state(
    const app_power_runtime_t *runtime)
{
    if (runtime->usb_data_host_connected) {
        return APP_POWER_STATE_NORMAL;
    }
    return runtime->manual_saving_requested ||
                   runtime->automatic_saving_active
               ? APP_POWER_STATE_SAVING
               : APP_POWER_STATE_NORMAL;
}

static bool policy_for_effective_state(app_power_state_t state,
                                       app_power_policy_t *policy)
{
    if (policy == NULL) {
        return false;
    }

    if (state == APP_POWER_STATE_NORMAL) {
        *policy = (app_power_policy_t){
            .show_seconds = true,
            .automatic_network = true,
            .rtc_read_interval_ms = 1000U,
            .sensor_read_interval_ms = 5000U,
            .battery_read_interval_ms = 30000U,
        };
        return true;
    }
    if (state == APP_POWER_STATE_SAVING) {
        *policy = (app_power_policy_t){
            .show_seconds = false,
            .automatic_network = false,
            .rtc_read_interval_ms = 60000U,
            .sensor_read_interval_ms = 60000U,
            .battery_read_interval_ms = 10000U,
        };
        return true;
    }
    return false;
}

static void reset_pending(app_power_runtime_t *runtime)
{
    runtime->pending_automatic_saving =
        runtime->automatic_saving_active;
    runtime->pending_samples = 0U;
}

bool app_power_runtime_init(app_power_runtime_t *runtime,
                            bool manual_saving_requested)
{
    if (runtime == NULL) {
        return false;
    }
    *runtime = (app_power_runtime_t){
        .manual_saving_requested = manual_saving_requested,
        .effective_state = manual_saving_requested
                               ? APP_POWER_STATE_SAVING
                               : APP_POWER_STATE_NORMAL,
    };
    return true;
}

bool app_power_runtime_set_manual_saving_requested(
    app_power_runtime_t *runtime, bool manual_saving_requested,
    bool *effective_state_changed)
{
    if (runtime == NULL || effective_state_changed == NULL ||
        !power_state_is_valid(runtime->effective_state)) {
        return false;
    }

    if (manual_saving_requested ==
        runtime->manual_saving_requested) {
        *effective_state_changed = false;
        return true;
    }

    const app_power_state_t previous = runtime->effective_state;
    runtime->manual_saving_requested = manual_saving_requested;
    reset_pending(runtime);
    runtime->effective_state = requested_effective_state(runtime);
    *effective_state_changed = runtime->effective_state != previous;
    return true;
}

bool app_power_runtime_observe_battery(
    app_power_runtime_t *runtime, bool battery_valid,
    uint8_t battery_percent, bool *effective_state_changed)
{
    if (runtime == NULL || effective_state_changed == NULL ||
        !power_state_is_valid(runtime->effective_state) ||
        (battery_valid && battery_percent > 100U)) {
        return false;
    }
    *effective_state_changed = false;

    if (!battery_valid) {
        reset_pending(runtime);
        return true;
    }

    if (runtime->usb_data_host_connected) {
        runtime->battery_observed = true;
        runtime->automatic_saving_active = false;
        reset_pending(runtime);
        return true;
    }

    if (!runtime->battery_observed) {
        runtime->battery_observed = true;
        const app_power_state_t previous = runtime->effective_state;
        /* A fresh boot has no previous effective state, so it uses the entry
         * threshold. Hysteresis starts after this first observation. */
        if (battery_percent <= APP_POWER_AUTO_ENTER_PERCENT) {
            runtime->automatic_saving_active = true;
        } else {
            runtime->automatic_saving_active = false;
        }
        runtime->effective_state = requested_effective_state(runtime);
        reset_pending(runtime);
        *effective_state_changed = runtime->effective_state != previous;
        return true;
    }

    bool requested = runtime->automatic_saving_active;
    if (!runtime->automatic_saving_active &&
        battery_percent <= APP_POWER_AUTO_ENTER_PERCENT) {
        requested = true;
    } else if (runtime->automatic_saving_active &&
               battery_percent >= APP_POWER_AUTO_EXIT_PERCENT) {
        requested = false;
    }

    if (requested == runtime->automatic_saving_active) {
        reset_pending(runtime);
        return true;
    }
    if (runtime->pending_automatic_saving != requested) {
        runtime->pending_automatic_saving = requested;
        runtime->pending_samples = 1U;
        return true;
    }
    if (runtime->pending_samples < UINT8_MAX) {
        runtime->pending_samples++;
    }
    if (runtime->pending_samples >= APP_POWER_AUTO_CONFIRM_SAMPLES) {
        const app_power_state_t previous = runtime->effective_state;
        runtime->automatic_saving_active = requested;
        runtime->effective_state = requested_effective_state(runtime);
        reset_pending(runtime);
        *effective_state_changed = runtime->effective_state != previous;
    }
    return true;
}

bool app_power_runtime_observe_usb_data_host(
    app_power_runtime_t *runtime, bool connected,
    bool *effective_state_changed)
{
    if (runtime == NULL || effective_state_changed == NULL ||
        !power_state_is_valid(runtime->effective_state)) {
        return false;
    }

    *effective_state_changed = false;
    if (runtime->usb_data_host_connected == connected) {
        return true;
    }
    runtime->usb_data_host_connected = connected;
    const app_power_state_t previous = runtime->effective_state;
    if (connected) {
        /* Terminal voltage while externally powered is not a trustworthy
         * remaining-capacity sample. Forget the old automatic latch and
         * require fresh readings after the host disappears. */
        runtime->automatic_saving_active = false;
        runtime->battery_observed = true;
    }
    reset_pending(runtime);
    runtime->effective_state = requested_effective_state(runtime);
    *effective_state_changed = runtime->effective_state != previous;
    return true;
}

bool app_power_policy_for_runtime(const app_power_runtime_t *runtime,
                                  app_power_policy_t *policy)
{
    if (runtime == NULL || policy == NULL ||
        !power_state_is_valid(runtime->effective_state) ||
        !policy_for_effective_state(runtime->effective_state, policy)) {
        return false;
    }
    return true;
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
