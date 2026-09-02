#include "weather_cache.h"

#include <stdbool.h>
#include <string.h>

#include "app_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define WEATHER_CACHE_NAMESPACE "rlcd_wcache"
#define WEATHER_CACHE_SLOT_A_KEY "cache_a"
#define WEATHER_CACHE_SLOT_B_KEY "cache_b"

static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static bool s_available;
static weather_cache_data_t s_data;
static weather_cache_record_slot_t s_active_slot;
static uint32_t s_generation;

static bool recoverable_error(esp_err_t error)
{
    return error == ESP_ERR_NVS_NOT_FOUND ||
           error == ESP_ERR_NVS_TYPE_MISMATCH ||
           error == ESP_ERR_NVS_INVALID_LENGTH;
}

static const char *slot_key(weather_cache_record_slot_t slot)
{
    if (slot == WEATHER_CACHE_RECORD_SLOT_A) {
        return WEATHER_CACHE_SLOT_A_KEY;
    }
    if (slot == WEATHER_CACHE_RECORD_SLOT_B) {
        return WEATHER_CACHE_SLOT_B_KEY;
    }
    return NULL;
}

static esp_err_t read_slot(nvs_handle_t handle,
                           weather_cache_record_slot_t slot,
                           weather_cache_record_t *record, bool *valid)
{
    const char *key = slot_key(slot);
    if (key == NULL || record == NULL || valid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *valid = false;
    size_t length = 0U;
    esp_err_t error = nvs_get_blob(handle, key, NULL, &length);
    if (recoverable_error(error) ||
        (error == ESP_OK && length != WEATHER_CACHE_RECORD_ENCODED_SIZE)) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    uint8_t encoded[WEATHER_CACHE_RECORD_ENCODED_SIZE];
    error = nvs_get_blob(handle, key, encoded, &length);
    if (recoverable_error(error)) {
        return ESP_OK;
    }
    if (error == ESP_OK) {
        *valid = weather_cache_record_decode(encoded, length, record);
    }
    return error;
}

static esp_err_t write_slot(nvs_handle_t handle,
                            weather_cache_record_slot_t slot,
                            uint32_t generation,
                            const weather_cache_data_t *data)
{
    const char *key = slot_key(slot);
    uint8_t encoded[WEATHER_CACHE_RECORD_ENCODED_SIZE];
    if (key == NULL || !weather_cache_record_encode(
                           generation, data, encoded, sizeof(encoded))) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = nvs_set_blob(handle, key, encoded, sizeof(encoded));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    return error;
}

esp_err_t weather_cache_init(void)
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
        error = nvs_open(WEATHER_CACHE_NAMESPACE, NVS_READWRITE, &handle);
        handle_open = error == ESP_OK;
    }
    weather_cache_record_t slot_a = {0};
    weather_cache_record_t slot_b = {0};
    bool slot_a_valid = false;
    bool slot_b_valid = false;
    if (error == ESP_OK) {
        error = read_slot(handle, WEATHER_CACHE_RECORD_SLOT_A,
                          &slot_a, &slot_a_valid);
    }
    if (error == ESP_OK) {
        error = read_slot(handle, WEATHER_CACHE_RECORD_SLOT_B,
                          &slot_b, &slot_b_valid);
    }
    if (handle_open) {
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        s_active_slot = weather_cache_record_select_latest(
            slot_a_valid ? &slot_a : NULL,
            slot_b_valid ? &slot_b : NULL);
        s_available = s_active_slot != WEATHER_CACHE_RECORD_SLOT_NONE;
        s_generation = 0U;
        memset(&s_data, 0, sizeof(s_data));
        if (s_active_slot == WEATHER_CACHE_RECORD_SLOT_A) {
            s_data = slot_a.data;
            s_generation = slot_a.generation;
        } else if (s_active_slot == WEATHER_CACHE_RECORD_SLOT_B) {
            s_data = slot_b.data;
            s_generation = slot_b.generation;
        }
        s_initialized = true;
    }
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t weather_cache_get(weather_cache_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const bool available = s_available;
    if (available) {
        *data = s_data;
    } else {
        memset(data, 0, sizeof(*data));
    }
    xSemaphoreGive(s_mutex);
    return available ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t weather_cache_save(const weather_cache_data_t *data)
{
    if (!weather_cache_data_is_valid(data)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const weather_cache_record_slot_t target =
        s_active_slot == WEATHER_CACHE_RECORD_SLOT_A
            ? WEATHER_CACHE_RECORD_SLOT_B
            : WEATHER_CACHE_RECORD_SLOT_A;
    const uint32_t generation = s_generation + 1U;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(WEATHER_CACHE_NAMESPACE,
                               NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = write_slot(handle, target, generation, data);
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        s_data = *data;
        s_available = true;
        s_active_slot = target;
        s_generation = generation;
    }
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t weather_cache_clear(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(WEATHER_CACHE_NAMESPACE,
                               NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_erase_all(handle);
        if (error == ESP_OK) {
            error = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        memset(&s_data, 0, sizeof(s_data));
        s_available = false;
        s_active_slot = WEATHER_CACHE_RECORD_SLOT_NONE;
        s_generation = 0U;
    }
    xSemaphoreGive(s_mutex);
    return error;
}
