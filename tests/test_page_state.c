#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "page_state.h"

static void test_browsing_stays_and_background_discovery_never_navigates(void)
{
    app_page_state_t state;
    app_page_state_init(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    app_page_state_set_weather_enabled(&state, true);
    app_page_state_set_market_enabled(&state, true);
    app_page_state_set_image_available(&state, true);
    app_page_state_set_music_available(&state, true);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    /* There is intentionally no clock/tick API in the page model. Repeated
     * background availability updates must also preserve every chosen page. */
    for (app_page_t page = APP_PAGE_HOME; page <= APP_PAGE_ONLINE_UPDATE; ++page) {
        assert(app_page_state_open_page(&state, page));
        for (unsigned poll = 0U; poll < 3600U; ++poll) {
            app_page_state_set_weather_enabled(&state, true);
            app_page_state_set_market_enabled(&state, true);
            app_page_state_set_image_available(&state, true);
            app_page_state_set_music_available(&state, true);
            assert(app_page_state_current(&state) == page);
        }
        app_page_state_init(&state); /* reboot does not remember this page */
        assert(app_page_state_current(&state) == APP_PAGE_HOME);
        app_page_state_set_weather_enabled(&state, true);
        app_page_state_set_market_enabled(&state, true);
        app_page_state_set_image_available(&state, true);
        app_page_state_set_music_available(&state, true);
    }
    assert(app_page_state_open_page(&state, APP_PAGE_CALENDAR));
    app_page_state_set_image_available(&state, false);
    app_page_state_set_weather_enabled(&state, false);
    app_page_state_set_image_available(&state, true);
    app_page_state_set_weather_enabled(&state, true);
    assert(app_page_state_current(&state) == APP_PAGE_CALENDAR);
    app_page_state_set_recovery_mode(&state, true);
    app_page_state_set_image_available(&state, true);
    assert(app_page_state_current(&state) == APP_PAGE_ONLINE_UPDATE);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_SETTINGS);
}

