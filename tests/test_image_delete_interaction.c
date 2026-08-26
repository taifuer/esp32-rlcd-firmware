#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "button_state.h"
#include "image_delete_ui.h"
#include "page_state.h"

static button_event_t update_for(button_state_t *button, bool pressed,
                                 uint32_t duration_ms)
{
    button_event_t event = BUTTON_EVENT_NONE;
    for (uint32_t elapsed = 0U; elapsed < duration_ms; elapsed += 10U) {
        const button_event_t current =
            button_state_update(button, pressed, 10U);
        if (current != BUTTON_EVENT_NONE) {
            assert(event == BUTTON_EVENT_NONE);
            event = current;
        }
    }
    return event;
}

static void settle_released(button_state_t *button)
{
    assert(update_for(button, false, 100U) == BUTTON_EVENT_NONE);
}

int main(void)
{
    button_state_t key;
    button_state_init_custom(&key, false, BUTTON_HOLD_PROMPT_MS,
                             APP_PAGE_IMAGE_DELETE_HOLD_MS);
    settle_released(&key);

    assert(update_for(&key, true, 300U) == BUTTON_EVENT_NONE);
    assert(update_for(&key, false, 60U) == BUTTON_EVENT_SHORT_PRESS);

    assert(update_for(&key, true, 1500U) == BUTTON_EVENT_NONE);
    assert(update_for(&key, false, 60U) ==
           BUTTON_EVENT_HOLD_CANCELLED);

    app_image_delete_ui_t ui;
    app_image_delete_ui_init(&ui);
    assert(update_for(&key, true, 2100U) == BUTTON_EVENT_LONG_PRESS);
    assert(app_image_delete_ui_begin(&ui));
    assert(!app_image_delete_ui_confirmation_armed(&ui));
    assert(!app_image_delete_ui_confirm(&ui));
    assert(update_for(&key, true, 500U) == BUTTON_EVENT_NONE);

    /* Releasing the original long press produces no short action. */
    assert(update_for(&key, false, 60U) == BUTTON_EVENT_NONE);
    assert(app_image_delete_ui_note_key_released(&ui));
    assert(app_image_delete_ui_confirmation_armed(&ui));

    /* Only a separate short press can confirm the destructive action. */
    assert(update_for(&key, true, 300U) == BUTTON_EVENT_NONE);
    assert(update_for(&key, false, 60U) == BUTTON_EVENT_SHORT_PRESS);
    assert(app_image_delete_ui_confirm(&ui));
    assert(app_image_delete_ui_state(&ui) ==
           APP_IMAGE_DELETE_UI_DELETING);

    app_image_delete_ui_init(&ui);
    assert(app_image_delete_ui_begin(&ui));
    assert(app_image_delete_ui_cancel(&ui));
    assert(app_image_delete_ui_state(&ui) == APP_IMAGE_DELETE_UI_IDLE);

    app_image_delete_ui_init(&ui);
    assert(app_image_delete_ui_begin(&ui));
    assert(app_image_delete_ui_note_key_released(&ui));
    assert(!app_image_delete_ui_tick(
        &ui, APP_IMAGE_DELETE_CONFIRM_TIMEOUT_MS - 1U));
    assert(app_image_delete_ui_tick(&ui, 1U));
    assert(app_image_delete_ui_state(&ui) == APP_IMAGE_DELETE_UI_IDLE);

    puts("image delete interaction tests passed");
    return 0;
}
