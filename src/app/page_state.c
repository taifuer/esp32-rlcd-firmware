#include "page_state.h"

#include <limits.h>
#include <string.h>

static uint32_t add_saturating(uint32_t value, uint32_t increment)
{
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

static app_page_t next_daily_page(const app_page_state_t *state)
{
    if (state->current == APP_PAGE_HOME) {
        return APP_PAGE_CALENDAR;
    }
    if (state->current == APP_PAGE_CALENDAR && state->image_available) {
        return APP_PAGE_IMAGE;
    }
    return APP_PAGE_HOME;
}

static app_page_t next_system_page(app_page_t page)
{
    switch (page) {
    case APP_PAGE_STATUS:
        return APP_PAGE_VOICE;
    case APP_PAGE_VOICE:
        return APP_PAGE_SETTINGS;
    case APP_PAGE_SETTINGS:
        return APP_PAGE_ONLINE_UPDATE;
    case APP_PAGE_ONLINE_UPDATE:
    default:
        return APP_PAGE_STATUS;
    }
}

void app_page_state_init(app_page_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->current = APP_PAGE_HOME;
}

void app_page_state_go_home(app_page_state_t *state)
{
    (void)app_page_state_open_page(state, APP_PAGE_HOME);
}

bool app_page_state_open_page(app_page_state_t *state, app_page_t page)
{
    if (state == NULL ||
        (!app_page_is_daily(page) && !app_page_is_system(page)) ||
        (page == APP_PAGE_IMAGE && !state->image_available)) {
        return false;
    }

    state->current = page;
    state->inactive_ms = 0U;
    return true;
}

void app_page_state_set_image_available(app_page_state_t *state,
                                        bool available)
{
    if (state == NULL) {
        return;
    }
    state->image_available = available;
    if (!available && state->current == APP_PAGE_IMAGE) {
        app_page_state_go_home(state);
    }
}

app_page_t app_page_state_current(const app_page_state_t *state)
{
    return state != NULL ? state->current : APP_PAGE_HOME;
}

bool app_page_is_daily(app_page_t page)
{
    return page == APP_PAGE_HOME || page == APP_PAGE_CALENDAR ||
           page == APP_PAGE_IMAGE;
}

bool app_page_is_system(app_page_t page)
{
    return page >= APP_PAGE_STATUS && page <= APP_PAGE_ONLINE_UPDATE;
}

app_page_action_t app_page_key_hold_action(app_page_t page)
{
    if (page == APP_PAGE_IMAGE) {
        return APP_PAGE_ACTION_DELETE_IMAGE;
    }
    if (page == APP_PAGE_STATUS) {
        return APP_PAGE_ACTION_SYNC_TIME;
    }
    if (page == APP_PAGE_VOICE) {
        return APP_PAGE_ACTION_START_VOICE;
    }
    if (page == APP_PAGE_SETTINGS) {
        return APP_PAGE_ACTION_OPEN_SETTINGS;
    }
    if (page == APP_PAGE_ONLINE_UPDATE) {
        return APP_PAGE_ACTION_CHECK_ONLINE_UPDATE;
    }
    return APP_PAGE_ACTION_NONE;
}

uint32_t app_page_key_hold_threshold_ms(app_page_t page)
{
    switch (app_page_key_hold_action(page)) {
    case APP_PAGE_ACTION_DELETE_IMAGE:
        return APP_PAGE_IMAGE_DELETE_HOLD_MS;
    case APP_PAGE_ACTION_SYNC_TIME:
        return APP_PAGE_MANUAL_SYNC_HOLD_MS;
    case APP_PAGE_ACTION_START_VOICE:
        return APP_PAGE_VOICE_HOLD_MS;
    case APP_PAGE_ACTION_OPEN_SETTINGS:
        return APP_PAGE_SETTINGS_HOLD_MS;
    case APP_PAGE_ACTION_CHECK_ONLINE_UPDATE:
        return app_page_online_update_hold_threshold_ms(false);
    case APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING:
    case APP_PAGE_ACTION_NONE:
    default:
        return 0U;
    }
}

uint32_t app_page_online_update_hold_threshold_ms(
    bool awaiting_install_confirmation)
{
    return awaiting_install_confirmation
               ? APP_PAGE_ONLINE_UPDATE_INSTALL_HOLD_MS
               : APP_PAGE_ONLINE_UPDATE_CHECK_HOLD_MS;
}

app_page_action_t app_page_boot_hold_action(app_page_t page)
{
    return page == APP_PAGE_SETTINGS
               ? APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING
               : APP_PAGE_ACTION_NONE;
}

uint32_t app_page_boot_hold_threshold_ms(app_page_t page)
{
    return app_page_boot_hold_action(page) ==
                   APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING
               ? APP_PAGE_SETTINGS_POWER_HOLD_MS
               : 0U;
}

void app_page_state_boot_short_press(app_page_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->current = app_page_is_daily(state->current)
                         ? next_daily_page(state)
                         : APP_PAGE_HOME;
    state->inactive_ms = 0U;
}

void app_page_state_key_short_press(app_page_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->current = app_page_is_system(state->current)
                         ? next_system_page(state->current)
                         : APP_PAGE_STATUS;
    state->inactive_ms = 0U;
}

void app_page_state_note_activity(app_page_state_t *state)
{
    if (state != NULL) {
        state->inactive_ms = 0U;
    }
}

bool app_page_state_tick(app_page_state_t *state, uint32_t elapsed_ms)
{
    if (state == NULL || state->current == APP_PAGE_HOME) {
        return false;
    }

    state->inactive_ms = add_saturating(state->inactive_ms, elapsed_ms);
    if (state->inactive_ms < APP_PAGE_SECONDARY_TIMEOUT_MS) {
        return false;
    }

    state->current = APP_PAGE_HOME;
    state->inactive_ms = 0U;
    return true;
}
