#include "settings_power_policy.h"

#include <stddef.h>

static bool configured_mode_is_valid(app_power_mode_t mode)
{
    return mode == APP_POWER_MODE_AUTO ||
           mode == APP_POWER_MODE_NORMAL ||
           mode == APP_POWER_MODE_SAVING;
}

static bool effective_mode_is_valid(app_power_mode_t mode)
{
    return mode == APP_POWER_MODE_NORMAL ||
           mode == APP_POWER_MODE_SAVING;
}

static bool policy_for_effective_mode(app_power_mode_t mode,
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

static void reset_pending(app_power_runtime_t *runtime)
{
    runtime->pending_mode = runtime->effective_mode;
    runtime->pending_samples = 0U;
}

bool app_power_runtime_init(app_power_runtime_t *runtime,
                            app_power_mode_t configured_mode)
{
    if (runtime == NULL || !configured_mode_is_valid(configured_mode)) {
        return false;
    }
    const app_power_mode_t effective =
        configured_mode == APP_POWER_MODE_SAVING
            ? APP_POWER_MODE_SAVING
            : APP_POWER_MODE_NORMAL;
    *runtime = (app_power_runtime_t){
        .configured_mode = configured_mode,
        .effective_mode = effective,
        .pending_mode = effective,
    };
    return true;
}

bool app_power_runtime_set_configured(
    app_power_runtime_t *runtime, app_power_mode_t configured_mode,
    bool battery_valid, uint8_t battery_percent,
    bool *effective_mode_changed)
{
    if (runtime == NULL || effective_mode_changed == NULL ||
        !configured_mode_is_valid(configured_mode) ||
        !effective_mode_is_valid(runtime->effective_mode) ||
        (battery_valid && battery_percent > 100U)) {
        return false;
    }

    if (configured_mode == runtime->configured_mode) {
        *effective_mode_changed = false;
        return true;
    }

    const app_power_mode_t previous = runtime->effective_mode;
    runtime->configured_mode = configured_mode;
    reset_pending(runtime);

    if (configured_mode == APP_POWER_MODE_NORMAL ||
        configured_mode == APP_POWER_MODE_SAVING) {
        runtime->effective_mode = configured_mode;
    } else if (battery_valid) {
        runtime->battery_observed = true;
        if (battery_percent <= APP_POWER_AUTO_ENTER_PERCENT) {
            runtime->effective_mode = APP_POWER_MODE_SAVING;
        } else if (battery_percent >= APP_POWER_AUTO_EXIT_PERCENT) {
            runtime->effective_mode = APP_POWER_MODE_NORMAL;
        }
    }
    reset_pending(runtime);
    *effective_mode_changed = runtime->effective_mode != previous;
    return true;
}

bool app_power_runtime_observe_battery(
    app_power_runtime_t *runtime, bool battery_valid,
    uint8_t battery_percent, bool *effective_mode_changed)
{
    if (runtime == NULL || effective_mode_changed == NULL ||
        !configured_mode_is_valid(runtime->configured_mode) ||
        !effective_mode_is_valid(runtime->effective_mode) ||
        (battery_valid && battery_percent > 100U)) {
        return false;
    }
    *effective_mode_changed = false;

    if (runtime->configured_mode != APP_POWER_MODE_AUTO) {
        reset_pending(runtime);
        return true;
    }
    if (!battery_valid) {
        reset_pending(runtime);
        return true;
    }

    if (!runtime->battery_observed) {
        runtime->battery_observed = true;
        const app_power_mode_t previous = runtime->effective_mode;
        /* There is no persisted hysteresis latch. In the 21-24% band, a
         * fresh boot conservatively starts in SAVING so a reboot cannot be
         * used to exit low-power operation before the 25% recovery point. */
        if (battery_percent < APP_POWER_AUTO_EXIT_PERCENT) {
            runtime->effective_mode = APP_POWER_MODE_SAVING;
        } else {
            runtime->effective_mode = APP_POWER_MODE_NORMAL;
        }
        reset_pending(runtime);
        *effective_mode_changed = runtime->effective_mode != previous;
        return true;
    }

    app_power_mode_t requested = runtime->effective_mode;
    if (runtime->effective_mode == APP_POWER_MODE_NORMAL &&
        battery_percent <= APP_POWER_AUTO_ENTER_PERCENT) {
        requested = APP_POWER_MODE_SAVING;
    } else if (runtime->effective_mode == APP_POWER_MODE_SAVING &&
               battery_percent >= APP_POWER_AUTO_EXIT_PERCENT) {
        requested = APP_POWER_MODE_NORMAL;
    }

    if (requested == runtime->effective_mode) {
        reset_pending(runtime);
        return true;
    }
    if (runtime->pending_mode != requested) {
        runtime->pending_mode = requested;
        runtime->pending_samples = 1U;
        return true;
    }
    if (runtime->pending_samples < UINT8_MAX) {
        runtime->pending_samples++;
    }
    if (runtime->pending_samples >= APP_POWER_AUTO_CONFIRM_SAMPLES) {
        runtime->effective_mode = requested;
        reset_pending(runtime);
        *effective_mode_changed = true;
    }
    return true;
}

bool app_power_policy_for_runtime(const app_power_runtime_t *runtime,
                                  app_power_policy_t *policy)
{
    if (runtime == NULL || policy == NULL ||
        !configured_mode_is_valid(runtime->configured_mode) ||
        !effective_mode_is_valid(runtime->effective_mode) ||
        !policy_for_effective_mode(runtime->effective_mode, policy)) {
        return false;
    }

    /* AUTO must notice charging recovery promptly. Manual SAVING keeps its
     * original five-minute battery cadence. */
    if (runtime->configured_mode == APP_POWER_MODE_AUTO &&
        runtime->effective_mode == APP_POWER_MODE_SAVING) {
        policy->battery_read_interval_ms = 60000U;
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
