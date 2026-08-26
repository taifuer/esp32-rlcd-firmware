#include "power_manager.h"

#include <stddef.h>

#include "esp_log.h"
#include "esp_pm.h"
#include "sdkconfig.h"

#ifndef CONFIG_PM_ENABLE
#error "CONFIG_PM_ENABLE must be enabled for the RLCD power manager"
#endif

#if CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240
#error "RLCD NORMAL mode requires the configured 240 MHz CPU maximum"
#endif

#define POWER_MANAGER_MAX_CPU_MHZ CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
#define POWER_MANAGER_MIN_CPU_MHZ 80

static const char *TAG = "power_manager";
static esp_pm_lock_handle_t s_normal_mode_lock;
static bool s_initialized;
static bool s_saving;

esp_err_t power_manager_init(bool saving)
{
    if (s_initialized) {
        return power_manager_set_saving(saving);
    }

    esp_pm_lock_handle_t lock = NULL;
    esp_err_t error = esp_pm_lock_create(
        ESP_PM_CPU_FREQ_MAX, 0, "rlcd_normal", &lock);
    if (error != ESP_OK) {
        return error;
    }

    /* Keep the CPU at its existing 240 MHz setting while DFS is configured.
     * If configuration fails, deleting this temporary lock leaves the board
     * on the original fixed-frequency behavior. */
    error = esp_pm_lock_acquire(lock);
    if (error != ESP_OK) {
        (void)esp_pm_lock_delete(lock);
        return error;
    }

    const esp_pm_config_t config = {
        .max_freq_mhz = POWER_MANAGER_MAX_CPU_MHZ,
        .min_freq_mhz = POWER_MANAGER_MIN_CPU_MHZ,
        .light_sleep_enable = false,
    };
    error = esp_pm_configure(&config);
    if (error != ESP_OK) {
        (void)esp_pm_lock_release(lock);
        (void)esp_pm_lock_delete(lock);
        return error;
    }

    s_normal_mode_lock = lock;
    s_saving = false;
    s_initialized = true;
    if (saving) {
        error = power_manager_set_saving(true);
        if (error != ESP_OK) {
            return error;
        }
    }

    ESP_LOGI(TAG,
             "dynamic frequency scaling ready: %d-%d MHz, light sleep off, mode=%s",
             POWER_MANAGER_MIN_CPU_MHZ, POWER_MANAGER_MAX_CPU_MHZ,
             s_saving ? "saving" : "normal");
    return ESP_OK;
}

esp_err_t power_manager_set_saving(bool saving)
{
    if (!s_initialized || s_normal_mode_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (saving == s_saving) {
        return ESP_OK;
    }

    const esp_err_t error = saving
                                ? esp_pm_lock_release(s_normal_mode_lock)
                                : esp_pm_lock_acquire(s_normal_mode_lock);
    if (error == ESP_OK) {
        s_saving = saving;
        ESP_LOGI(TAG, "CPU policy switched to %s",
                 saving ? "saving (80-240 MHz)" : "normal (240 MHz)");
    }
    return error;
}

bool power_manager_is_ready(void)
{
    return s_initialized;
}

bool power_manager_is_saving(void)
{
    return s_initialized && s_saving;
}
