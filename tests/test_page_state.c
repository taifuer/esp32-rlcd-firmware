#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "page_state.h"
#include "settings_model.h"
#include "display_interaction_model.h"

static void assert_footer_destination(app_page_t page, app_page_t next)
{
    const char *target = app_page_daily_label(next);
    char footer[64];
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "BOOT: %s |", target);
    if (page == APP_PAGE_WEATHER) {
        assert(display_interaction_format_weather_footer(footer, sizeof(footer), target));
    } else if (page == APP_PAGE_IMAGE) {
        assert(display_interaction_format_image_navigation(footer, sizeof(footer), 31U, 32U, target));
    } else if (page == APP_PAGE_MUSIC) {
        assert(display_interaction_format_music_navigation(footer, sizeof(footer), target, true));
    } else {
        assert(display_interaction_format_daily_navigation(footer, sizeof(footer), target));
    }
    assert(strncmp(footer, prefix, strlen(prefix)) == 0);
    assert(strlen(footer) <= DISPLAY_INTERACTION_FOOTER_MAX_CHARS);
}

static unsigned tested_orders;

static app_page_t page_for_id(uint8_t id)
{
    switch (id) {
    case APP_DAILY_PAGE_WEATHER: return APP_PAGE_WEATHER;
    case APP_DAILY_PAGE_MARKET: return APP_PAGE_MARKET;
    case APP_DAILY_PAGE_CALENDAR: return APP_PAGE_CALENDAR;
    case APP_DAILY_PAGE_IMAGE: return APP_PAGE_IMAGE;
    case APP_DAILY_PAGE_MUSIC: return APP_PAGE_MUSIC;
    case APP_DAILY_PAGE_CHAT: return APP_PAGE_VOICE;
    case APP_DAILY_PAGE_TIMER: return APP_PAGE_TIMER;
    default: assert(false); return APP_PAGE_HOME;
    }
}

static void assert_ordered_ring(const uint8_t *order)
{
    for (unsigned mask = 0U; mask <= APP_DAILY_PAGES_ALL_MASK; ++mask) {
        for (unsigned flags = 0U; flags < 16U; ++flags) {
            app_page_state_t state;
            app_page_state_init(&state);
            app_page_state_set_weather_enabled(&state, (flags & 1U) != 0U);
            app_page_state_set_market_enabled(&state, (flags & 2U) != 0U);
            app_page_state_set_image_available(&state, (flags & 4U) != 0U);
            app_page_state_set_music_available(&state, (flags & 8U) != 0U);
            app_page_state_set_daily_pages_mask(&state, (uint8_t)mask);
            assert(app_page_state_set_daily_pages_order(&state, order));
            assert(app_page_state_current(&state) == APP_PAGE_HOME);
            app_page_t ring[APP_DAILY_PAGE_COUNT + 1U] = {APP_PAGE_HOME};
            size_t count = 1U;
            for (size_t i = 0U; i < APP_DAILY_PAGE_COUNT; ++i) {
                const app_page_t page = page_for_id(order[i]);
                if (app_page_state_page_available(&state, page)) ring[count++] = page;
            }
            for (size_t i = 0U; i < count; ++i) {
                const app_page_t next = ring[(i + 1U) % count];
                assert(app_page_state_current(&state) == ring[i]);
                assert(app_page_state_next_daily_page(&state) == next);
                assert_footer_destination(ring[i], next);
                app_page_state_boot_short_press(&state);
                assert(app_page_state_current(&state) == next);
            }
        }
    }
}

static void test_page_permutations(uint8_t *order, size_t position)
{
    if (position == APP_DAILY_PAGE_COUNT) {
        assert_ordered_ring(order);
        ++tested_orders;
        return;
    }
    for (size_t i = position; i < APP_DAILY_PAGE_COUNT; ++i) {
        const uint8_t saved = order[position];
        order[position] = order[i];
        order[i] = saved;
        test_page_permutations(order, position + 1U);
        order[i] = order[position];
        order[position] = saved;
    }
}

