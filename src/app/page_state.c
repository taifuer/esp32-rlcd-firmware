#include "page_state.h"

#include <limits.h>
#include <string.h>

static uint32_t add_saturating(uint32_t value, uint32_t increment)
{
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

static app_page_t next_daily_page(app_page_t page)
{
    return page == APP_PAGE_HOME ? APP_PAGE_CALENDAR : APP_PAGE_HOME;
}

static app_page_t next_system_page(app_page_t page)
{
    switch (page) {
    case APP_PAGE_DEVICE_HEALTH:
        return APP_PAGE_NETWORK_TIME;
    case APP_PAGE_NETWORK_TIME:
        return APP_PAGE_WIFI_MAINTENANCE;
    case APP_PAGE_WIFI_MAINTENANCE:
        return APP_PAGE_ABOUT_UPDATE;
    case APP_PAGE_ABOUT_UPDATE:
    default:
        return APP_PAGE_DEVICE_HEALTH;
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

app_page_t app_page_state_current(const app_page_state_t *state)
{
    return state != NULL ? state->current : APP_PAGE_HOME;
}

bool app_page_is_daily(app_page_t page)
{
    return page == APP_PAGE_HOME || page == APP_PAGE_CALENDAR;
}

bool app_page_is_system(app_page_t page)
{
    return page >= APP_PAGE_DEVICE_HEALTH && page <= APP_PAGE_ABOUT_UPDATE;
}

app_page_action_t app_page_key_hold_action(app_page_t page)
{
    if (page == APP_PAGE_NETWORK_TIME) {
        return APP_PAGE_ACTION_SYNC_TIME;
    }
    if (page == APP_PAGE_WIFI_MAINTENANCE) {
        return APP_PAGE_ACTION_RESET_WIFI;
    }
    if (page == APP_PAGE_ABOUT_UPDATE) {
        return APP_PAGE_ACTION_START_UPDATE;
    }
    return APP_PAGE_ACTION_NONE;
}

uint32_t app_page_key_hold_threshold_ms(app_page_t page)
{
    switch (app_page_key_hold_action(page)) {
    case APP_PAGE_ACTION_SYNC_TIME:
        return APP_PAGE_MANUAL_SYNC_HOLD_MS;
    case APP_PAGE_ACTION_RESET_WIFI:
        return APP_PAGE_WIFI_RESET_HOLD_MS;
    case APP_PAGE_ACTION_START_UPDATE:
        return APP_PAGE_FIRMWARE_UPDATE_HOLD_MS;
    case APP_PAGE_ACTION_NONE:
    default:
        return 0U;
    }
}

void app_page_state_boot_short_press(app_page_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->current = app_page_is_daily(state->current)
                         ? next_daily_page(state->current)
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
                         : APP_PAGE_DEVICE_HEALTH;
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
