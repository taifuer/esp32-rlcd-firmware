#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "page_state.h"

int main(void)
{
    app_page_state_t state;
    app_page_state_init(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    assert(app_page_is_daily(APP_PAGE_HOME));
    assert(app_page_is_daily(APP_PAGE_CALENDAR));
    assert(!app_page_is_daily(APP_PAGE_DEVICE_HEALTH));
    assert(app_page_is_system(APP_PAGE_DEVICE_HEALTH));
    assert(app_page_is_system(APP_PAGE_NETWORK_TIME));
    assert(app_page_is_system(APP_PAGE_AUDIO));
    assert(app_page_is_system(APP_PAGE_WIFI_MAINTENANCE));
    assert(app_page_is_system(APP_PAGE_ABOUT_UPDATE));
    assert(!app_page_is_system(APP_PAGE_HOME));
    assert(app_page_key_hold_action(APP_PAGE_HOME) == APP_PAGE_ACTION_NONE);
    assert(app_page_key_hold_action(APP_PAGE_CALENDAR) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_key_hold_action(APP_PAGE_DEVICE_HEALTH) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_key_hold_action(APP_PAGE_NETWORK_TIME) ==
           APP_PAGE_ACTION_SYNC_TIME);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_NETWORK_TIME) ==
           APP_PAGE_MANUAL_SYNC_HOLD_MS);
    assert(app_page_key_hold_action(APP_PAGE_AUDIO) ==
           APP_PAGE_ACTION_TEST_AUDIO);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_AUDIO) ==
           APP_PAGE_AUDIO_TEST_HOLD_MS);
    assert(app_page_key_hold_action(APP_PAGE_WIFI_MAINTENANCE) ==
           APP_PAGE_ACTION_RESET_WIFI);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_WIFI_MAINTENANCE) ==
           APP_PAGE_WIFI_RESET_HOLD_MS);
    assert(app_page_key_hold_action(APP_PAGE_ABOUT_UPDATE) ==
           APP_PAGE_ACTION_START_UPDATE);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_ABOUT_UPDATE) ==
           APP_PAGE_FIRMWARE_UPDATE_HOLD_MS);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_HOME) == 0U);

    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_CALENDAR);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_boot_short_press(&state);
    assert(!app_page_state_tick(&state,
                                APP_PAGE_SECONDARY_TIMEOUT_MS - 1U));
    app_page_state_note_activity(&state);
    assert(!app_page_state_tick(&state,
                                APP_PAGE_SECONDARY_TIMEOUT_MS - 1U));
    assert(app_page_state_tick(&state, 1U));
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_DEVICE_HEALTH);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_NETWORK_TIME);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_AUDIO);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_WIFI_MAINTENANCE);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_ABOUT_UPDATE);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_DEVICE_HEALTH);
    assert(!app_page_state_tick(&state,
                                APP_PAGE_SECONDARY_TIMEOUT_MS - 1U));
    assert(app_page_state_tick(&state, 1U));
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_boot_short_press(&state);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_DEVICE_HEALTH);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_init(NULL);
    app_page_state_boot_short_press(NULL);
    app_page_state_key_short_press(NULL);
    app_page_state_note_activity(NULL);
    assert(!app_page_state_tick(NULL, UINT32_MAX));
    assert(app_page_state_current(NULL) == APP_PAGE_HOME);

    puts("page state tests passed");
    return 0;
}
