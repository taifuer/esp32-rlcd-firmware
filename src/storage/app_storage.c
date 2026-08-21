#include "app_storage.h"

#include <stdbool.h>

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "app_storage";
static bool s_initialized;

esp_err_t app_storage_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG,
                 "NVS requires reinitialization; project settings will be cleared");
        error = nvs_flash_erase();
        if (error == ESP_OK) {
            error = nvs_flash_init();
        }
    }
    if (error == ESP_OK) {
        s_initialized = true;
    }
    return error;
}
