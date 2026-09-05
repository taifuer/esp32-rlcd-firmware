#include "quick_settings.h"

#include <limits.h>

void quick_settings_open(quick_settings_t *menu)
{
    if (menu != NULL) {
        *menu = (quick_settings_t){.active = true, .release_required = true};
    }
}

void quick_settings_close(quick_settings_t *menu)
{
    if (menu != NULL) {
        *menu = (quick_settings_t){0};
    }
}

bool quick_settings_release_gate(quick_settings_t *menu, bool any_pressed)
{
    if (menu == NULL || !menu->active || !menu->release_required) {
        return false;
    }
    if (!any_pressed) {
        menu->release_required = false;
    }
    return true;
}

quick_settings_action_t quick_settings_input(
    quick_settings_t *menu, quick_settings_input_t input,
    const app_settings_t *latest)
{
    if (menu == NULL || !menu->active || menu->release_required) {
        return QUICK_SETTINGS_ACTION_NONE;
    }
    menu->inactive_ms = 0U;
    menu->notice = QUICK_SETTINGS_NOTICE_NONE;
    if (input == QUICK_SETTINGS_BACK) {
        if (menu->editing) {
            menu->editing = false;
            menu->draft = 0U;
        } else {
            quick_settings_close(menu);
        }
    } else if (input == QUICK_SETTINGS_NEXT) {
        if (!menu->editing) {
            menu->item = (quick_settings_item_t)(
                (menu->item + 1U) % QUICK_SETTINGS_ITEM_COUNT);
        } else if (menu->item == QUICK_SETTINGS_VOLUME) {
            menu->draft = menu->draft >= 100U ? 0U
                : (uint8_t)((menu->draft / 10U + 1U) * 10U);
        } else if (menu->item == QUICK_SETTINGS_ALARM) {
            menu->draft = menu->draft == 0U ? 1U : 0U;
        }
    } else if (input == QUICK_SETTINGS_ACTIVATE) {
        menu->release_required = true;
        if (menu->editing) {
            return QUICK_SETTINGS_ACTION_SAVE;
        }
        if (menu->item == QUICK_SETTINGS_WEB) {
            return QUICK_SETTINGS_ACTION_OPEN_WEB;
        }
        if (!app_settings_validate(latest)) {
            menu->notice = QUICK_SETTINGS_NOTICE_UNAVAILABLE;
            return QUICK_SETTINGS_ACTION_NONE;
        }
        menu->draft = menu->item == QUICK_SETTINGS_VOLUME
            ? latest->audio_playback_volume
            : (latest->alarm_enabled ? 1U : 0U);
        menu->editing = true;
    }
    return QUICK_SETTINGS_ACTION_NONE;
}

bool quick_settings_save_request(const quick_settings_t *menu,
                                  app_setting_field_t *field, uint8_t *value)
{
    if (menu == NULL || !menu->active || !menu->editing ||
        field == NULL || value == NULL) {
        return false;
    }
    switch (menu->item) {
    case QUICK_SETTINGS_VOLUME:
        *field = APP_SETTING_VOLUME;
        break;
    case QUICK_SETTINGS_ALARM:
        *field = APP_SETTING_ALARM_ENABLED;
        break;
    default:
        return false;
    }
    *value = menu->draft;
    return true;
}

void quick_settings_save_result(quick_settings_t *menu, bool success)
{
    if (menu != NULL && menu->active && menu->editing) {
        menu->notice = success ? QUICK_SETTINGS_NOTICE_SAVED
                               : QUICK_SETTINGS_NOTICE_SAVE_FAILED;
        if (success) {
            menu->editing = false;
            menu->draft = 0U;
        }
    }
}

bool quick_settings_tick(quick_settings_t *menu, uint32_t elapsed_ms,
                          bool any_pressed)
{
    if (menu == NULL || !menu->active) {
        return false;
    }
    if (!menu->editing || any_pressed || menu->release_required) {
        menu->inactive_ms = 0U;
        return false;
    }
    menu->inactive_ms = elapsed_ms > UINT32_MAX - menu->inactive_ms
        ? UINT32_MAX : menu->inactive_ms + elapsed_ms;
    if (menu->inactive_ms < QUICK_SETTINGS_EDIT_TIMEOUT_MS) {
        return false;
    }
    menu->editing = false;
    menu->draft = 0U;
    menu->inactive_ms = 0U;
    menu->notice = QUICK_SETTINGS_NOTICE_NONE;
    return true;
}

const char *quick_settings_item_name(quick_settings_item_t item)
{
    switch (item) {
    case QUICK_SETTINGS_VOLUME: return "VOLUME";
    case QUICK_SETTINGS_ALARM: return "ALARM";
    case QUICK_SETTINGS_WEB: return "WEB SETTINGS";
    default: return "SETTINGS";
    }
}

const char *quick_settings_hold_title(const quick_settings_t *menu)
{
    if (menu == NULL) { return "QUICK SETTINGS"; }
    switch (menu->item) {
    case QUICK_SETTINGS_VOLUME:
        return menu->editing ? "SAVE VOLUME" : "EDIT VOLUME";
    case QUICK_SETTINGS_ALARM:
        return menu->editing ? "SAVE ALARM" : "EDIT ALARM";
    case QUICK_SETTINGS_WEB:
        return "OPEN WEB SETTINGS";
    default: return "QUICK SETTINGS";
    }
}
