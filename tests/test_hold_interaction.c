#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hold_interaction.h"

static app_hold_prompt_context_t available_context(void)
{
    return (app_hold_prompt_context_t) {
        .buttons_ready = true,
        .manual_sync_idle = true,
        .weather_refresh_available = true,
        .image_delete_available = true,
    };
}

static void assert_common_gate_blocks(
    const app_hold_prompt_context_t *context)
{
    assert(!app_hold_prompt_allowed(
        context, APP_PAGE_ACTION_START_VOICE, false));
    assert(!app_hold_prompt_allowed(
        context, APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING, false));
}

static void test_all_actions_and_other_button_gate(void)
{
    const app_hold_prompt_context_t context = available_context();
    const app_page_action_t actions[] = {
        APP_PAGE_ACTION_REFRESH_WEATHER,
        APP_PAGE_ACTION_DELETE_IMAGE,
        APP_PAGE_ACTION_SYNC_TIME,
        APP_PAGE_ACTION_START_VOICE,
        APP_PAGE_ACTION_OPEN_SETTINGS,
        APP_PAGE_ACTION_CHECK_ONLINE_UPDATE,
        APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING,
        APP_PAGE_ACTION_NEXT_TRACK,
        APP_PAGE_ACTION_MUSIC_VOLUME,
    };
    for (size_t index = 0U;
         index < sizeof(actions) / sizeof(actions[0]); ++index) {
        assert(app_hold_prompt_allowed(&context, actions[index], false));
        assert(!app_hold_prompt_allowed(&context, actions[index], true));
    }
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_NONE, false));
    assert(!app_hold_prompt_allowed(NULL,
                                    APP_PAGE_ACTION_SYNC_TIME, false));
}

static void test_common_modal_and_release_gates(void)
{
    app_hold_prompt_context_t context = available_context();
    context.buttons_ready = false;
    assert_common_gate_blocks(&context);

#define ASSERT_GATE(field)                                                   \
    do {                                                                     \
        context = available_context();                                       \
        context.field = true;                                                \
        assert_common_gate_blocks(&context);                                 \
    } while (0)

    ASSERT_GATE(alarm_input_blocked);
    ASSERT_GATE(dual_button_release_gate);
    ASSERT_GATE(voice_button_release_gate);
    ASSERT_GATE(image_delete_release_gate);
    ASSERT_GATE(network_setup_visible);
    ASSERT_GATE(power_setting_ui_active);
    ASSERT_GATE(firmware_update_ui_active);
    ASSERT_GATE(gallery_download_ui_active);
    ASSERT_GATE(online_update_busy);
    ASSERT_GATE(image_delete_ui_active);
    ASSERT_GATE(voice_session_active);

#undef ASSERT_GATE
}

static void test_action_specific_gates(void)
{
    app_hold_prompt_context_t context = available_context();
    context.weather_refresh_available = false;
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_REFRESH_WEATHER, false));
    assert(app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_SYNC_TIME, false));

    context = available_context();
    context.image_delete_available = false;
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_DELETE_IMAGE, false));
    assert(app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_SYNC_TIME, false));

    context = available_context();
    context.manual_sync_idle = false;
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_DELETE_IMAGE, false));
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_START_VOICE, false));
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_OPEN_SETTINGS, false));
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_CHECK_ONLINE_UPDATE, false));
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING, false));
    /* A completed sync result does not block starting another sync. */
    assert(app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_SYNC_TIME, false));

    context.manual_sync_active = true;
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_SYNC_TIME, false));

    context = available_context();
    context.online_update_confirmation_active = true;
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_DELETE_IMAGE, false));
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_START_VOICE, false));
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_OPEN_SETTINGS, false));
    assert(!app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING, false));
    assert(app_hold_prompt_allowed(
        &context, APP_PAGE_ACTION_CHECK_ONLINE_UPDATE, false));
}

static void test_action_titles(void)
{
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_REFRESH_WEATHER,
                      APP_HOLD_UPDATE_CHECK, false),
                  "REFRESH WEATHER") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_DELETE_IMAGE,
                      APP_HOLD_UPDATE_CHECK, false),
                  "DELETE IMAGE") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_SYNC_TIME,
                      APP_HOLD_UPDATE_CHECK, false),
                  "SYNC TIME") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_START_VOICE,
                      APP_HOLD_UPDATE_CHECK, false),
                  "START CHAT") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_OPEN_SETTINGS,
                      APP_HOLD_UPDATE_CHECK, false),
                  "QUICK SETTINGS") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_CHECK_ONLINE_UPDATE,
                      APP_HOLD_UPDATE_CHECK, false),
                  "CHECK UPDATE") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_CHECK_ONLINE_UPDATE,
                      APP_HOLD_UPDATE_REVIEW, false),
                  "REVIEW UPDATE") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_CHECK_ONLINE_UPDATE,
                      APP_HOLD_UPDATE_INSTALL, false),
                  "INSTALL UPDATE") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING,
                      APP_HOLD_UPDATE_CHECK, false),
                  "MANUAL SAVING ON") == 0);
    assert(strcmp(app_hold_prompt_title(
                      APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING,
                      APP_HOLD_UPDATE_CHECK, true),
                  "MANUAL SAVING OFF") == 0);
}

int main(void)
{
    test_all_actions_and_other_button_gate();
    test_common_modal_and_release_gates();
    test_action_specific_gates();
    test_action_titles();
    puts("hold interaction tests passed");
    return 0;
}