static void test_reordering_preserves_current_and_rejects_invalid(void)
{
    app_page_state_t state;
    app_page_state_init(&state);
    app_page_state_set_weather_enabled(&state, true);
    app_page_state_set_market_enabled(&state, true);
    app_page_state_set_image_available(&state, true);
    app_page_state_set_music_available(&state, true);
    const uint8_t order[] = {APP_DAILY_PAGE_CHAT, APP_DAILY_PAGE_MUSIC,
        APP_DAILY_PAGE_IMAGE, APP_DAILY_PAGE_CALENDAR, APP_DAILY_PAGE_MARKET,
        APP_DAILY_PAGE_WEATHER, APP_DAILY_PAGE_TIMER};
    for (app_page_t page = APP_PAGE_HOME; page <= APP_PAGE_TIMER; ++page) {
        /* Voice navigation and modal restore target IDs, not old positions. */
        assert(app_page_state_open_page(&state, page));
        assert(app_page_state_set_daily_pages_order(&state, order));
        assert(app_page_state_current(&state) == page);
        assert(app_page_state_open_page(&state, page));
    }
    assert(app_page_state_open_page(&state, APP_PAGE_CALENDAR));
    const app_page_state_t before = state;
    for (size_t i = 0U; i < APP_DAILY_PAGE_COUNT; ++i) {
        for (unsigned id = 0U; id <= UINT8_MAX; ++id) {
            uint8_t invalid[APP_DAILY_PAGE_COUNT];
            memcpy(invalid, order, sizeof(invalid));
            invalid[i] = (uint8_t)id;
            assert(app_page_state_set_daily_pages_order(&state, invalid) == (id == order[i]));
            assert(memcmp(&state, &before, sizeof(state)) == 0);
        }
    }
    assert(!app_page_state_set_daily_pages_order(NULL, order));
    assert(!app_page_state_set_daily_pages_order(&state, NULL));
    assert(app_page_state_set_daily_pages_order(&state, state.daily_pages_order));
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    app_page_state_set_daily_pages_mask(&state, APP_DAILY_PAGE_CHAT);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    assert(!app_page_state_open_page(&state, APP_PAGE_CALENDAR));
    assert(app_page_state_next_daily_page(&state) == APP_PAGE_VOICE);
    app_page_state_set_daily_pages_mask(&state, APP_DAILY_PAGES_ALL_MASK);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    app_page_state_set_recovery_mode(&state, true);
    assert(app_page_state_set_daily_pages_order(&state, order));
    assert(app_page_state_current(&state) == APP_PAGE_ONLINE_UPDATE);
    assert(app_page_state_next_daily_page(&state) == APP_PAGE_ONLINE_UPDATE);
    app_page_state_set_recovery_mode(&state, false);
    assert(app_page_state_current(&state) == APP_PAGE_HOME);
    assert(app_page_state_next_daily_page(&state) == APP_PAGE_VOICE);
}

