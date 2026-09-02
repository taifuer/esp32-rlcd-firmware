#include "weather_config.h"

#include <stdbool.h>

#include "app_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "weather_config_record.h"

#define WEATHER_CONFIG_NAMESPACE "rlcd_weather"
#define WEATHER_CONFIG_SLOT_A_KEY "cfg_a"
#define WEATHER_CONFIG_SLOT_B_KEY "cfg_b"

static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static weather_config_t s_config;
static weather_config_record_slot_t s_active_slot;
static uint32_t s_generation;

static bool recoverable_record_error(esp_err_t error)
{
    return error == ESP_ERR_NVS_NOT_FOUND ||
           error == ESP_ERR_NVS_TYPE_MISMATCH ||
           error == ESP_ERR_NVS_INVALID_LENGTH;
}

static const char *record_slot_key(weather_config_record_slot_t slot)
{
    if (slot == WEATHER_CONFIG_RECORD_SLOT_A) {
        return WEATHER_CONFIG_SLOT_A_KEY;
    }
    if (slot == WEATHER_CONFIG_RECORD_SLOT_B) {
        return WEATHER_CONFIG_SLOT_B_KEY;
    }
    return NULL;
}

static esp_err_t read_record_slot(
    nvs_handle_t handle, weather_config_record_slot_t slot,
    weather_config_record_t *record, bool *valid)
{
    const char *key = record_slot_key(slot);
    if (key == NULL || record == NULL || valid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *valid = false;
    weather_config_clear_sensitive(record, sizeof(*record));

    size_t encoded_size = 0U;
    esp_err_t error = nvs_get_blob(handle, key, NULL, &encoded_size);
    if (recoverable_record_error(error)) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    if (encoded_size != WEATHER_CONFIG_RECORD_ENCODED_SIZE) {
        return ESP_OK;
    }

    uint8_t encoded[WEATHER_CONFIG_RECORD_ENCODED_SIZE];
    error = nvs_get_blob(handle, key, encoded, &encoded_size);
    if (recoverable_record_error(error)) {
        weather_config_clear_sensitive(encoded, sizeof(encoded));
        return ESP_OK;
    }
    if (error == ESP_OK) {
        *valid = weather_config_record_decode(encoded, encoded_size,
                                              record);
    }
    weather_config_clear_sensitive(encoded, sizeof(encoded));
    return error;
}

static esp_err_t write_record_slot(
    nvs_handle_t handle, weather_config_record_slot_t slot,
    uint32_t generation, const weather_config_t *config)
{
    const char *key = record_slot_key(slot);
    uint8_t encoded[WEATHER_CONFIG_RECORD_ENCODED_SIZE];
    if (key == NULL || !weather_config_record_encode(
                           generation, config, encoded,
                           sizeof(encoded))) {
        weather_config_clear_sensitive(encoded, sizeof(encoded));
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = nvs_set_blob(handle, key, encoded, sizeof(encoded));
    weather_config_clear_sensitive(encoded, sizeof(encoded));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    return error;
}

esp_err_t weather_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_initialized) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    esp_err_t error = app_storage_init();
    nvs_handle_t handle = 0;
    bool handle_open = false;
    if (error == ESP_OK) {
        error = nvs_open(WEATHER_CONFIG_NAMESPACE, NVS_READWRITE,
                         &handle);
        handle_open = error == ESP_OK;
    }

    weather_config_record_t slot_a = {0};
    weather_config_record_t slot_b = {0};
    bool slot_a_valid = false;
    bool slot_b_valid = false;
    if (error == ESP_OK) {
        error = read_record_slot(handle, WEATHER_CONFIG_RECORD_SLOT_A,
                                 &slot_a, &slot_a_valid);
    }
    if (error == ESP_OK) {
        error = read_record_slot(handle, WEATHER_CONFIG_RECORD_SLOT_B,
                                 &slot_b, &slot_b_valid);
    }
    if (handle_open) {
        nvs_close(handle);
    }

    if (error == ESP_OK) {
        const weather_config_record_slot_t latest =
            weather_config_record_select_latest(
                slot_a_valid ? &slot_a : NULL,
                slot_b_valid ? &slot_b : NULL);
        weather_config_reset(&s_config);
        s_active_slot = latest;
        s_generation = 0U;
        if (latest == WEATHER_CONFIG_RECORD_SLOT_A) {
            s_config = slot_a.config;
            s_generation = slot_a.generation;
        } else if (latest == WEATHER_CONFIG_RECORD_SLOT_B) {
            s_config = slot_b.config;
            s_generation = slot_b.generation;
        }
        s_initialized = true;
    }
    weather_config_clear_sensitive(&slot_a, sizeof(slot_a));
    weather_config_clear_sensitive(&slot_b, sizeof(slot_b));
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t weather_config_get_snapshot(weather_config_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    snapshot->config = s_config;
    snapshot->generation = s_generation;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t weather_config_get_status(weather_config_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    weather_config_make_status(&s_config, s_generation, status);
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t weather_config_save(const weather_config_update_t *update)
{
    if (update == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    weather_config_t candidate;
    const weather_config_result_t validation =
        weather_config_apply_update(&s_config, update, &candidate);
    if (validation != WEATHER_CONFIG_RESULT_OK) {
        weather_config_clear_sensitive(&candidate, sizeof(candidate));
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    const weather_config_record_slot_t target =
        s_active_slot == WEATHER_CONFIG_RECORD_SLOT_A
            ? WEATHER_CONFIG_RECORD_SLOT_B
            : WEATHER_CONFIG_RECORD_SLOT_A;
    const uint32_t generation = s_generation + 1U;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(WEATHER_CONFIG_NAMESPACE, NVS_READWRITE,
                               &handle);
    if (error == ESP_OK) {
        error = write_record_slot(handle, target, generation, &candidate);
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        weather_config_clear_sensitive(&s_config, sizeof(s_config));
        s_config = candidate;
        s_active_slot = target;
        s_generation = generation;
    }
    weather_config_clear_sensitive(&candidate, sizeof(candidate));
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t weather_config_clear(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(WEATHER_CONFIG_NAMESPACE, NVS_READWRITE,
                               &handle);
    if (error == ESP_OK) {
        error = nvs_erase_all(handle);
        if (error == ESP_OK) {
            error = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        weather_config_reset(&s_config);
        s_active_slot = WEATHER_CONFIG_RECORD_SLOT_NONE;
        s_generation = 0U;
    }
    xSemaphoreGive(s_mutex);
    return error;
}
