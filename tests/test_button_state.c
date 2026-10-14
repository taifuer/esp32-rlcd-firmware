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

static void test_action_timing(uint32_t long_press_ms,
                               uint32_t initial_seconds_remaining)
{
    button_state_t state;
    button_state_init(&state, false);
    assert(button_state_set_action_timing(&state, long_press_ms));
    assert(update_for(&state, false, 100U, 10U) == BUTTON_EVENT_NONE);

    /* Debouncing consumes 40 ms before hold time starts. */
    assert(update_for(&state, true, 1030U, 10U) == BUTTON_EVENT_NONE);
    assert(button_state_hold_ms(&state) == BUTTON_HOLD_PROMPT_MS - 10U);
    assert(!button_state_hold_prompt_active(&state));
    assert(button_state_hold_seconds_remaining(&state) == 0U);
    assert(update_for(&state, true, 10U, 10U) == BUTTON_EVENT_NONE);
    assert(button_state_hold_ms(&state) == BUTTON_HOLD_PROMPT_MS);
    assert(button_state_hold_prompt_active(&state));
    assert(button_state_hold_seconds_remaining(&state) ==
           initial_seconds_remaining);

    /* Releasing after the prompt, but before the action threshold, is never
     * reinterpreted as a short press. */
    assert(update_for(&state, false, 60U, 10U) ==
           BUTTON_EVENT_HOLD_CANCELLED);
    assert(!button_state_hold_prompt_active(&state));
    assert(button_state_hold_seconds_remaining(&state) == 0U);

    assert(update_for(&state, true, long_press_ms + 30U, 10U) ==
           BUTTON_EVENT_NONE);
    assert(button_state_hold_ms(&state) == long_press_ms - 10U);
    assert(button_state_hold_prompt_active(&state));
    assert(button_state_hold_seconds_remaining(&state) == 1U);
    assert(update_for(&state, true, 10U, 10U) ==
           BUTTON_EVENT_LONG_PRESS);
    assert(button_state_hold_ms(&state) == long_press_ms);
    assert(!button_state_hold_prompt_active(&state));
    assert(button_state_hold_seconds_remaining(&state) == 0U);
    assert(update_for(&state, true, 500U, 10U) == BUTTON_EVENT_NONE);

    /* The release which follows a triggered action is consumed. */
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_NONE);
}

typedef struct {
    button_state_t first;
    button_state_t second;
    button_chord_t chord;
    unsigned triggers;
    unsigned first_short_presses;
    unsigned second_short_presses;
} chord_fixture_t;

static void chord_for(chord_fixture_t *fixture, bool first, bool second,
                      bool eligible, uint32_t duration_ms)
{
    for (uint32_t elapsed = 0U; elapsed < duration_ms; elapsed += 10U) {
        if (button_state_update(&fixture->first, first, 10U) ==
            BUTTON_EVENT_SHORT_PRESS) ++fixture->first_short_presses;
        if (button_state_update(&fixture->second, second, 10U) ==
            BUTTON_EVENT_SHORT_PRESS) ++fixture->second_short_presses;
        if (button_chord_update(&fixture->chord, &fixture->first,
                                &fixture->second, eligible, 10U))
            ++fixture->triggers;
    }
}

static void chord_fixture_init(chord_fixture_t *fixture, bool initially_pressed)
{
    *fixture = (chord_fixture_t){0};
    button_state_init(&fixture->first, initially_pressed);
    button_state_init(&fixture->second, initially_pressed);
    button_chord_init(&fixture->chord);
    chord_for(fixture, initially_pressed, initially_pressed, true, 60U);
}

static void valid_chord(chord_fixture_t *fixture)
{
    const unsigned previous = fixture->triggers;
    chord_for(fixture, true, true, true, 200U);
    assert(button_chord_is_tracking(&fixture->chord));
    assert(fixture->triggers == previous);
    chord_for(fixture, false, false, true, 80U);
    assert(fixture->triggers == previous + 1U);
    assert(!button_chord_is_tracking(&fixture->chord));
    chord_for(fixture, false, false, true, 100U);
    assert(fixture->triggers == previous + 1U);
}

