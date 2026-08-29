#include "network_screen_policy.h"

bool app_network_setup_should_overlay(bool provisioning, bool configured,
                                      bool dismissed,
                                      uint32_t provisioning_elapsed_ms)
{
    return provisioning && !configured && !dismissed &&
           provisioning_elapsed_ms < APP_NETWORK_SETUP_SCREEN_MS;
}

app_wifi_display_t app_network_wifi_display(
    bool service_ready, bool configured, bool station_connected,
    bool background_network_enabled, bool connecting, bool retrying)
{
    if (!service_ready) {
        return APP_WIFI_DISPLAY_NOT_READY;
    }
    if (!configured) {
        return APP_WIFI_DISPLAY_NOT_CONFIGURED;
    }
    if (station_connected) {
        return APP_WIFI_DISPLAY_CONNECTED;
    }
    if (connecting) {
        return APP_WIFI_DISPLAY_CONNECTING;
    }
    if (!background_network_enabled) {
        return APP_WIFI_DISPLAY_OFF_SAVING;
    }
    return retrying ? APP_WIFI_DISPLAY_OFFLINE_RETRY
                    : APP_WIFI_DISPLAY_OFFLINE;
}

app_time_sync_display_t app_network_time_sync_display(
    bool service_ready, bool last_sync_valid, bool synchronizing,
    bool ntp_failure, bool service_failure)
{
    if (!service_ready) {
        return APP_TIME_SYNC_DISPLAY_NOT_READY;
    }
    if (synchronizing) {
        return APP_TIME_SYNC_DISPLAY_SYNCING;
    }
    if (ntp_failure) {
        return APP_TIME_SYNC_DISPLAY_NTP_ERROR;
    }
    if (service_failure) {
        return APP_TIME_SYNC_DISPLAY_SERVICE_ERROR;
    }
    return last_sync_valid ? APP_TIME_SYNC_DISPLAY_OK
                           : APP_TIME_SYNC_DISPLAY_NOT_SYNCED;
}
