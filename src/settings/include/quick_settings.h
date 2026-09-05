#pragma once

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUICK_SETTINGS_HOLD_MS 2000U
#define QUICK_SETTINGS_EDIT_TIMEOUT_MS 30000U

typedef enum {
    QUICK_SETTINGS_VOLUME = 0,
    QUICK_SETTINGS_ALARM,
    QUICK_SETTINGS_WEB,
    QUICK_SETTINGS_ITEM_COUNT,
} quick_settings_item_t;

typedef enum {
    QUICK_SETTINGS_NEXT = 0,
    QUICK_SETTINGS_ACTIVATE,
    QUICK_SETTINGS_BACK,
} quick_settings_input_t;

typedef enum {
    QUICK_SETTINGS_ACTION_NONE = 0,
    QUICK_SETTINGS_ACTION_SAVE,
    QUICK_SETTINGS_ACTION_OPEN_WEB,
} quick_settings_action_t;

typedef enum {
    QUICK_SETTINGS_NOTICE_NONE = 0,
    QUICK_SETTINGS_NOTICE_SAVED,
    QUICK_SETTINGS_NOTICE_SAVE_FAILED,
    QUICK_SETTINGS_NOTICE_UNAVAILABLE,
} quick_settings_notice_t;

typedef struct {
    bool active;
    bool editing;
    bool release_required;
    quick_settings_item_t item;
    uint8_t draft;
    uint32_t inactive_ms;
    quick_settings_notice_t notice;
} quick_settings_t;

void quick_settings_open(quick_settings_t *menu);
void quick_settings_close(quick_settings_t *menu);
/* Returns true while the opening/confirming press must still be consumed,
 * including the frame which observes its release. */
bool quick_settings_release_gate(quick_settings_t *menu, bool any_pressed);
quick_settings_action_t quick_settings_input(
    quick_settings_t *menu, quick_settings_input_t input,
    const app_settings_t *latest);
bool quick_settings_save_request(const quick_settings_t *menu,
                                  app_setting_field_t *field, uint8_t *value);
void quick_settings_save_result(quick_settings_t *menu, bool success);
/* Only an unsaved editor expires, returning to the menu. Browsing stays. */
bool quick_settings_tick(quick_settings_t *menu, uint32_t elapsed_ms,
                          bool any_pressed);
const char *quick_settings_item_name(quick_settings_item_t item);
const char *quick_settings_hold_title(const quick_settings_t *menu);

#ifdef __cplusplus
}
#endif
