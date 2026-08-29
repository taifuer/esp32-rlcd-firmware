#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "network_screen_policy.h"

int main(void)
{
    assert(app_network_setup_should_overlay(true, false, false, 0U));
    assert(app_network_setup_should_overlay(
        true, false, false, APP_NETWORK_SETUP_SCREEN_MS - 1U));
    assert(!app_network_setup_should_overlay(
        true, false, false, APP_NETWORK_SETUP_SCREEN_MS));
    assert(!app_network_setup_should_overlay(false, false, false, 0U));
    assert(!app_network_setup_should_overlay(true, true, false, 0U));
    assert(!app_network_setup_should_overlay(true, false, true, 0U));
    assert(!app_network_setup_should_overlay(false, true, true, UINT32_MAX));

    assert(app_network_wifi_display(false, false, false, true, false, false) ==
           APP_WIFI_DISPLAY_NOT_READY);
    assert(app_network_wifi_display(true, false, false, true, false, false) ==
           APP_WIFI_DISPLAY_NOT_CONFIGURED);
    assert(app_network_wifi_display(true, true, true, true, false, false) ==
           APP_WIFI_DISPLAY_CONNECTED);
    assert(app_network_wifi_display(true, true, true, false, false, false) ==
           APP_WIFI_DISPLAY_CONNECTED);
    assert(app_network_wifi_display(true, true, false, false, true, false) ==
           APP_WIFI_DISPLAY_CONNECTING);
    assert(app_network_wifi_display(true, true, false, false, false, false) ==
           APP_WIFI_DISPLAY_OFF_SAVING);
    assert(app_network_wifi_display(true, true, false, true, false, true) ==
           APP_WIFI_DISPLAY_OFFLINE_RETRY);
    assert(app_network_wifi_display(true, true, false, true, false, false) ==
           APP_WIFI_DISPLAY_OFFLINE);

    assert(app_network_time_sync_display(false, false, false, false, false) ==
           APP_TIME_SYNC_DISPLAY_NOT_READY);
    assert(app_network_time_sync_display(true, false, false, false, false) ==
           APP_TIME_SYNC_DISPLAY_NOT_SYNCED);
    assert(app_network_time_sync_display(true, true, true, false, false) ==
           APP_TIME_SYNC_DISPLAY_SYNCING);
    assert(app_network_time_sync_display(true, true, false, true, false) ==
           APP_TIME_SYNC_DISPLAY_NTP_ERROR);
    assert(app_network_time_sync_display(true, true, false, false, true) ==
           APP_TIME_SYNC_DISPLAY_SERVICE_ERROR);
    assert(app_network_time_sync_display(true, true, false, false, false) ==
           APP_TIME_SYNC_DISPLAY_OK);

    puts("network screen policy tests passed");
    return 0;
}
