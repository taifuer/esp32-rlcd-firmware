#include "button_state.h"

#include <limits.h>
#include <string.h>

static uint32_t add_saturating(uint32_t value, uint32_t increment)
{
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

static void apply_timing(button_state_t *state, uint32_t hold_prompt_ms,
                         uint32_t long_press_ms)
{
    state->long_press_ms = long_press_ms == 0U
                               ? BUTTON_LONG_PRESS_MS
                               : long_press_ms;
    state->hold_prompt_ms = hold_prompt_ms > state->long_press_ms
                                ? state->long_press_ms
                                : hold_prompt_ms;
}

void button_state_init(button_state_t *state, bool raw_pressed)
{
    button_state_init_custom(state, raw_pressed, BUTTON_HOLD_PROMPT_MS,
                             BUTTON_LONG_PRESS_MS);
}

void button_state_init_custom(button_state_t *state, bool raw_pressed,
                              uint32_t hold_prompt_ms,
                              uint32_t long_press_ms)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->candidate_pressed = raw_pressed;
    apply_timing(state, hold_prompt_ms, long_press_ms);
}

bool button_state_set_timing(button_state_t *state, uint32_t hold_prompt_ms,
                             uint32_t long_press_ms)
{
    if (state == NULL || state->candidate_pressed ||
        state->debounced_pressed) {
        return false;
    }
    apply_timing(state, hold_prompt_ms, long_press_ms);
    return true;
}

bool button_state_set_action_timing(button_state_t *state,
                                    uint32_t long_press_ms)
{
    return button_state_set_timing(
        state, BUTTON_HOLD_PROMPT_MS,
        long_press_ms > 0U ? long_press_ms
                           : BUTTON_LONG_PRESS_DISABLED_MS);
}

button_event_t button_state_update(button_state_t *state, bool raw_pressed,
                                   uint32_t elapsed_ms)
{
    if (state == NULL) {
        return BUTTON_EVENT_NONE;
    }

    if (raw_pressed != state->candidate_pressed) {
        state->candidate_pressed = raw_pressed;
        state->candidate_ms = 0U;
    } else {
        state->candidate_ms = add_saturating(state->candidate_ms, elapsed_ms);
    }

    button_event_t event = BUTTON_EVENT_NONE;
    if (state->candidate_pressed != state->debounced_pressed &&
        state->candidate_ms >= BUTTON_DEBOUNCE_MS) {
        state->debounced_pressed = state->candidate_pressed;
        if (state->debounced_pressed) {
            state->hold_ms = 0U;
            state->long_press_reported = false;
        } else {
            if (!state->long_press_reported) {
                event = state->hold_ms >= state->hold_prompt_ms
                            ? BUTTON_EVENT_HOLD_CANCELLED
                            : BUTTON_EVENT_SHORT_PRESS;
            }
            state->hold_ms = 0U;
            state->long_press_reported = false;
        }
    }

    if (state->debounced_pressed) {
        state->hold_ms = add_saturating(state->hold_ms, elapsed_ms);
        if (!state->long_press_reported &&
            state->hold_ms >= state->long_press_ms) {
            state->long_press_reported = true;
            event = BUTTON_EVENT_LONG_PRESS;
        }
    }
    return event;
}

bool button_state_is_pressed(const button_state_t *state)
{
    return state != NULL && state->debounced_pressed;
}

uint32_t button_state_hold_ms(const button_state_t *state)
{
    return button_state_is_pressed(state) ? state->hold_ms : 0U;
}

bool button_state_hold_prompt_active(const button_state_t *state)
{
    return button_state_is_pressed(state) &&
           !state->long_press_reported &&
           state->long_press_ms != BUTTON_LONG_PRESS_DISABLED_MS &&
           state->hold_ms >= state->hold_prompt_ms &&
           state->hold_ms < state->long_press_ms;
}

uint32_t button_state_hold_seconds_remaining(const button_state_t *state)
{
    if (!button_state_hold_prompt_active(state)) {
        return 0U;
    }
    const uint32_t remaining_ms = state->long_press_ms - state->hold_ms;
    return remaining_ms / 1000U + (remaining_ms % 1000U != 0U ? 1U : 0U);
}
