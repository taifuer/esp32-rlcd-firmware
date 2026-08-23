#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_TIME_STATE_UNINITIALIZED = 0,
    NETWORK_TIME_STATE_STARTING,
    NETWORK_TIME_STATE_PROVISIONING,
    NETWORK_TIME_STATE_CONNECTING,
    NETWORK_TIME_STATE_SYNCHRONIZING,
    NETWORK_TIME_STATE_SYNCHRONIZED,
    NETWORK_TIME_STATE_RETRY_WAIT,
    NETWORK_TIME_STATE_ERROR,
} network_time_state_t;

typedef enum {
    NETWORK_TIME_FAILURE_NONE = 0,
    NETWORK_TIME_FAILURE_WIFI,
    NETWORK_TIME_FAILURE_NTP,
    NETWORK_TIME_FAILURE_SERVICE,
} network_time_failure_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} network_time_datetime_t;

typedef struct {
    network_time_state_t state;
    bool configured;
    bool automatic_sync_enabled;
    esp_err_t last_error;
    network_time_failure_t last_failure;
    char setup_ssid[33];
    char setup_password[64];
    char setup_url[24];
} network_time_status_t;

esp_err_t network_time_init(bool automatic_sync_enabled);
esp_err_t network_time_get_status(network_time_status_t *status);
bool network_time_take_datetime(network_time_datetime_t *datetime);
esp_err_t network_time_request_sync(void);
esp_err_t network_time_clear_credentials(void);
esp_err_t network_time_begin_maintenance(void);
void network_time_end_maintenance(void);
/* Acquire an exclusive STA session and return only after IPv4 is ready. */
esp_err_t network_time_begin_online_session(uint32_t timeout_ms);
/* End a successful online session; calling it again after release is harmless. */
esp_err_t network_time_end_online_session(void);
const char *network_time_state_name(network_time_state_t state);
const char *network_time_failure_name(network_time_failure_t failure);

#ifdef __cplusplus
}
#endif
