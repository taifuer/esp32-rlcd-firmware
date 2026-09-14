#pragma once

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUICK_SETTINGS_HOLD_MS 2000U
#define QUICK_SETTINGS_EDIT_TIMEOUT_MS 30000U

typedef enum {
    QUICK_SETTINGS_NEXT = 0,
    QUICK_SETTINGS_ACTIVATE,
    QUICK_SETTINGS_BACK,
} quick_settings_input_t;

typedef enum {
    QUICK_SETTINGS_ACTION_NONE = 0,
    QUICK_SETTINGS_ACTION_SAVE,
} quick_settings_action_t;

typedef enum {
    QUICK_SETTINGS_NOTICE_NONE = 0,
    QUICK_SETTINGS_NOTICE_SAVE_FAILED,
} quick_settings_notice_t;

typedef struct {
    bool active;
    bool release_required;
    uint8_t draft;
    uint32_t inactive_ms;
    quick_settings_notice_t notice;
} quick_settings_t;

/* Contextual music-volume editor; device settings open the web directly. */
bool quick_settings_open_volume(quick_settings_t *menu, const app_settings_t *latest);
void quick_settings_close(quick_settings_t *menu);
uint8_t quick_settings_playback_volume(const quick_settings_t *menu,
                                       uint8_t saved_volume);
/* Returns true while the opening/confirming press must still be consumed,
 * including the frame which observes its release. */
bool quick_settings_release_gate(quick_settings_t *menu, bool any_pressed);
quick_settings_action_t quick_settings_input(
    quick_settings_t *menu, quick_settings_input_t input);
bool quick_settings_save_request(const quick_settings_t *menu,
                                  app_setting_field_t *field, uint8_t *value);
void quick_settings_save_result(quick_settings_t *menu, bool success);
/* An unsaved editor expires back to the music page without saving. */
bool quick_settings_tick(quick_settings_t *menu, uint32_t elapsed_ms,
                          bool any_pressed);
const char *quick_settings_hold_title(void);

#ifdef __cplusplus
}
#endif
