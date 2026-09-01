#include "hold_interaction.h"

#include <stddef.h>

bool app_hold_prompt_allowed(const app_hold_prompt_context_t *context,
                             app_page_action_t action,
                             bool other_button_pressed)
{
    if (context == NULL || action == APP_PAGE_ACTION_NONE ||
        !context->buttons_ready || context->alarm_input_blocked ||
        context->dual_button_release_gate ||
        context->voice_button_release_gate ||
        context->image_delete_release_gate ||
        context->network_setup_visible ||
        context->power_setting_ui_active ||
        context->firmware_update_ui_active ||
        context->gallery_download_ui_active ||
        context->online_update_busy || context->image_delete_ui_active ||
        context->voice_session_active || other_button_pressed) {
        return false;
    }

    switch (action) {
    case APP_PAGE_ACTION_DELETE_IMAGE:
        return context->manual_sync_idle &&
               !context->online_update_confirmation_active &&
               context->image_delete_available;
    case APP_PAGE_ACTION_SYNC_TIME:
        return !context->manual_sync_active;
    case APP_PAGE_ACTION_START_VOICE:
    case APP_PAGE_ACTION_OPEN_SETTINGS:
    case APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING:
        return context->manual_sync_idle &&
               !context->online_update_confirmation_active;
    case APP_PAGE_ACTION_CHECK_ONLINE_UPDATE:
        return context->manual_sync_idle;
    case APP_PAGE_ACTION_NONE:
    default:
        return false;
    }
}

const char *app_hold_prompt_title(app_page_action_t action,
                                  app_hold_update_intent_t update_intent,
                                  bool manual_saving_requested)
{
    switch (action) {
    case APP_PAGE_ACTION_DELETE_IMAGE:
        return "DELETE IMAGE";
    case APP_PAGE_ACTION_SYNC_TIME:
        return "SYNC TIME";
    case APP_PAGE_ACTION_START_VOICE:
        return "START CHAT";
    case APP_PAGE_ACTION_OPEN_SETTINGS:
        return "OPEN WEB SETTINGS";
    case APP_PAGE_ACTION_CHECK_ONLINE_UPDATE:
        if (update_intent == APP_HOLD_UPDATE_INSTALL) {
            return "INSTALL UPDATE";
        }
        if (update_intent == APP_HOLD_UPDATE_REVIEW) {
            return "REVIEW UPDATE";
        }
        return "CHECK UPDATE";
    case APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING:
        return manual_saving_requested ? "MANUAL SAVING OFF"
                                       : "MANUAL SAVING ON";
    case APP_PAGE_ACTION_NONE:
    default:
        return "KEEP HOLDING";
    }
}
