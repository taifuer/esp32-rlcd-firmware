#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "firmware_update_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_UPDATE_SSID_CAPACITY 33U
#define FIRMWARE_UPDATE_PASSWORD_CAPACITY 64U
#define FIRMWARE_UPDATE_URL_CAPACITY 24U
#define FIRMWARE_UPDATE_VERSION_CAPACITY 33U

typedef struct {
    firmware_update_state_t state;
    esp_err_t last_error;
    size_t received_bytes;
    size_t total_bytes;
    uint8_t percent;
    char access_point_ssid[FIRMWARE_UPDATE_SSID_CAPACITY];
    char access_point_password[FIRMWARE_UPDATE_PASSWORD_CAPACITY];
    char access_url[FIRMWARE_UPDATE_URL_CAPACITY];
    char incoming_version[FIRMWARE_UPDATE_VERSION_CAPACITY];
} firmware_update_status_t;

esp_err_t firmware_update_init(void);
esp_err_t firmware_update_confirm_running_image(void);
esp_err_t firmware_update_start(void);
esp_err_t firmware_update_cancel(void);
esp_err_t firmware_update_dismiss_result(void);
esp_err_t firmware_update_get_status(firmware_update_status_t *status);

#ifdef __cplusplus
}
#endif
