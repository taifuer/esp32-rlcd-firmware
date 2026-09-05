#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "button_state.h"
#include "page_state.h"

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

int main(void)
{
    app_page_state_t page;
    app_page_state_init(&page);
    app_page_state_set_weather_enabled(&page, true);
    assert(app_page_state_open_page(&page, APP_PAGE_WEATHER));
    assert(app_page_key_hold_action(APP_PAGE_WEATHER) ==
           APP_PAGE_ACTION_REFRESH_WEATHER);

    button_state_t key;
    button_state_init(&key, false);
    assert(button_state_set_action_timing(
        &key, app_page_key_hold_threshold_ms(APP_PAGE_WEATHER)));
    settle_released(&key);

    assert(update_for(&key, true, 1040U) == BUTTON_EVENT_NONE);
    assert(button_state_hold_prompt_active(&key));
    assert(button_state_hold_seconds_remaining(&key) == 1U);
    assert(update_for(&key, false, 60U) ==
           BUTTON_EVENT_HOLD_CANCELLED);
    assert(app_page_state_current(&page) == APP_PAGE_WEATHER);

    assert(update_for(&key, true, 2040U) == BUTTON_EVENT_LONG_PRESS);
    assert(update_for(&key, false, 60U) == BUTTON_EVENT_NONE);
    assert(app_page_state_current(&page) == APP_PAGE_WEATHER);

    assert(update_for(&key, true, 300U) == BUTTON_EVENT_NONE);
    assert(update_for(&key, false, 60U) == BUTTON_EVENT_SHORT_PRESS);
    app_page_state_key_short_press(&page);
    assert(app_page_state_current(&page) == APP_PAGE_VOICE);

    assert(app_page_state_open_page(&page, APP_PAGE_WEATHER));
    app_page_state_boot_short_press(&page);
    assert(app_page_state_current(&page) == APP_PAGE_CALENDAR);

    puts("weather refresh interaction tests passed");
    return 0;
}
