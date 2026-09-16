#include "market_config.h"

#include <string.h>

#include "app_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static market_config_t s_config;
static uint32_t s_generation;

esp_err_t market_config_init(void)
{
    if (s_initialized) return ESP_OK;
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (s_initialized) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }
    esp_err_t error = app_storage_init();
    market_config_defaults(&s_config);
    nvs_handle_t handle;
    if (error == ESP_OK) {
        error = nvs_open("rlcd_market", NVS_READONLY, &handle);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            error = ESP_OK;
        } else if (error == ESP_OK) {
            uint8_t encoded[MARKET_CONFIG_RECORD_SIZE];
            size_t size = sizeof(encoded);
            error = nvs_get_blob(handle, "config", encoded, &size);
            if (error == ESP_OK) {
                (void)market_config_decode(encoded, size, &s_config);
            } else if (error == ESP_ERR_NVS_NOT_FOUND ||
                       error == ESP_ERR_NVS_INVALID_LENGTH ||
                       error == ESP_ERR_NVS_TYPE_MISMATCH) {
                error = ESP_OK;
            }
            nvs_close(handle);
        }
    }
    if (error == ESP_OK) s_initialized = true;
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t market_config_get_snapshot(market_config_t *config,
                                    uint32_t *generation)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t error = market_config_init();
    if (error != ESP_OK) return error;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    *config = s_config;
    if (generation != NULL) *generation = s_generation;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t market_config_get(market_config_t *config)
{
    return market_config_get_snapshot(config, NULL);
}

esp_err_t market_config_set(const market_config_t *config)
{
    uint8_t encoded[MARKET_CONFIG_RECORD_SIZE];
    if (!market_config_encode(config, encoded)) return ESP_ERR_INVALID_ARG;
    esp_err_t error = market_config_init();
    if (error != ESP_OK) return error;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (!market_config_equal(config, &s_config)) {
        nvs_handle_t handle;
        error = nvs_open("rlcd_market", NVS_READWRITE, &handle);
        if (error == ESP_OK) {
            error = nvs_set_blob(handle, "config", encoded, sizeof(encoded));
            if (error == ESP_OK) error = nvs_commit(handle);
            nvs_close(handle);
        }
        if (error == ESP_OK) {
            s_config = *config;
            memset(s_config.ids + s_config.count, 0,
                   MARKET_MAX_ROWS - s_config.count);
            ++s_generation;
        }
    }
    xSemaphoreGive(s_mutex);
    return error;
}
