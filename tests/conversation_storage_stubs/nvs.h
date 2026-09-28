#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef unsigned nvs_handle_t;
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 6
#define ESP_ERR_NVS_TYPE_MISMATCH 7
#define ESP_ERR_NVS_INVALID_LENGTH 8
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value,
                       size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                       size_t size);
esp_err_t nvs_erase_all(nvs_handle_t handle);
