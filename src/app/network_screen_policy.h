#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    APP_NETWORK_SETUP_SCREEN_MS = 60000U,
};

bool app_network_setup_should_overlay(bool provisioning, bool configured,
                                      bool dismissed,
                                      uint32_t provisioning_elapsed_ms);

typedef enum {
    APP_WIFI_DISPLAY_NOT_READY = 0,
    APP_WIFI_DISPLAY_NOT_CONFIGURED,
    APP_WIFI_DISPLAY_CONNECTED,
    APP_WIFI_DISPLAY_CONNECTING,
    APP_WIFI_DISPLAY_OFFLINE_RETRY,
    APP_WIFI_DISPLAY_OFF_SAVING,
    APP_WIFI_DISPLAY_OFFLINE,
} app_wifi_display_t;

typedef enum {
    APP_TIME_SYNC_DISPLAY_NOT_READY = 0,
    APP_TIME_SYNC_DISPLAY_NOT_SYNCED,
    APP_TIME_SYNC_DISPLAY_SYNCING,
    APP_TIME_SYNC_DISPLAY_OK,
    APP_TIME_SYNC_DISPLAY_NTP_ERROR,
    APP_TIME_SYNC_DISPLAY_SERVICE_ERROR,
} app_time_sync_display_t;

app_wifi_display_t app_network_wifi_display(
    bool service_ready, bool configured, bool station_connected,
    bool background_network_enabled, bool connecting, bool retrying);

app_time_sync_display_t app_network_time_sync_display(
    bool service_ready, bool last_sync_valid, bool synchronizing,
    bool ntp_failure, bool service_failure);

#ifdef __cplusplus
}
#endif
