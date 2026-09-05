#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "button_state.h"
#include "display_interaction_model.h"
#include "quick_settings.h"

static app_settings_t saved;

static void release(quick_settings_t *menu)
{
    assert(quick_settings_release_gate(menu, true));
    assert(quick_settings_release_gate(menu, false));
    assert(!quick_settings_release_gate(menu, false));
}

static quick_settings_action_t input(quick_settings_t *menu,
                                     quick_settings_input_t action)
{
    return quick_settings_input(menu, action, &saved, true, true);
}

static void test_edit_cancel_save_and_latest_values(void)
{
    quick_settings_t menu = {0};
    quick_settings_open(&menu);
    assert(menu.active && !menu.editing && menu.item == QUICK_SETTINGS_VOLUME);
    assert(input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_NONE);
    assert(!menu.editing); /* opening press cannot enter editing */
    release(&menu);
    saved.audio_playback_volume = 68U;
    assert(input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_NONE);
    assert(menu.editing && menu.draft == 68U);
    assert(input(&menu, QUICK_SETTINGS_NEXT) == QUICK_SETTINGS_ACTION_NONE);
    assert(menu.draft == 68U); /* old press still consumed */
    release(&menu);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == 70U && saved.audio_playback_volume == 68U);
    input(&menu, QUICK_SETTINGS_BACK);
    assert(menu.active && !menu.editing && saved.audio_playback_volume == 68U);

    saved.audio_playback_volume = 90U; /* another writer changed the record */
    input(&menu, QUICK_SETTINGS_ACTIVATE);
    assert(menu.draft == 90U);
    release(&menu);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == 100U);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == 0U);
    assert(input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_SAVE);
    app_setting_field_t field;
    uint8_t value;
    assert(quick_settings_save_request(&menu, &field, &value));
    assert(field == APP_SETTING_VOLUME && value == 0U);
    quick_settings_save_result(&menu, false);
    assert(menu.editing && menu.draft == 0U);
    assert(menu.notice == QUICK_SETTINGS_NOTICE_SAVE_FAILED);
    release(&menu);
    assert(input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_SAVE);
    assert(app_settings_set_field(&saved, field, value));
    quick_settings_save_result(&menu, true);
    assert(!menu.editing && menu.notice == QUICK_SETTINGS_NOTICE_SAVED);
    assert(saved.audio_playback_volume == 0U);
    assert(!quick_settings_save_request(&menu, &field, &value));
    release(&menu);

    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.item == QUICK_SETTINGS_ALARM);
    saved.alarm_enabled = false;
    input(&menu, QUICK_SETTINGS_ACTIVATE);
    release(&menu);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == 1U && !saved.alarm_enabled);
    assert(input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_SAVE);
    assert(quick_settings_save_request(&menu, &field, &value));
    assert(field == APP_SETTING_ALARM_ENABLED && value == 1U);
    assert(app_settings_set_field(&saved, field, value));
    assert(saved.alarm_hour == 7U && saved.alarm_minute == 30U);
    quick_settings_save_result(&menu, true);
    release(&menu);
    input(&menu, QUICK_SETTINGS_BACK);
    assert(!menu.active);
}

static void test_display_availability_and_web(void)
{
    quick_settings_t menu;
    quick_settings_open(&menu);
    release(&menu);
    input(&menu, QUICK_SETTINGS_NEXT);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.item == QUICK_SETTINGS_DISPLAY);
    saved.default_display = APP_DEFAULT_DISPLAY_CLOCK;
    input(&menu, QUICK_SETTINGS_ACTIVATE);
    release(&menu);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == APP_DEFAULT_DISPLAY_WEATHER);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == APP_DEFAULT_DISPLAY_IMAGE);
    assert(quick_settings_input(&menu, QUICK_SETTINGS_ACTIVATE,
                                &saved, true, false) == QUICK_SETTINGS_ACTION_NONE);
    assert(menu.editing && menu.notice == QUICK_SETTINGS_NOTICE_UNAVAILABLE);
    release(&menu);
    quick_settings_input(&menu, QUICK_SETTINGS_NEXT, &saved, false, false);
    assert(menu.draft == APP_DEFAULT_DISPLAY_CLOCK);
    quick_settings_input(&menu, QUICK_SETTINGS_NEXT, &saved, false, false);
    assert(menu.draft == APP_DEFAULT_DISPLAY_CLOCK);
    quick_settings_input(&menu, QUICK_SETTINGS_NEXT, &saved, false, true);
    assert(menu.draft == APP_DEFAULT_DISPLAY_IMAGE);
    app_setting_field_t field;
    uint8_t value;
    assert(input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_SAVE);
    assert(quick_settings_save_request(&menu, &field, &value));
    assert(field == APP_SETTING_DEFAULT_DISPLAY && value == APP_DEFAULT_DISPLAY_IMAGE);
    quick_settings_save_result(&menu, true);
    release(&menu);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.item == QUICK_SETTINGS_WEB);
    assert(input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_OPEN_WEB);
    assert(!quick_settings_save_request(&menu, &field, &value));
    release(&menu);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.item == QUICK_SETTINGS_VOLUME);
    assert(quick_settings_input(&menu, QUICK_SETTINGS_ACTIVATE, NULL,
                                true, true) == QUICK_SETTINGS_ACTION_NONE);
    assert(!menu.editing && menu.notice == QUICK_SETTINGS_NOTICE_UNAVAILABLE);
}

