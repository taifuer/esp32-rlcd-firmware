#include "image_delete_ui.h"

#include <limits.h>
#include <string.h>

static uint32_t add_saturating(uint32_t value, uint32_t increment)
{
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

void app_image_delete_ui_init(app_image_delete_ui_t *ui)
{
    if (ui != NULL) {
        memset(ui, 0, sizeof(*ui));
    }
}

bool app_image_delete_ui_begin(app_image_delete_ui_t *ui)
{
    if (ui == NULL || ui->state != APP_IMAGE_DELETE_UI_IDLE) {
        return false;
    }
    ui->state = APP_IMAGE_DELETE_UI_CONFIRMING;
    ui->elapsed_ms = 0U;
    ui->confirmation_armed = false;
    return true;
}

bool app_image_delete_ui_note_key_released(app_image_delete_ui_t *ui)
{
    if (ui == NULL || ui->state != APP_IMAGE_DELETE_UI_CONFIRMING ||
        ui->confirmation_armed) {
        return false;
    }
    ui->confirmation_armed = true;
    return true;
}

bool app_image_delete_ui_confirmation_armed(
    const app_image_delete_ui_t *ui)
{
    return ui != NULL && ui->state == APP_IMAGE_DELETE_UI_CONFIRMING &&
           ui->confirmation_armed;
}

bool app_image_delete_ui_confirm(app_image_delete_ui_t *ui)
{
    if (!app_image_delete_ui_confirmation_armed(ui)) {
        return false;
    }
    ui->state = APP_IMAGE_DELETE_UI_DELETING;
    ui->elapsed_ms = 0U;
    ui->confirmation_armed = false;
    return true;
}

bool app_image_delete_ui_cancel(app_image_delete_ui_t *ui)
{
    if (ui == NULL || ui->state != APP_IMAGE_DELETE_UI_CONFIRMING) {
        return false;
    }
    ui->state = APP_IMAGE_DELETE_UI_IDLE;
    ui->elapsed_ms = 0U;
    ui->confirmation_armed = false;
    return true;
}

bool app_image_delete_ui_complete(app_image_delete_ui_t *ui, bool success)
{
    if (ui == NULL || ui->state != APP_IMAGE_DELETE_UI_DELETING) {
        return false;
    }
    ui->state = success ? APP_IMAGE_DELETE_UI_SUCCESS
                        : APP_IMAGE_DELETE_UI_FAILED;
    ui->elapsed_ms = 0U;
    ui->confirmation_armed = false;
    return true;
}

bool app_image_delete_ui_tick(app_image_delete_ui_t *ui,
                              uint32_t elapsed_ms)
{
    if (ui == NULL) {
        return false;
    }
    uint32_t timeout_ms = 0U;
    if (ui->state == APP_IMAGE_DELETE_UI_CONFIRMING) {
        timeout_ms = APP_IMAGE_DELETE_CONFIRM_TIMEOUT_MS;
    } else if (ui->state == APP_IMAGE_DELETE_UI_SUCCESS ||
               ui->state == APP_IMAGE_DELETE_UI_FAILED) {
        timeout_ms = APP_IMAGE_DELETE_RESULT_TIMEOUT_MS;
    } else {
        return false;
    }

    ui->elapsed_ms = add_saturating(ui->elapsed_ms, elapsed_ms);
    if (ui->elapsed_ms < timeout_ms) {
        return false;
    }
    ui->state = APP_IMAGE_DELETE_UI_IDLE;
    ui->elapsed_ms = 0U;
    ui->confirmation_armed = false;
    return true;
}

app_image_delete_ui_state_t app_image_delete_ui_state(
    const app_image_delete_ui_t *ui)
{
    return ui != NULL ? ui->state : APP_IMAGE_DELETE_UI_IDLE;
}

bool app_image_delete_ui_is_active(const app_image_delete_ui_t *ui)
{
    return app_image_delete_ui_state(ui) != APP_IMAGE_DELETE_UI_IDLE;
}

bool app_image_delete_target_matches(
    const app_image_delete_target_t *expected,
    const app_image_delete_target_t *current)
{
    return expected != NULL && current != NULL && expected->ready &&
           current->ready && expected->filename != NULL &&
           current->filename != NULL && expected->filename[0] != '\0' &&
           current->filename[0] != '\0' &&
           expected->revision == current->revision &&
           expected->image_count == current->image_count &&
           expected->selected_index == current->selected_index &&
           strcmp(expected->filename, current->filename) == 0;
}