int main(void)
{
    test_browsing_stays_and_background_discovery_never_navigates();
    app_page_state_t state;
    app_page_state_init(&state);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    assert(app_page_is_daily(APP_PAGE_HOME));
    assert(app_page_is_daily(APP_PAGE_WEATHER));
    assert(app_page_is_daily(APP_PAGE_MARKET));
    assert(!app_page_is_system(APP_PAGE_MARKET));
    assert(!app_page_state_open_page(&state, APP_PAGE_MARKET));
    assert(app_page_key_hold_action(APP_PAGE_MARKET) == APP_PAGE_ACTION_REFRESH_MARKET);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_MARKET) == 2000U);
    assert(app_page_boot_hold_action(APP_PAGE_MARKET) == APP_PAGE_ACTION_NONE);
    assert(app_page_is_daily(APP_PAGE_CALENDAR));
    assert(app_page_is_daily(APP_PAGE_IMAGE));
    assert(!app_page_is_daily(APP_PAGE_STATUS));
    assert(app_page_is_system(APP_PAGE_STATUS));
    assert(!app_page_is_system(APP_PAGE_VOICE));
    assert(app_page_is_daily(APP_PAGE_VOICE));
    assert(app_page_is_system(APP_PAGE_SETTINGS));
    assert(app_page_is_system(APP_PAGE_ONLINE_UPDATE));
    assert(!app_page_is_system(APP_PAGE_HOME));
    assert(!app_page_is_system(APP_PAGE_IMAGE));
    assert(app_page_key_hold_action(APP_PAGE_HOME) == APP_PAGE_ACTION_NONE);
    assert(app_page_key_hold_action(APP_PAGE_WEATHER) ==
           APP_PAGE_ACTION_REFRESH_WEATHER);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_WEATHER) ==
           APP_PAGE_WEATHER_REFRESH_HOLD_MS);
    assert(APP_PAGE_WEATHER_REFRESH_HOLD_MS == 2000U);
    assert(app_page_key_hold_action(APP_PAGE_CALENDAR) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_key_hold_action(APP_PAGE_IMAGE) ==
           APP_PAGE_ACTION_DELETE_IMAGE);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_IMAGE) ==
           APP_PAGE_IMAGE_DELETE_HOLD_MS);
    assert(APP_PAGE_IMAGE_DELETE_HOLD_MS == 2000U);
    assert(app_page_key_hold_action(APP_PAGE_STATUS) ==
           APP_PAGE_ACTION_SYNC_TIME);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_STATUS) ==
           APP_PAGE_MANUAL_SYNC_HOLD_MS);
    assert(APP_PAGE_MANUAL_SYNC_HOLD_MS == 2000U);
    assert(app_page_key_hold_action(APP_PAGE_VOICE) ==
           APP_PAGE_ACTION_START_VOICE);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_VOICE) ==
           APP_PAGE_VOICE_HOLD_MS);
    assert(APP_PAGE_VOICE_HOLD_MS == 2000U);
    assert(app_page_key_hold_action(APP_PAGE_SETTINGS) ==
           APP_PAGE_ACTION_OPEN_SETTINGS);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_SETTINGS) ==
           APP_PAGE_SETTINGS_HOLD_MS);
    assert(APP_PAGE_SETTINGS_HOLD_MS == 2000U);
    assert(APP_PAGE_RECOVERY_SETTINGS_HOLD_MS == 3000U);
    assert(app_page_is_daily(APP_PAGE_MUSIC));
    assert(!app_page_is_system(APP_PAGE_MUSIC));
    assert(app_page_key_hold_action(APP_PAGE_MUSIC) == APP_PAGE_ACTION_NEXT_TRACK);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_MUSIC) == 2000U);
    assert(app_page_boot_hold_action(APP_PAGE_MUSIC) == APP_PAGE_ACTION_MUSIC_VOLUME);
    assert(app_page_boot_hold_threshold_ms(APP_PAGE_MUSIC) == 2000U);
    assert(app_page_key_hold_action(APP_PAGE_ONLINE_UPDATE) ==
           APP_PAGE_ACTION_CHECK_ONLINE_UPDATE);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_ONLINE_UPDATE) ==
           APP_PAGE_ONLINE_UPDATE_CHECK_HOLD_MS);
    assert(APP_PAGE_ONLINE_UPDATE_CHECK_HOLD_MS == 2000U);
    assert(APP_PAGE_ONLINE_UPDATE_INSTALL_HOLD_MS == 3000U);
    assert(app_page_online_update_hold_threshold_ms(false) == 2000U);
    assert(app_page_online_update_hold_threshold_ms(true) == 3000U);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_HOME) == 0U);

    assert(app_page_boot_hold_action(APP_PAGE_HOME) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_WEATHER) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_CALENDAR) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_IMAGE) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_STATUS) ==
           APP_PAGE_ACTION_NONE);
    assert(app_page_boot_hold_action(APP_PAGE_VOICE) ==
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

    /* All availability combinations keep Chat in the daily ring, last. */
    for (unsigned flags = 0U; flags < 16U; ++flags) {
        app_page_state_init(&state);
        app_page_state_set_weather_enabled(&state, (flags & 1U) != 0U);
        app_page_state_set_image_available(&state, (flags & 2U) != 0U);
        app_page_state_set_music_available(&state, (flags & 4U) != 0U);
        app_page_state_set_market_enabled(&state, (flags & 8U) != 0U);
        app_page_t ring[7] = {APP_PAGE_HOME};
        unsigned count = 1U;
        if (flags & 1U) ring[count++] = APP_PAGE_WEATHER;
        if (flags & 8U) ring[count++] = APP_PAGE_MARKET;
        ring[count++] = APP_PAGE_CALENDAR;
        if (flags & 2U) ring[count++] = APP_PAGE_IMAGE;
        if (flags & 4U) ring[count++] = APP_PAGE_MUSIC;
        ring[count++] = APP_PAGE_VOICE;
        for (unsigned repeat = 0U; repeat < 3U; ++repeat)
            for (unsigned index = 0U; index < count; ++index) {
                assert(app_page_state_current(&state) == ring[index]);
                assert(app_page_is_daily(ring[index]));
                assert(!app_page_is_system(ring[index]));
                app_page_state_boot_short_press(&state);
            }
        for (unsigned index = 0U; index < count; ++index) {
            assert(app_page_state_open_page(&state, ring[index]));
            app_page_state_key_short_press(&state);
            assert(app_page_state_current(&state) == APP_PAGE_SETTINGS);
        }
        const app_page_t system[] = {APP_PAGE_SETTINGS, APP_PAGE_ONLINE_UPDATE, APP_PAGE_STATUS};
        for (unsigned repeat = 0U; repeat < 3U; ++repeat)
            for (unsigned index = 0U; index < 3U; ++index) {
                assert(app_page_state_current(&state) == system[index]);
                assert(app_page_is_system(system[index]));
                app_page_state_boot_short_press(&state);
                assert(app_page_state_current(&state) == APP_PAGE_HOME);
                assert(app_page_state_open_page(&state, system[index]));
                app_page_state_key_short_press(&state);
            }
    }
    assert(!app_page_state_open_page(&state, (app_page_t)-1));
    assert(!app_page_state_open_page(&state, (app_page_t)(APP_PAGE_ONLINE_UPDATE + 1)));
    assert(!app_page_is_system((app_page_t)-1));
    assert(!app_page_is_daily((app_page_t)-1));
    assert(app_page_state_open_page(&state, APP_PAGE_MARKET));
    /* Polling, failed downloads and unrelated availability changes cannot
     * navigate away. Only an explicit disable removes this daily page. */
    for (unsigned i = 0U; i < 100U; ++i) {
        app_page_state_set_market_enabled(&state, true);
        assert(app_page_state_current(&state) == APP_PAGE_MARKET);
    }
    app_page_state_set_market_enabled(&state, false);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    assert(!app_page_state_open_page(&state, APP_PAGE_MARKET));
    assert(app_page_state_open_page(&state, APP_PAGE_WEATHER));
    app_page_state_set_weather_enabled(&state, false);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    assert(!app_page_state_open_page(&state, APP_PAGE_WEATHER));
    assert(app_page_state_open_page(&state, APP_PAGE_IMAGE));
    app_page_state_set_image_available(&state, false);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    assert(!app_page_state_open_page(&state, APP_PAGE_IMAGE));
    assert(app_page_state_open_page(&state, APP_PAGE_MUSIC));
    app_page_state_set_music_available(&state, false);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    assert(!app_page_state_open_page(&state, APP_PAGE_MUSIC));
    app_page_state_set_recovery_mode(&state, true);
    assert(state.recovery_mode);
    assert(app_page_state_current(&state) == APP_PAGE_ONLINE_UPDATE);
    assert(!app_page_state_open_page(&state, APP_PAGE_HOME));
    assert(!app_page_state_open_page(&state, APP_PAGE_WEATHER));
    assert(!app_page_state_open_page(&state, APP_PAGE_CALENDAR));
    app_page_state_set_market_enabled(&state, true);
    assert(!app_page_state_open_page(&state, APP_PAGE_MARKET));
    assert(!app_page_state_open_page(&state, APP_PAGE_IMAGE));
    assert(!app_page_state_open_page(&state, APP_PAGE_MUSIC));
    assert(!app_page_state_open_page(&state, APP_PAGE_STATUS));
    assert(!app_page_state_open_page(&state, APP_PAGE_VOICE));
    assert(app_page_state_open_page(&state, APP_PAGE_SETTINGS));
    assert(app_page_state_current(&state) == APP_PAGE_SETTINGS);
    app_page_state_boot_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_SETTINGS);
    app_page_state_go_home(&state);
    assert(app_page_state_current(&state) == APP_PAGE_SETTINGS);
    assert(app_page_state_current(&state) == APP_PAGE_SETTINGS);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_ONLINE_UPDATE);
    app_page_state_key_short_press(&state);
    assert(app_page_state_current(&state) == APP_PAGE_SETTINGS);
    assert(app_page_state_open_page(&state, APP_PAGE_ONLINE_UPDATE));
    app_page_state_set_recovery_mode(&state, false);
    assert(!state.recovery_mode);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);

    app_page_state_init(NULL);
    app_page_state_go_home(NULL);
    assert(!app_page_state_open_page(NULL, APP_PAGE_HOME));
    app_page_state_set_weather_enabled(NULL, true);
    app_page_state_set_market_enabled(NULL, true);
    app_page_state_set_image_available(NULL, true);
    app_page_state_set_recovery_mode(NULL, true);
    app_page_state_boot_short_press(NULL);
    app_page_state_key_short_press(NULL);
    assert(app_page_state_current(NULL) == APP_PAGE_HOME);

    puts("page state tests passed");
    return 0;
}
