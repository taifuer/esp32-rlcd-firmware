#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "button_state.h"
#include "page_state.h"
#include "quick_settings.h"

static void release(quick_settings_t *menu)
{
    assert(quick_settings_release_gate(menu, true));
    assert(quick_settings_release_gate(menu, false));
    assert(!quick_settings_release_gate(menu, false));
}

int main(void)
{
    app_settings_t saved;
    app_settings_defaults(&saved);
    saved.audio_playback_volume = 68U;
    quick_settings_t menu = {0};
    assert(!quick_settings_open_volume(NULL, &saved));
    assert(!quick_settings_open_volume(&menu, NULL));
    assert(quick_settings_open_volume(&menu, &saved));
    assert(menu.active && menu.draft == 68U);
    assert(quick_settings_input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_NONE);
    assert(!quick_settings_tick(&menu, UINT32_MAX, false));
    release(&menu);
    quick_settings_input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == 70U && saved.audio_playback_volume == 68U);
    assert(quick_settings_playback_volume(&menu, 68U) == 70U);
    quick_settings_input(&menu, QUICK_SETTINGS_BACK);
    assert(!menu.active);
    assert(quick_settings_playback_volume(&menu, 42U) == 42U);

    saved.audio_playback_volume = 90U; /* reopen reads current, not old draft */
    assert(quick_settings_open_volume(&menu, &saved));
    release(&menu);
    quick_settings_input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == 100U);
    quick_settings_input(&menu, QUICK_SETTINGS_NEXT);
    assert(menu.draft == 0U);
    assert(quick_settings_input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_SAVE);
    app_setting_field_t field;
    uint8_t value;
    assert(quick_settings_save_request(&menu, &field, &value));
    assert(field == APP_SETTING_VOLUME && value == 0U);
    quick_settings_save_result(&menu, false);
    assert(menu.active && menu.notice == QUICK_SETTINGS_NOTICE_SAVE_FAILED);
    assert(quick_settings_playback_volume(&menu, 90U) == 0U);
    release(&menu);
    assert(quick_settings_input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_SAVE);
    assert(app_settings_set_field(&saved, field, value));
    quick_settings_save_result(&menu, true);
    assert(!menu.active && saved.audio_playback_volume == 0U);
    assert(!quick_settings_save_request(&menu, &field, &value));
    assert(saved.alarm_volume == APP_SETTINGS_DEFAULT_AUDIO_PLAYBACK_VOLUME);
    assert(saved.update_channel == APP_UPDATE_CHANNEL_STABLE);
    assert(saved.alarm_hour == 7U && saved.alarm_minute == 30U);

    assert(quick_settings_open_volume(&menu, &saved));
    release(&menu);
    quick_settings_input(&menu, QUICK_SETTINGS_NEXT);
    assert(!quick_settings_tick(&menu, 29999U, false));
    assert(!quick_settings_tick(&menu, UINT32_MAX, true));
    assert(!quick_settings_tick(&menu, 29999U, false));
    assert(quick_settings_tick(&menu, 1U, false));
    assert(!menu.active && saved.audio_playback_volume == 0U);
    assert(quick_settings_playback_volume(&menu, 23U) == 23U);
    assert(quick_settings_open_volume(&menu, &saved));
    release(&menu);
    assert(quick_settings_tick(&menu, UINT32_MAX, false));
    assert(quick_settings_open_volume(&menu, &saved));
    quick_settings_close(&menu); /* alarm/maintenance preemption */
    assert(!menu.active && quick_settings_playback_volume(&menu, 31U) == 31U);

    /* A long opening press is consumed; release never saves or navigates. */
    button_state_t key;
    button_state_init_custom(&key, false, 1000U, APP_PAGE_SETTINGS_HOLD_MS);
    assert(button_state_update(&key, true, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 40U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 1960U) == BUTTON_EVENT_LONG_PRESS);
    assert(quick_settings_open_volume(&menu, &saved));
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
    assert(button_state_update(&key, false, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, false, 40U) == BUTTON_EVENT_HOLD_CANCELLED);
    assert(menu.active && menu.draft == saved.audio_playback_volume);
    assert(button_state_update(&key, true, 20U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 40U) == BUTTON_EVENT_NONE);
    assert(button_state_update(&key, true, 1960U) == BUTTON_EVENT_LONG_PRESS);
    assert(quick_settings_input(&menu, QUICK_SETTINGS_ACTIVATE) == QUICK_SETTINGS_ACTION_SAVE);
    assert(strcmp(quick_settings_hold_title(), "SAVE VOLUME") == 0);
    quick_settings_close(NULL);
    assert(!quick_settings_tick(NULL, 1U, false));
    assert(quick_settings_input(NULL, QUICK_SETTINGS_NEXT) == QUICK_SETTINGS_ACTION_NONE);
    puts("contextual music volume tests passed");
}
