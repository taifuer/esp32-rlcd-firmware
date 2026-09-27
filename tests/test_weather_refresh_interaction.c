#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "button_state.h"
#include "page_state.h"
#include "settings_model.h"

static button_event_t update_for(button_state_t *button, bool pressed,
                                 uint32_t duration_ms)
{
    button_event_t event = BUTTON_EVENT_NONE;
    for (uint32_t elapsed = 0U; elapsed < duration_ms; elapsed += 10U) {
        const button_event_t current =
            button_state_update(button, pressed, 10U);
        if (current != BUTTON_EVENT_NONE) {
            assert(event == BUTTON_EVENT_NONE);
            event = current;
        }
    }
    return event;
}

static void settle_released(button_state_t *button)
{
    assert(update_for(button, false, 100U) == BUTTON_EVENT_NONE);
}

static void test_refresh_page(app_page_t target, app_page_action_t action)
{
    app_page_state_t page;
    app_page_state_init(&page);
    app_page_state_set_weather_enabled(&page, true);
    app_page_state_set_market_enabled(&page, true);
    assert(app_page_state_open_page(&page, target));
    assert(app_page_key_hold_action(target) == action);

    button_state_t key;
    button_state_init(&key, false);
    assert(button_state_set_action_timing(
        &key, app_page_key_hold_threshold_ms(target)));
    settle_released(&key);

    assert(update_for(&key, true, 1040U) == BUTTON_EVENT_NONE);
    assert(button_state_hold_prompt_active(&key));
    assert(button_state_hold_seconds_remaining(&key) == 1U);
    assert(update_for(&key, false, 60U) ==
           BUTTON_EVENT_HOLD_CANCELLED);
    assert(app_page_state_current(&page) == target);

    assert(update_for(&key, true, 2040U) == BUTTON_EVENT_LONG_PRESS);
    assert(update_for(&key, false, 60U) == BUTTON_EVENT_NONE);
    assert(app_page_state_current(&page) == target);

    assert(update_for(&key, true, 300U) == BUTTON_EVENT_NONE);
    assert(update_for(&key, false, 60U) == BUTTON_EVENT_SHORT_PRESS);
    app_page_state_key_short_press(&page);
    assert(app_page_state_current(&page) == APP_PAGE_SETTINGS);

    assert(app_page_state_open_page(&page, target));
    app_page_state_boot_short_press(&page);
    assert(app_page_state_current(&page) ==
           (target == APP_PAGE_WEATHER ? APP_PAGE_MARKET : APP_PAGE_CALENDAR));
}

static void test_hidden_page_cannot_continue_a_hold(void)
{
    const app_page_t targets[] = {APP_PAGE_WEATHER, APP_PAGE_MARKET,
        APP_PAGE_IMAGE, APP_PAGE_MUSIC, APP_PAGE_VOICE};
    for (size_t i = 0U; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        app_page_state_t page;
        app_page_state_init(&page);
        app_page_state_set_weather_enabled(&page, true);
        app_page_state_set_market_enabled(&page, true);
        app_page_state_set_image_available(&page, true);
        app_page_state_set_music_available(&page, true);
        assert(app_page_state_open_page(&page, targets[i]));
        button_state_t key;
        button_state_init(&key, false);
        assert(button_state_set_action_timing(
            &key, app_page_key_hold_threshold_ms(targets[i])));
        settle_released(&key);
        assert(update_for(&key, true, 1040U) == BUTTON_EVENT_NONE);
        assert(button_state_hold_prompt_active(&key));
        app_page_state_set_daily_pages_mask(&page, 0U);
        assert(app_page_state_current(&page) == APP_PAGE_HOME);
        /* Timing stays locked until release, but actions are resolved against
         * the new current page, just like the application input loop. */
        (void)button_state_set_action_timing(&key,
            app_page_key_hold_threshold_ms(app_page_state_current(&page)));
        const button_event_t event = update_for(&key, true, 1100U);
        assert(event == BUTTON_EVENT_LONG_PRESS || event == BUTTON_EVENT_NONE);
        assert(app_page_key_hold_action(app_page_state_current(&page)) == APP_PAGE_ACTION_NONE);
        assert(update_for(&key, false, 60U) != BUTTON_EVENT_SHORT_PRESS);
        app_page_state_boot_short_press(&page);
        assert(app_page_state_current(&page) == APP_PAGE_HOME);
        assert(!app_page_state_open_page(&page, targets[i]));
        app_page_state_set_daily_pages_mask(&page, APP_DAILY_PAGES_ALL_MASK);
        assert(app_page_state_current(&page) == APP_PAGE_HOME);
    }
}

int main(void)
{
    test_refresh_page(APP_PAGE_WEATHER, APP_PAGE_ACTION_REFRESH_WEATHER);
    test_refresh_page(APP_PAGE_MARKET, APP_PAGE_ACTION_REFRESH_MARKET);
    test_hidden_page_cannot_continue_a_hold();
    puts("weather and market refresh interaction tests passed");
    return 0;
}