static void test_timeout_and_preemption(void)
{
    quick_settings_t menu;
    quick_settings_open(&menu);
    assert(!quick_settings_tick(&menu, UINT32_MAX, false));
    release(&menu);
    input(&menu, QUICK_SETTINGS_ACTIVATE);
    release(&menu);
    input(&menu, QUICK_SETTINGS_NEXT);
    assert(!quick_settings_tick(&menu, 29999U, false));
    assert(!quick_settings_tick(&menu, UINT32_MAX, true));
    assert(!quick_settings_tick(&menu, 29999U, false));
    assert(quick_settings_tick(&menu, 1U, false));
    assert(!menu.active && !menu.editing && menu.draft == 0U);
    assert(!quick_settings_tick(&menu, UINT32_MAX, false));
    quick_settings_open(&menu);
    release(&menu);
    input(&menu, QUICK_SETTINGS_ACTIVATE);
    quick_settings_close(&menu); /* alarm or maintenance preemption */
    assert(!menu.active && !menu.editing && menu.draft == 0U);
    assert(input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_NONE);
    quick_settings_open(&menu);
    release(&menu);
    assert(quick_settings_tick(&menu, UINT32_MAX, false));
}

static void test_real_button_release_and_timing(void)
{
    button_state_t key;
    button_state_init_custom(&key, false, 1000U, 3000U);
    assert(button_state_update(&key, true, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 40U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 2960U) == BUTTON_EVENT_LONG_PRESS);
    quick_settings_t menu;
    quick_settings_open(&menu);
    assert(!button_state_set_action_timing(&key, QUICK_SETTINGS_HOLD_MS));
    assert(button_state_update(&key, true, 5000U) == BUTTON_EVENT_NONE);
    assert(quick_settings_release_gate(&menu, true));
    assert(button_state_update(&key, false, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, false, 40U) == BUTTON_EVENT_NONE);
    assert(quick_settings_release_gate(&menu, false));
    assert(button_state_set_action_timing(&key, QUICK_SETTINGS_HOLD_MS));
    assert(button_state_update(&key, true, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 40U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 960U) == BUTTON_EVENT_NONE);
    assert(button_state_hold_prompt_active(&key));
    assert(button_state_hold_seconds_remaining(&key) == 1U);
    assert(button_state_update(&key, false, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, false, 40U) == BUTTON_EVENT_HOLD_CANCELLED);
    assert(!menu.editing && menu.item == QUICK_SETTINGS_VOLUME);
    assert(button_state_update(&key, true, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 40U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 1960U) == BUTTON_EVENT_LONG_PRESS);
    input(&menu, QUICK_SETTINGS_ACTIVATE);
    assert(menu.editing);
    assert(button_state_update(&key, false, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, false, 40U) == BUTTON_EVENT_NONE);
    assert(quick_settings_release_gate(&menu, false));
}

static void test_copy_and_invalid_arguments(void)
{
    for (unsigned editing = 0U; editing < 2U; ++editing) {
        assert(strlen(display_interaction_quick_navigation(editing != 0U)) <=
               DISPLAY_INTERACTION_FOOTER_MAX_CHARS);
        for (unsigned web = 0U; web < 2U; ++web) {
            assert(strlen(display_interaction_quick_action(editing != 0U, web != 0U)) <=
                   DISPLAY_INTERACTION_FOOTER_MAX_CHARS);
        }
    }
    quick_settings_open(NULL);
    quick_settings_close(NULL);
    assert(!quick_settings_release_gate(NULL, false));
    assert(!quick_settings_tick(NULL, 100U, false));
    assert(!quick_settings_save_request(NULL, NULL, NULL));
    quick_settings_save_result(NULL, true);
    assert(quick_settings_input(NULL, QUICK_SETTINGS_NEXT, &saved, true, true) ==
           QUICK_SETTINGS_ACTION_NONE);
}

int main(void)
{
    app_settings_defaults(&saved);
    test_edit_cancel_save_and_latest_values();
    test_display_availability_and_web();
    test_timeout_and_preemption();
    test_real_button_release_and_timing();
    test_copy_and_invalid_arguments();
    puts("quick settings editing, release gates and timeout tests passed");
}
