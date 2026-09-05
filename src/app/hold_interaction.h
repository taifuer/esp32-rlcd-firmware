#pragma once

#include <stdbool.h>

#include "page_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_HOLD_UPDATE_CHECK = 0,
    APP_HOLD_UPDATE_REVIEW,
    APP_HOLD_UPDATE_INSTALL,
} app_hold_update_intent_t;

typedef struct {
    bool buttons_ready;
    bool alarm_input_blocked;
    bool dual_button_release_gate;
    bool voice_button_release_gate;
    bool image_delete_release_gate;
    bool network_setup_visible;
    bool power_setting_ui_active;
    bool firmware_update_ui_active;
    bool gallery_download_ui_active;
    bool online_update_busy;
    bool image_delete_ui_active;
    bool voice_session_active;
    bool manual_sync_idle;
    bool manual_sync_active;
    bool online_update_confirmation_active;
    bool weather_refresh_available;
    bool image_delete_available;
} app_hold_prompt_context_t;

bool app_hold_prompt_allowed(const app_hold_prompt_context_t *context,
                             app_page_action_t action,
                             bool other_button_pressed);
const char *app_hold_prompt_title(app_page_action_t action,
                                  app_hold_update_intent_t update_intent,
                                  bool manual_saving_requested);

#ifdef __cplusplus
}
#endif
