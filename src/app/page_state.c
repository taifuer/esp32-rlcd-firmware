#include "page_state.h"

#include <limits.h>
#include <string.h>

static uint32_t add_saturating(uint32_t value, uint32_t increment)
{
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

static app_page_t next_primary_page(app_page_t page)
{
    switch (page) {
    case APP_PAGE_HOME:
        return APP_PAGE_CALENDAR;
    case APP_PAGE_CALENDAR:
        return APP_PAGE_FIRMWARE;
    case APP_PAGE_FIRMWARE:
    default:
        return APP_PAGE_HOME;
    }
}

void app_page_state_init(app_page_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->current = APP_PAGE_HOME;
    state->status_return = APP_PAGE_HOME;
}

app_page_t app_page_state_current(const app_page_state_t *state)
{
    return state != NULL ? state->current : APP_PAGE_HOME;
}

void app_page_state_boot_short_press(app_page_state_t *state)
{
    if (state == NULL) {
        return;
    }
    const app_page_t primary = state->current == APP_PAGE_DEVICE_STATUS
                                   ? state->status_return
                                   : state->current;
    state->current = next_primary_page(primary);
    state->inactive_ms = 0U;
}

void app_page_state_key_short_press(app_page_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->current == APP_PAGE_DEVICE_STATUS) {
        state->current = state->status_return;
    } else {
        state->status_return = state->current;
        state->current = APP_PAGE_DEVICE_STATUS;
    }
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
    const uint32_t timeout = state->current == APP_PAGE_DEVICE_STATUS
                                 ? APP_PAGE_DEVICE_STATUS_TIMEOUT_MS
                                 : APP_PAGE_SECONDARY_TIMEOUT_MS;
    if (state->inactive_ms < timeout) {
        return false;
    }

    state->current = APP_PAGE_HOME;
    state->status_return = APP_PAGE_HOME;
    state->inactive_ms = 0U;
    return true;
}
