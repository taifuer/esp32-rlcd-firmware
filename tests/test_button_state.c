#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "button_state.h"

static button_event_t update_for(button_state_t *state, bool pressed,
                                 uint32_t duration_ms, uint32_t step_ms)
{
    button_event_t event = BUTTON_EVENT_NONE;
    for (uint32_t elapsed = 0U; elapsed < duration_ms; elapsed += step_ms) {
        const button_event_t current =
            button_state_update(state, pressed, step_ms);
        if (current != BUTTON_EVENT_NONE) {
            assert(event == BUTTON_EVENT_NONE);
            event = current;
        }
    }
    return event;
}

int main(void)
{
    button_state_t state;
    button_state_init(&state, false);
    assert(!button_state_is_pressed(&state));
    assert(button_state_hold_ms(&state) == 0U);
    assert(update_for(&state, false, 100U, 10U) == BUTTON_EVENT_NONE);

    for (unsigned bounce = 0U; bounce < 4U; ++bounce) {
        assert(update_for(&state, true, 20U, 10U) == BUTTON_EVENT_NONE);
        assert(update_for(&state, false, 20U, 10U) == BUTTON_EVENT_NONE);
    }
    assert(!button_state_is_pressed(&state));

    assert(update_for(&state, true, 300U, 10U) == BUTTON_EVENT_NONE);
    assert(button_state_is_pressed(&state));
    assert(button_state_hold_ms(&state) < BUTTON_HOLD_PROMPT_MS);
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_SHORT_PRESS);
    assert(!button_state_is_pressed(&state));

    assert(update_for(&state, true, 1400U, 10U) == BUTTON_EVENT_NONE);
    assert(button_state_hold_ms(&state) >= BUTTON_HOLD_PROMPT_MS);
    assert(button_state_hold_ms(&state) < BUTTON_LONG_PRESS_MS);
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_HOLD_CANCELLED);

    unsigned long_events = 0U;
    for (uint32_t elapsed = 0U; elapsed < 5600U; elapsed += 10U) {
        if (button_state_update(&state, true, 10U) == BUTTON_EVENT_LONG_PRESS) {
            ++long_events;
        }
    }
    assert(long_events == 1U);
    assert(button_state_hold_ms(&state) >= BUTTON_LONG_PRESS_MS);
    assert(update_for(&state, true, 500U, 10U) == BUTTON_EVENT_NONE);
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_NONE);

    button_state_init(&state, true);
    assert(update_for(&state, true, 5100U, 10U) == BUTTON_EVENT_LONG_PRESS);

    button_state_init_custom(&state, false, 2000U, 2000U);
    assert(update_for(&state, true, 1500U, 10U) == BUTTON_EVENT_NONE);
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_SHORT_PRESS);
    unsigned boot_long_events = 0U;
    for (uint32_t elapsed = 0U; elapsed < 2400U; elapsed += 10U) {
        if (button_state_update(&state, true, 10U) == BUTTON_EVENT_LONG_PRESS) {
            ++boot_long_events;
        }
    }
    assert(boot_long_events == 1U);
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_NONE);

    button_state_init(&state, false);
    assert(button_state_set_timing(&state, 2000U, 2000U));
    assert(update_for(&state, true, 100U, 10U) == BUTTON_EVENT_NONE);
    assert(!button_state_set_timing(&state, BUTTON_HOLD_PROMPT_MS,
                                    BUTTON_LONG_PRESS_MS));
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_SHORT_PRESS);
    assert(button_state_set_timing(&state, BUTTON_HOLD_PROMPT_MS,
                                   BUTTON_LONG_PRESS_DISABLED_MS));
    assert(update_for(&state, true, 1400U, 10U) == BUTTON_EVENT_NONE);
    assert(update_for(&state, false, 60U, 10U) ==
           BUTTON_EVENT_HOLD_CANCELLED);

    assert(button_state_update(NULL, true, 10U) == BUTTON_EVENT_NONE);
    assert(!button_state_set_timing(NULL, 1000U, 2000U));
    assert(!button_state_is_pressed(NULL));
    assert(button_state_hold_ms(NULL) == 0U);

    puts("button state tests passed");
    return 0;
}
