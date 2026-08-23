#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "online_update_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ONLINE_FIRMWARE_UPDATE_VERSION_CAPACITY \
    (ONLINE_UPDATE_VERSION_MAX_LENGTH + 1U)
#define ONLINE_FIRMWARE_UPDATE_CHECKED_CAPACITY 17U

typedef struct {
    online_update_state_t state;
    online_update_error_t policy_error;
    esp_err_t last_error;
    size_t downloaded_bytes;
    size_t total_bytes;
    uint8_t percent;
    bool beta_channel;
    char current_version[ONLINE_FIRMWARE_UPDATE_VERSION_CAPACITY];
    char latest_version[ONLINE_FIRMWARE_UPDATE_VERSION_CAPACITY];
    char last_checked[ONLINE_FIRMWARE_UPDATE_CHECKED_CAPACITY];
} online_firmware_update_status_t;

esp_err_t online_firmware_update_init(const char *current_version,
                                      bool beta_updates_enabled);
esp_err_t online_firmware_update_request_check(void);
esp_err_t online_firmware_update_request_confirmation(void);
esp_err_t online_firmware_update_start_install(void);
esp_err_t online_firmware_update_cancel(void);
esp_err_t online_firmware_update_get_status(
    online_firmware_update_status_t *status);

const char *online_firmware_update_error_detail(
    const online_firmware_update_status_t *status);

#ifdef __cplusplus
}
#endif
