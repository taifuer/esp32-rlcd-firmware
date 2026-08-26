#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "image_delete_ui.h"

int main(void)
{
    app_image_delete_ui_t ui;
    app_image_delete_ui_init(&ui);
    assert(app_image_delete_ui_state(&ui) == APP_IMAGE_DELETE_UI_IDLE);
    assert(!app_image_delete_ui_is_active(&ui));

    assert(app_image_delete_ui_begin(&ui));
    assert(!app_image_delete_ui_begin(&ui));
    assert(app_image_delete_ui_state(&ui) ==
           APP_IMAGE_DELETE_UI_CONFIRMING);
    assert(!app_image_delete_ui_confirmation_armed(&ui));
    assert(!app_image_delete_ui_confirm(&ui));
    assert(app_image_delete_ui_note_key_released(&ui));
    assert(app_image_delete_ui_confirmation_armed(&ui));
    assert(!app_image_delete_ui_note_key_released(&ui));
    assert(!app_image_delete_ui_tick(
        &ui, APP_IMAGE_DELETE_CONFIRM_TIMEOUT_MS - 1U));
    assert(app_image_delete_ui_tick(&ui, 1U));
    assert(app_image_delete_ui_state(&ui) == APP_IMAGE_DELETE_UI_IDLE);

    assert(app_image_delete_ui_begin(&ui));
    assert(app_image_delete_ui_cancel(&ui));
    assert(!app_image_delete_ui_cancel(&ui));
    assert(!app_image_delete_ui_confirmation_armed(&ui));

    assert(app_image_delete_ui_begin(&ui));
    assert(!app_image_delete_ui_confirm(&ui));
    assert(app_image_delete_ui_note_key_released(&ui));
    assert(app_image_delete_ui_confirm(&ui));
    assert(!app_image_delete_ui_confirm(&ui));
    assert(!app_image_delete_ui_cancel(&ui));
    assert(app_image_delete_ui_complete(&ui, true));
    assert(app_image_delete_ui_state(&ui) == APP_IMAGE_DELETE_UI_SUCCESS);
    assert(!app_image_delete_ui_tick(
        &ui, APP_IMAGE_DELETE_RESULT_TIMEOUT_MS - 1U));
    assert(app_image_delete_ui_tick(&ui, 1U));

    assert(app_image_delete_ui_begin(&ui));
    assert(app_image_delete_ui_note_key_released(&ui));
    assert(app_image_delete_ui_confirm(&ui));
    assert(app_image_delete_ui_complete(&ui, false));
    assert(app_image_delete_ui_state(&ui) == APP_IMAGE_DELETE_UI_FAILED);
    assert(app_image_delete_ui_tick(&ui, UINT32_MAX));
    assert(app_image_delete_ui_state(&ui) == APP_IMAGE_DELETE_UI_IDLE);

    app_image_delete_ui_init(NULL);
    assert(!app_image_delete_ui_begin(NULL));
    assert(!app_image_delete_ui_note_key_released(NULL));
    assert(!app_image_delete_ui_confirmation_armed(NULL));
    assert(!app_image_delete_ui_confirm(NULL));
    assert(!app_image_delete_ui_cancel(NULL));
    assert(!app_image_delete_ui_complete(NULL, true));
    assert(!app_image_delete_ui_tick(NULL, UINT32_MAX));
    assert(app_image_delete_ui_state(NULL) == APP_IMAGE_DELETE_UI_IDLE);
    assert(!app_image_delete_ui_is_active(NULL));

    const app_image_delete_target_t expected = {
        .ready = true,
        .revision = 7U,
        .image_count = 3U,
        .selected_index = 1U,
        .filename = "image-b.pbm",
    };
    app_image_delete_target_t current = expected;
    assert(app_image_delete_target_matches(&expected, &current));
    current.ready = false;
    assert(!app_image_delete_target_matches(&expected, &current));
    current = expected;
    ++current.revision;
    assert(!app_image_delete_target_matches(&expected, &current));
    current = expected;
    ++current.image_count;
    assert(!app_image_delete_target_matches(&expected, &current));
    current = expected;
    ++current.selected_index;
    assert(!app_image_delete_target_matches(&expected, &current));
    current = expected;
    current.filename = "image-c.pbm";
    assert(!app_image_delete_target_matches(&expected, &current));
    current = expected;
    current.filename = "";
    assert(!app_image_delete_target_matches(&expected, &current));
    assert(!app_image_delete_target_matches(NULL, &current));
    assert(!app_image_delete_target_matches(&expected, NULL));

    puts("image delete UI tests passed");
    return 0;
}