static void test_short_chord(void)
{
    chord_fixture_t fixture;
    chord_fixture_init(&fixture, false);
    valid_chord(&fixture);
    /* The helper never consumes or mutates ordinary single-button events. */
    assert(fixture.first_short_presses == 1U);
    assert(fixture.second_short_presses == 1U);
    chord_for(&fixture, true, false, true, 200U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 1U);
    assert(fixture.first_short_presses == 2U);
    chord_for(&fixture, false, true, true, 200U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 1U);
    assert(fixture.second_short_presses == 2U);
    valid_chord(&fixture);

    /* Either press/release order is accepted, but never until full release. */
    for (unsigned reverse = 0; reverse < 2U; ++reverse) {
        chord_fixture_init(&fixture, false);
        chord_for(&fixture, reverse == 0U, reverse != 0U, true, 150U);
        chord_for(&fixture, true, true, true, 150U);
        chord_for(&fixture, reverse != 0U, reverse == 0U, true, 100U);
        assert(fixture.triggers == 0U);
        assert(button_chord_is_tracking(&fixture.chord));
        chord_for(&fixture, false, false, true, 80U);
        assert(fixture.triggers == 1U);
    }
}

static void test_chord_duration_and_onset(void)
{
    chord_fixture_t fixture;
    chord_fixture_init(&fixture, false);
    chord_for(&fixture, true, true, true, 1100U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 0U);
    valid_chord(&fixture);

    for (unsigned reverse = 0; reverse < 2U; ++reverse) {
        chord_fixture_init(&fixture, false);
        chord_for(&fixture, reverse == 0U, reverse != 0U, true, 400U);
        chord_for(&fixture, true, true, true, 100U);
        chord_for(&fixture, false, false, true, 80U);
        assert(fixture.triggers == 0U);
        valid_chord(&fixture);
    }
    /* Releasing one key quickly cannot turn the remaining long hold into a
     * short chord, nor can a late second press revive an old long hold. */
    chord_fixture_init(&fixture, false);
    chord_for(&fixture, true, true, true, 200U);
    chord_for(&fixture, false, true, true, 1000U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 0U);
    chord_for(&fixture, true, false, true, 2500U);
    chord_for(&fixture, true, true, true, 100U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 0U);
    valid_chord(&fixture);
}

static void test_chord_modal_and_startup_gates(void)
{
    chord_fixture_t fixture;
    chord_fixture_init(&fixture, true);
    assert(!button_chord_is_tracking(&fixture.chord));
    chord_for(&fixture, true, true, true, 200U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 0U);
    valid_chord(&fixture);

    chord_fixture_init(&fixture, false);
    chord_for(&fixture, true, true, false, 100U);
    assert(!button_chord_is_tracking(&fixture.chord));
    /* The modal ending while keys remain held must not arm the shortcut. */
    chord_for(&fixture, true, true, true, 100U);
    chord_for(&fixture, false, true, true, 80U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 0U);
    valid_chord(&fixture);

    chord_fixture_init(&fixture, false);
    chord_for(&fixture, true, true, true, 100U);
    chord_for(&fixture, true, true, false, 10U);
    chord_for(&fixture, false, true, true, 100U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 0U);
    valid_chord(&fixture);

    /* An alarm/transaction starting on the release loop has priority too. */
    chord_fixture_init(&fixture, false);
    chord_for(&fixture, true, true, true, 100U);
    chord_for(&fixture, false, false, false, 80U);
    assert(fixture.triggers == 0U);
    valid_chord(&fixture);
}

static void test_chord_bounce_and_non_overlap(void)
{
    chord_fixture_t fixture;
    chord_fixture_init(&fixture, false);
    for (unsigned bounce = 0; bounce < 4U; ++bounce) {
        chord_for(&fixture, true, true, true, 20U);
        chord_for(&fixture, false, false, true, 20U);
    }
    assert(fixture.triggers == 0U);
    chord_for(&fixture, true, false, true, 100U);
    chord_for(&fixture, true, true, true, 20U);
    chord_for(&fixture, true, false, true, 100U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 0U);

    /* Independent taps whose debounce tails overlap are not a chord. */
    chord_for(&fixture, true, false, true, 100U);
    chord_for(&fixture, false, true, true, 100U);
    chord_for(&fixture, false, false, true, 80U);
    assert(fixture.triggers == 0U);
    valid_chord(&fixture);

    button_chord_init(NULL);
    assert(!button_chord_is_tracking(NULL));
    assert(!button_chord_update(NULL, &fixture.first, &fixture.second, true, 10U));
    assert(!button_chord_update(&fixture.chord, NULL, &fixture.second, true, 10U));
    assert(!button_chord_update(&fixture.chord, &fixture.first, NULL, true, 10U));
}

