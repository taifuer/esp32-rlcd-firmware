#include "quick_settings.h"

#include <limits.h>

bool quick_settings_open_volume(quick_settings_t *menu, const app_settings_t *latest)
{
    if (menu == NULL || !app_settings_validate(latest)) return false;
    *menu = (quick_settings_t){.active = true, .release_required = true,
                              .draft = latest->audio_playback_volume};
    return true;
}

void quick_settings_close(quick_settings_t *menu)
{
    if (menu != NULL) *menu = (quick_settings_t){0};
}

uint8_t quick_settings_playback_volume(const quick_settings_t *menu,
                                      uint8_t saved_volume)
{
    return menu != NULL && menu->active && menu->draft <= 100U
               ? menu->draft : saved_volume;
}

bool quick_settings_release_gate(quick_settings_t *menu, bool any_pressed)
{
    if (menu == NULL || !menu->active || !menu->release_required) return false;
    if (!any_pressed) menu->release_required = false;
    return true;
}

quick_settings_action_t quick_settings_input(
    quick_settings_t *menu, quick_settings_input_t input)
{
    if (menu == NULL || !menu->active || menu->release_required)
        return QUICK_SETTINGS_ACTION_NONE;
    menu->inactive_ms = 0U;
    menu->notice = QUICK_SETTINGS_NOTICE_NONE;
    switch (input) {
    case QUICK_SETTINGS_BACK:
        quick_settings_close(menu);
        break;
    case QUICK_SETTINGS_NEXT:
        menu->draft = menu->draft >= 100U ? 0U
            : (uint8_t)((menu->draft / 10U + 1U) * 10U);
        break;
    case QUICK_SETTINGS_ACTIVATE:
        menu->release_required = true;
        return QUICK_SETTINGS_ACTION_SAVE;
    }
    return QUICK_SETTINGS_ACTION_NONE;
}

bool quick_settings_save_request(const quick_settings_t *menu,
                                 app_setting_field_t *field, uint8_t *value)
{
    if (menu == NULL || !menu->active || menu->draft > 100U ||
        field == NULL || value == NULL) return false;
    *field = APP_SETTING_VOLUME;
    *value = menu->draft;
    return true;
}

void quick_settings_save_result(quick_settings_t *menu, bool success)
{
    if (menu == NULL || !menu->active) return;
    if (success) quick_settings_close(menu);
    else menu->notice = QUICK_SETTINGS_NOTICE_SAVE_FAILED;
}

bool quick_settings_tick(quick_settings_t *menu, uint32_t elapsed_ms,
                         bool any_pressed)
{
    if (menu == NULL || !menu->active) return false;
    if (any_pressed || menu->release_required) {
        menu->inactive_ms = 0U;
        return false;
    }
    menu->inactive_ms = elapsed_ms > UINT32_MAX - menu->inactive_ms
        ? UINT32_MAX : menu->inactive_ms + elapsed_ms;
    if (menu->inactive_ms < QUICK_SETTINGS_EDIT_TIMEOUT_MS) return false;
    quick_settings_close(menu);
    return true;
}

const char *quick_settings_hold_title(void)
{
    return "SAVE VOLUME";
}
