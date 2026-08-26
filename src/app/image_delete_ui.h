#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_IMAGE_DELETE_CONFIRM_TIMEOUT_MS 10000U
#define APP_IMAGE_DELETE_RESULT_TIMEOUT_MS 2000U

typedef enum {
    APP_IMAGE_DELETE_UI_IDLE = 0,
    APP_IMAGE_DELETE_UI_CONFIRMING,
    APP_IMAGE_DELETE_UI_DELETING,
    APP_IMAGE_DELETE_UI_SUCCESS,
    APP_IMAGE_DELETE_UI_FAILED,
} app_image_delete_ui_state_t;

typedef struct {
    app_image_delete_ui_state_t state;
    uint32_t elapsed_ms;
    bool confirmation_armed;
} app_image_delete_ui_t;

typedef struct {
    bool ready;
    uint32_t revision;
    size_t image_count;
    size_t selected_index;
    const char *filename;
} app_image_delete_target_t;

void app_image_delete_ui_init(app_image_delete_ui_t *ui);
bool app_image_delete_ui_begin(app_image_delete_ui_t *ui);
bool app_image_delete_ui_note_key_released(app_image_delete_ui_t *ui);
bool app_image_delete_ui_confirmation_armed(
    const app_image_delete_ui_t *ui);
bool app_image_delete_ui_confirm(app_image_delete_ui_t *ui);
bool app_image_delete_ui_cancel(app_image_delete_ui_t *ui);
bool app_image_delete_ui_complete(app_image_delete_ui_t *ui, bool success);
bool app_image_delete_ui_tick(app_image_delete_ui_t *ui,
                              uint32_t elapsed_ms);
app_image_delete_ui_state_t app_image_delete_ui_state(
    const app_image_delete_ui_t *ui);
bool app_image_delete_ui_is_active(const app_image_delete_ui_t *ui);
bool app_image_delete_target_matches(
    const app_image_delete_target_t *expected,
    const app_image_delete_target_t *current);

#ifdef __cplusplus
}
#endif