static void test_chord_boundaries(void)
{
    for (unsigned beyond_limit = 0U; beyond_limit < 2U; ++beyond_limit) {
        chord_fixture_t fixture;
        chord_fixture_init(&fixture, false);
        chord_for(&fixture, true, false, true,
                  BUTTON_CHORD_MAX_ONSET_MS + beyond_limit * 10U);
        chord_for(&fixture, true, true, true, 100U);
        chord_for(&fixture, false, false, true, 80U);
        assert(fixture.triggers == (beyond_limit == 0U ? 1U : 0U));
    }

    /* Exact duration boundary, independent of a particular polling step. */
    const button_state_t released = {0};
    const button_state_t pressed = {
        .candidate_pressed = true, .debounced_pressed = true, .hold_ms = 40U,
    };
    for (unsigned at_limit = 0U; at_limit < 2U; ++at_limit) {
        button_chord_t chord;
        button_chord_init(&chord);
        assert(!button_chord_update(&chord, &released, &released, true, 0U));
        assert(!button_chord_update(&chord, &pressed, &pressed, true, 0U));
        assert(button_chord_update(&chord, &released, &released, true,
                                   BUTTON_CHORD_SHORT_PRESS_MS - 1U + at_limit) ==
               (at_limit == 0U));
    }
    button_chord_t chord;
    button_chord_init(&chord);
    assert(!button_chord_update(&chord, &released, &released, true, 0U));
    assert(!button_chord_update(&chord, &pressed, &pressed, true, 0U));
    chord.elapsed_ms = UINT32_MAX - 5U;
    assert(!button_chord_update(&chord, &pressed, &pressed, true, 10U));
    assert(chord.elapsed_ms == UINT32_MAX);
    assert(!button_chord_update(&chord, &released, &released, true, 10U));
}

typedef struct {
    button_state_t first;
    button_state_t second;
    button_chord_t chord;
    bool dual_gate;
    unsigned home_actions;
    unsigned first_actions;
    unsigned second_actions;
} chord_dispatch_t;

/* Replay the application's gate/admission/dispatch order at its 50 ms poll
 * period, including debounced release tails and the follow-up single tap. */
static void dispatch_chord_for(chord_dispatch_t *state, bool first, bool second,
                               bool eligible, uint32_t duration_ms)
{
    for (uint32_t elapsed = 0U; elapsed < duration_ms; elapsed += 50U) {
        button_event_t first_event = button_state_update(&state->first, first, 50U);
        button_event_t second_event = button_state_update(&state->second, second, 50U);
        const bool first_down = first || button_state_is_pressed(&state->first);
        const bool second_down = second || button_state_is_pressed(&state->second);
        const bool admitted = !state->dual_gate || button_chord_is_tracking(&state->chord);
        if (first_down && second_down) state->dual_gate = true;
        const bool home = button_chord_update(&state->chord, &state->first,
                                              &state->second,
                                              eligible && admitted, 50U);
        if (home) {
            ++state->home_actions;
            first_event = second_event = BUTTON_EVENT_NONE;
        }
        if (state->dual_gate) {
            first_event = second_event = BUTTON_EVENT_NONE;
            if (!first_down && !second_down) state->dual_gate = false;
        }
        if (eligible) {
            if (first_event == BUTTON_EVENT_SHORT_PRESS) ++state->first_actions;
            if (second_event == BUTTON_EVENT_SHORT_PRESS) ++state->second_actions;
        }
    }
}