static void test_every_visibility_and_availability_combination(void)
{
    const app_page_t daily[] = {APP_PAGE_HOME, APP_PAGE_WEATHER, APP_PAGE_MARKET,
        APP_PAGE_CALENDAR, APP_PAGE_TIMER, APP_PAGE_IMAGE, APP_PAGE_MUSIC, APP_PAGE_VOICE};
    const app_page_t system[] = {APP_PAGE_SETTINGS, APP_PAGE_ONLINE_UPDATE, APP_PAGE_STATUS};
    for (unsigned mask = 0U; mask <= APP_DAILY_PAGES_ALL_MASK; ++mask) {
        for (unsigned flags = 0U; flags < 16U; ++flags) {
            app_page_state_t state;
            app_page_state_init(&state);
            assert(state.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
            app_page_state_set_weather_enabled(&state, (flags & 1U) != 0U);
            app_page_state_set_market_enabled(&state, (flags & 2U) != 0U);
            app_page_state_set_image_available(&state, (flags & 4U) != 0U);
            app_page_state_set_music_available(&state, (flags & 8U) != 0U);
            app_page_state_set_daily_pages_mask(&state, (uint8_t)mask);
            const bool available[] = {true,
                (mask & APP_DAILY_PAGE_WEATHER) && (flags & 1U),
                (mask & APP_DAILY_PAGE_MARKET) && (flags & 2U),
                (mask & APP_DAILY_PAGE_CALENDAR) != 0U,
                (mask & APP_DAILY_PAGE_TIMER) != 0U,
                (mask & APP_DAILY_PAGE_IMAGE) && (flags & 4U),
                (mask & APP_DAILY_PAGE_MUSIC) && (flags & 8U),
                (mask & APP_DAILY_PAGE_CHAT) != 0U};
            app_page_t ring[APP_DAILY_PAGE_COUNT + 1U];
            size_t count = 0U;
            for (size_t i = 0U; i < APP_DAILY_PAGE_COUNT + 1U; ++i) {
                assert(app_page_state_page_available(&state, daily[i]) == available[i]);
                if (available[i]) ring[count++] = daily[i];
            }
            /* Three complete bounded cycles cover the all-hidden HOME loop. */
            for (unsigned repeat = 0U; repeat < 3U; ++repeat) {
                for (size_t i = 0U; i < count; ++i) {
                    assert(app_page_state_current(&state) == ring[i]);
                    const app_page_t next = ring[(i + 1U) % count];
                    assert(app_page_state_next_daily_page(&state) == next);
                    assert_footer_destination(ring[i], next);
                    app_page_state_boot_short_press(&state);
                    assert(app_page_state_current(&state) == next);
                }
            }
            for (size_t i = 0U; i < APP_DAILY_PAGE_COUNT + 1U; ++i) {
                app_page_state_go_home(&state);
                /* Voice and late asynchronous requests use this same guard. */
                assert(app_page_state_open_page(&state, daily[i]) == available[i]);
                assert(app_page_state_current(&state) == (available[i] ? daily[i] : APP_PAGE_HOME));
                if (!available[i]) continue;
                /* Modal overlays don't copy/restore stale page state. Hide
                 * the underlying destination while covered, then return. */
                app_page_state_set_daily_pages_mask(&state, 0U);
                assert(app_page_state_current(&state) == APP_PAGE_HOME);
                assert(state.weather_enabled == ((flags & 1U) != 0U));
                assert(state.market_enabled == ((flags & 2U) != 0U));
                assert(state.image_available == ((flags & 4U) != 0U));
                assert(state.music_available == ((flags & 8U) != 0U));
                assert(app_page_state_open_page(&state, daily[i]) == (i == 0U));
                app_page_state_set_daily_pages_mask(&state, (uint8_t)mask);
                assert(app_page_state_current(&state) == APP_PAGE_HOME);
                assert(app_page_state_open_page(&state, daily[i]));
                app_page_state_key_short_press(&state);
                assert(app_page_state_current(&state) ==
                       (daily[i] == APP_PAGE_TIMER ? APP_PAGE_TIMER : APP_PAGE_SETTINGS));
            }
            for (size_t i = 0U; i < 3U; ++i) {
                assert(app_page_state_open_page(&state, system[i]));
                app_page_state_set_daily_pages_mask(&state, 0U);
                assert(app_page_state_current(&state) == system[i]);
                assert(app_page_state_next_daily_page(&state) == APP_PAGE_HOME);
                app_page_state_key_short_press(&state);
                assert(app_page_state_current(&state) == system[(i + 1U) % 3U]);
                app_page_state_boot_short_press(&state);
                assert(app_page_state_current(&state) == APP_PAGE_HOME);
            }
            app_page_state_set_recovery_mode(&state, true);
            app_page_state_set_daily_pages_mask(&state, (uint8_t)mask);
            for (size_t i = 0U; i < APP_DAILY_PAGE_COUNT + 1U; ++i) assert(!app_page_state_open_page(&state, daily[i]));
            for (unsigned i = 0U; i < 4U; ++i) {
                const app_page_t before = app_page_state_current(&state);
                assert(app_page_state_next_daily_page(&state) == before);
                app_page_state_boot_short_press(&state);
                assert(app_page_state_current(&state) == before);
                app_page_state_key_short_press(&state);
                assert(app_page_state_current(&state) != before);
            }
            app_page_state_set_recovery_mode(&state, false);
            assert(app_page_state_current(&state) == APP_PAGE_HOME);
        }
    }
}

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
    for (app_page_t page = APP_PAGE_HOME; page <= APP_PAGE_TIMER; ++page) {
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
    uint8_t order[] = APP_DAILY_PAGES_DEFAULT_ORDER;
    test_page_permutations(order, 0U);
    assert(tested_orders == 5040U);
    test_reordering_preserves_current_and_rejects_invalid();
    test_every_visibility_and_availability_combination();
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
    assert(APP_PAGE_TIMER == 10); /* All existing enum IDs remain unchanged. */
    assert(app_page_is_daily(APP_PAGE_TIMER));
    assert(!app_page_is_system(APP_PAGE_TIMER));
    assert(strcmp(app_page_daily_label(APP_PAGE_TIMER), "TIMER") == 0);
    assert(app_page_key_hold_action(APP_PAGE_TIMER) == APP_PAGE_ACTION_TIMER_RESET);
    assert(app_page_key_hold_threshold_ms(APP_PAGE_TIMER) == 2000U);
    assert(app_page_boot_hold_action(APP_PAGE_TIMER) == APP_PAGE_ACTION_TIMER_MODE);
    assert(app_page_boot_hold_threshold_ms(APP_PAGE_TIMER) == 2000U);
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
        app_page_t ring[APP_DAILY_PAGE_COUNT + 1U] = {APP_PAGE_HOME};
        unsigned count = 1U;
        if (flags & 1U) ring[count++] = APP_PAGE_WEATHER;
        if (flags & 8U) ring[count++] = APP_PAGE_MARKET;
        ring[count++] = APP_PAGE_CALENDAR;
        ring[count++] = APP_PAGE_TIMER;
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
            assert(app_page_state_current(&state) ==
                   (ring[index] == APP_PAGE_TIMER ? APP_PAGE_TIMER : APP_PAGE_SETTINGS));
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
    assert(!app_page_state_open_page(&state, (app_page_t)(APP_PAGE_TIMER + 1)));
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
    app_page_state_set_daily_pages_mask(NULL, 0U);
    assert(!app_page_state_page_available(NULL, APP_PAGE_HOME));
    assert(app_page_state_next_daily_page(NULL) == APP_PAGE_HOME);
    app_page_state_set_daily_pages_mask(&state, UINT8_MAX);
    assert(state.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
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
