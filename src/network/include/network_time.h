#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "network_credentials.h"

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
    /* Current STA link has usable IPv4; excludes provisioning SoftAP. */
    bool station_connected;
    esp_err_t last_error;
    network_time_failure_t last_failure;
    char setup_ssid[33];
    char setup_password[64];
    char setup_url[24];
} network_time_status_t;

typedef struct {
    bool configured;
    char ssid[NETWORK_SSID_MAX_LENGTH + 1U];
} network_time_saved_network_t;

/* Recovery disables the initial provisioning window so maintenance can own
 * the radio even when no credentials have been saved. Explicit provisioning
 * requests remain available. */
esp_err_t network_time_init(bool automatic_sync_enabled,
                            bool startup_provisioning_enabled);
esp_err_t network_time_get_status(network_time_status_t *status);
bool network_time_take_datetime(network_time_datetime_t *datetime);
esp_err_t network_time_request_sync(void);
esp_err_t network_time_set_automatic_sync_enabled(bool enabled);
/* Return only the saved SSID. The saved password never leaves this module. */
esp_err_t network_time_get_saved_network(
    network_time_saved_network_t *network);
/* While a settings SoftAP owns maintenance, test a candidate in RAM and save
 * it only after the STA obtains IPv4. The SoftAP remains available. */
esp_err_t network_time_validate_and_save_credentials(
    const network_credentials_t *credentials, uint32_t timeout_ms,
    bool *portal_available);
esp_err_t network_time_clear_credentials(void);
/* Atomically pause any persistent station, clear credentials, and hand the
 * radio to the provisioning task. Intended for the USB maintenance command. */
esp_err_t network_time_forget_and_request_provisioning(void);
/* Wake an idle network task after credentials were cleared without maintenance. */
esp_err_t network_time_request_provisioning(void);
esp_err_t network_time_begin_maintenance(void);
void network_time_end_maintenance(void);
/* Release maintenance directly to the network task after its portal/AP stopped. */
esp_err_t network_time_end_maintenance_and_request_provisioning(void);
/* Release a stopped settings portal directly to a normal connect/sync pass. */
esp_err_t network_time_end_maintenance_and_request_sync(void);
/* Acquire an exclusive STA session and return only after IPv4 is ready. */
esp_err_t network_time_begin_online_session(uint32_t timeout_ms);
/* Atomically hand an active settings-maintenance session to an online task. */
esp_err_t network_time_begin_online_session_from_maintenance(
    uint32_t timeout_ms);
/* End a successful online session; calling it again after release is harmless. */
esp_err_t network_time_end_online_session(void);
const char *network_time_state_name(network_time_state_t state);
const char *network_time_failure_name(network_time_failure_t failure);

#ifdef __cplusplus
}
#endif