static void test_chord_application_dispatch(void)
{
    const uint32_t onset_gaps[] = {0U, 100U, 300U};
    for (unsigned order = 0U; order < 2U; ++order) {
        for (unsigned gap = 0U; gap < sizeof(onset_gaps) / sizeof(onset_gaps[0]); ++gap) {
            chord_dispatch_t state = {0};
            button_state_init(&state.first, false);
            button_state_init(&state.second, false);
            button_chord_init(&state.chord);
            dispatch_chord_for(&state, false, false, true, 100U);
            dispatch_chord_for(&state, order == 0U, order != 0U, true, onset_gaps[gap]);
            dispatch_chord_for(&state, true, true, true, 200U);
            dispatch_chord_for(&state, order != 0U, order == 0U, true, 100U);
            assert(state.home_actions == 0U);
            dispatch_chord_for(&state, false, false, true, 100U);
            assert(state.home_actions == 1U);
            assert(state.first_actions == 0U && state.second_actions == 0U);
            assert(!state.dual_gate && !button_chord_is_tracking(&state.chord));
            dispatch_chord_for(&state, true, false, true, 200U);
            dispatch_chord_for(&state, false, false, true, 100U);
            dispatch_chord_for(&state, false, true, true, 200U);
            dispatch_chord_for(&state, false, false, true, 100U);
            assert(state.home_actions == 1U);
            assert(state.first_actions == 1U && state.second_actions == 1U);
        }
    }

    chord_dispatch_t state = {0};
    button_state_init(&state.first, false);
    button_state_init(&state.second, false);
    button_chord_init(&state.chord);
    dispatch_chord_for(&state, false, false, true, 100U);
    /* External gates cannot become HOME gestures just because both keys are
     * pressed. Releasing restores the next independent input normally. */
    state.dual_gate = true;
    dispatch_chord_for(&state, true, true, true, 200U);
    dispatch_chord_for(&state, false, false, true, 100U);
    assert(state.home_actions == 0U);
    assert(state.first_actions == 0U && state.second_actions == 0U);
    dispatch_chord_for(&state, true, true, true, 200U);
    dispatch_chord_for(&state, false, false, true, 100U);
    assert(state.home_actions == 1U);
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

    test_action_timing(2000U, 1U);
    test_action_timing(3000U, 2U);

    button_state_init_custom(&state, false, BUTTON_HOLD_PROMPT_MS, 3000U);
    assert(update_for(&state, true, 1500U, 10U) == BUTTON_EVENT_NONE);
    assert(update_for(&state, false, 60U, 10U) ==
           BUTTON_EVENT_HOLD_CANCELLED);
    unsigned update_long_events = 0U;
    for (uint32_t elapsed = 0U; elapsed < 3400U; elapsed += 10U) {
        if (button_state_update(&state, true, 10U) ==
            BUTTON_EVENT_LONG_PRESS) {
            ++update_long_events;
        }
    }
    assert(update_long_events == 1U);
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_NONE);

    button_state_init_custom(&state, false, BUTTON_HOLD_PROMPT_MS, 2000U);
    assert(update_for(&state, true, 1500U, 10U) == BUTTON_EVENT_NONE);
    assert(update_for(&state, false, 60U, 10U) ==
           BUTTON_EVENT_HOLD_CANCELLED);
    unsigned power_long_events = 0U;
    for (uint32_t elapsed = 0U; elapsed < 2600U; elapsed += 10U) {
        if (button_state_update(&state, true, 10U) ==
            BUTTON_EVENT_LONG_PRESS) {
            ++power_long_events;
        }
    }
    assert(power_long_events == 1U);
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
    assert(!button_state_set_action_timing(&state, 3000U));
    assert(update_for(&state, false, 60U, 10U) == BUTTON_EVENT_SHORT_PRESS);
    assert(button_state_set_timing(&state, BUTTON_HOLD_PROMPT_MS,
                                   BUTTON_LONG_PRESS_DISABLED_MS));
    assert(update_for(&state, true, 1400U, 10U) == BUTTON_EVENT_NONE);
    assert(!button_state_hold_prompt_active(&state));
    assert(button_state_hold_seconds_remaining(&state) == 0U);
    assert(update_for(&state, false, 60U, 10U) ==
           BUTTON_EVENT_HOLD_CANCELLED);

    assert(button_state_set_action_timing(&state, 0U));
    assert(update_for(&state, true, 1400U, 10U) == BUTTON_EVENT_NONE);
    assert(!button_state_hold_prompt_active(&state));
    assert(button_state_hold_seconds_remaining(&state) == 0U);
    assert(update_for(&state, false, 60U, 10U) ==
           BUTTON_EVENT_HOLD_CANCELLED);

    assert(button_state_update(NULL, true, 10U) == BUTTON_EVENT_NONE);
    assert(!button_state_set_timing(NULL, 1000U, 2000U));
    assert(!button_state_set_action_timing(NULL, 2000U));
    assert(!button_state_is_pressed(NULL));
    assert(button_state_hold_ms(NULL) == 0U);
    assert(!button_state_hold_prompt_active(NULL));
    assert(button_state_hold_seconds_remaining(NULL) == 0U);

    test_short_chord();
    test_chord_duration_and_onset();
    test_chord_modal_and_startup_gates();
    test_chord_bounce_and_non_overlap();
    test_chord_boundaries();
    test_chord_application_dispatch();

    puts("button state tests passed");
    return 0;
}
