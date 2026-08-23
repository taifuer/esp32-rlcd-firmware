#include "alarm_history.h"

#include <stddef.h>
#include <stdint.h>

#include "nvs.h"

#define ALARM_HISTORY_NAMESPACE "rlcd_alarm"
#define ALARM_HISTORY_KEY "last_fired"

static bool missing_or_invalid_nvs_value(esp_err_t error)
{
    return error == ESP_ERR_NVS_NOT_FOUND ||
           error == ESP_ERR_NVS_TYPE_MISMATCH ||
           error == ESP_ERR_NVS_INVALID_LENGTH;
}

esp_err_t alarm_history_load(alarm_history_record_t *record, bool *found)
{
    if (record == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = false;

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(ALARM_HISTORY_NAMESPACE, NVS_READONLY,
                               &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    size_t encoded_size = 0U;
    error = nvs_get_blob(handle, ALARM_HISTORY_KEY, NULL, &encoded_size);
    if (missing_or_invalid_nvs_value(error)) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (error != ESP_OK) {
        nvs_close(handle);
        return error;
    }
    if (encoded_size != ALARM_HISTORY_RECORD_ENCODED_SIZE) {
        nvs_close(handle);
        return ESP_OK;
    }

    uint8_t encoded[ALARM_HISTORY_RECORD_ENCODED_SIZE];
    error = nvs_get_blob(handle, ALARM_HISTORY_KEY, encoded,
                         &encoded_size);
    nvs_close(handle);
    if (missing_or_invalid_nvs_value(error)) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    alarm_history_record_t decoded;
    if (alarm_history_record_decode(encoded, encoded_size, &decoded)) {
        *record = decoded;
        *found = true;
    }
    return ESP_OK;
}

esp_err_t alarm_history_store(const alarm_history_record_t *record)
{
    uint8_t encoded[ALARM_HISTORY_RECORD_ENCODED_SIZE];
    if (!alarm_history_record_encode(record, encoded, sizeof(encoded))) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(ALARM_HISTORY_NAMESPACE, NVS_READWRITE,
                               &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_blob(handle, ALARM_HISTORY_KEY, encoded,
                         sizeof(encoded));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}
