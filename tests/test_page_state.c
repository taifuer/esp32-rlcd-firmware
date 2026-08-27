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
    assert(app_page_is_daily(APP_PAGE_IMAGE));
    assert(!app_page_is_daily(APP_PAGE_STATUS));
    assert(app_page_is_system(APP_PAGE_STATUS));
    assert(app_page_is_system(APP_PAGE_AUDIO));
    assert(app_page_is_system(APP_PAGE_SETTINGS));
    assert(app_page_is_system(APP_PAGE_ONLINE_UPDATE));
    assert(!app_page_is_system(APP_PAGE_HOME));
    assert(!app_page_is_system(APP_PAGE_IMAGE));
    assert(app_page_key_hold_action(APP_PAGE_HOME) == APP_PAGE_ACTION_NONE);
    assert(app_page_key_hold_action(APP_PAGE_CALENDAR) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_key_hold_action(APP_PAGE_IMAGE) ==
           APP_PAGE_ACTION_DELETE_IMAGE);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_IMAGE) ==
           APP_PAGE_IMAGE_DELETE_HOLD_MS);
    assert(app_page_key_hold_action(APP_PAGE_STATUS) ==
           APP_PAGE_ACTION_SYNC_TIME);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_STATUS) ==
           APP_PAGE_MANUAL_SYNC_HOLD_MS);
    assert(app_page_key_hold_action(APP_PAGE_AUDIO) ==
           APP_PAGE_ACTION_TEST_AUDIO);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_AUDIO) ==
           APP_PAGE_AUDIO_TEST_HOLD_MS);
    assert(app_page_key_hold_action(APP_PAGE_SETTINGS) ==
           APP_PAGE_ACTION_OPEN_SETTINGS);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_SETTINGS) ==
           APP_PAGE_SETTINGS_HOLD_MS);
    assert(app_page_key_hold_action(APP_PAGE_ONLINE_UPDATE) ==
           APP_PAGE_ACTION_CHECK_ONLINE_UPDATE);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_ONLINE_UPDATE) ==
           APP_PAGE_ONLINE_UPDATE_CHECK_HOLD_MS);
    assert(APP_PAGE_ONLINE_UPDATE_INSTALL_HOLD_MS == 3000U);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_HOME) == 0U);

    assert(app_page_boot_hold_action(APP_PAGE_HOME) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_CALENDAR) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_IMAGE) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_STATUS) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_AUDIO) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_SETTINGS) ==
           APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING);
    assert(app_page_boot_hold_threshold_ms(APP_PAGE_SETTINGS) ==
           APP_PAGE_SETTINGS_POWER_HOLD_MS);
    assert(APP_PAGE_SETTINGS_POWER_HOLD_MS == 2000U);
    assert(app_page_boot_hold_action(APP_PAGE_ONLINE_UPDATE) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_threshold_ms(APP_PAGE_HOME) == 0U);
    assert(app_page_boot_hold_threshold_ms(APP_PAGE_ONLINE_UPDATE) == 0U);

    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_CALENDAR);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_set_image_available(&state, true);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_CALENDAR);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_IMAGE);
    assert(!app_page_state_tick(&state,
                                APP_PAGE_SECONDARY_TIMEOUT_MS - 1U));
    assert(app_page_state_tick(&state, 1U));
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_boot_short_press(&state);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_IMAGE);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_STATUS);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_boot_short_press(&state);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_IMAGE);
    app_page_state_set_image_available(&state, false);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_set_image_available(&state, true);
    app_page_state_go_home(&state);
    app_page_state_boot_short_press(&state);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_IMAGE);
    app_page_state_go_home(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    app_page_state_boot_short_press(&state);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_IMAGE);
    app_page_state_go_home(&state);
    app_page_state_set_image_available(&state, false);

    app_page_state_boot_short_press(&state);
    assert(!app_page_state_tick(&state,
                                APP_PAGE_SECONDARY_TIMEOUT_MS - 1U));
    app_page_state_note_activity(&state);
    assert(!app_page_state_tick(&state,
                                APP_PAGE_SECONDARY_TIMEOUT_MS - 1U));
    assert(app_page_state_tick(&state, 1U));
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_STATUS);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_AUDIO);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_SETTINGS);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_ONLINE_UPDATE);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_STATUS);
    assert(!app_page_state_tick(&state,
                                APP_PAGE_SECONDARY_TIMEOUT_MS - 1U));
    assert(app_page_state_tick(&state, 1U));
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_boot_short_press(&state);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_STATUS);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_init(NULL);
    app_page_state_go_home(NULL);
    app_page_state_set_image_available(NULL, true);
    app_page_state_boot_short_press(NULL);
    app_page_state_key_short_press(NULL);
    app_page_state_note_activity(NULL);
    assert(!app_page_state_tick(NULL, UINT32_MAX));
    assert(app_page_state_current(NULL) == APP_PAGE_HOME);

    puts("page state tests passed");
    return 0;
}
