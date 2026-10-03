#include <assert.h>
#include <stdio.h>
#include "timing_controls.h"
#include "button_state.h"

static void test_choices(void)
{
    for (uint32_t custom = 0; custom <= UINT16_MAX; ++custom) {
        uint16_t choices[TIMING_PRESET_CAPACITY];
        const unsigned count = timing_preset_options((uint16_t)custom, choices);
        const bool extra = custom >= 1U && custom <= 180U &&
            custom != 1U && custom != 5U && custom != 10U && custom != 30U;
        assert(count == (extra ? 5U : 4U));
        assert(choices[0] == 1U && choices[1] == 5U && choices[2] == 10U && choices[3] == 30U);
        if (extra) assert(choices[4] == custom);
        for (unsigned i = 0; i < count; ++i) {
            assert(timing_next_preset(choices[i], (uint16_t)custom) == choices[(i + 1U) % count]);
        }
    }
    assert(timing_next_preset(25U, 17U) == 1U); /* removed custom */
}

static void test_select_start_pause_reset(void)
{
    timing_t timer;
    timing_init(&timer, TIMING_COUNTDOWN_DEFAULT_MINUTES);
    assert(timing_snapshot(&timer).countdown_duration_minutes == 5U);
    timing_key_short(&timer, false, 5U, 0U);
    assert(timer.countdown_state == TIMING_IDLE && timer.countdown_duration_minutes == 10U);
    timing_key_short(&timer, false, 5U, 100U);
    assert(timer.countdown_state == TIMING_IDLE && timer.countdown_duration_minutes == 30U);
    timing_key_short(&timer, false, 5U, 200U);
    assert(timer.countdown_state == TIMING_IDLE && timer.countdown_duration_minutes == 1U);
    timing_key_hold(&timer, false, 300U);
    assert(timer.countdown_state == TIMING_RUNNING && timer.countdown_remaining_ms == 60000U);
    timing_key_short(&timer, false, 180U, 1300U);
    assert(timer.countdown_state == TIMING_PAUSED && timer.countdown_remaining_ms == 59000U);
    timing_key_short(&timer, false, 180U, 5300U);
    assert(timer.countdown_state == TIMING_RUNNING && timer.countdown_remaining_ms == 59000U);
    timing_key_hold(&timer, false, 6300U);
    assert(timer.countdown_state == TIMING_IDLE && timer.countdown_duration_minutes == 1U);
    assert(timer.countdown_remaining_ms == 60000U); /* reset keeps selection, not custom */
    timing_key_hold(&timer, false, 7300U);
    timing_update(&timer, 67300U);
    assert(timer.countdown_state == TIMING_FINISHED && timer.countdown_notification_pending);
    timing_key_short(&timer, false, 180U, 68300U);
    assert(timer.countdown_state == TIMING_IDLE && timer.countdown_duration_minutes == 1U);
    assert(!timer.countdown_notification_pending); /* explicit return, no restart */
    timing_key_hold(&timer, false, 69300U);
    assert(timer.countdown_state == TIMING_RUNNING);
}

static void test_independent_modes_and_custom(void)
{
    timing_t timer;
    timing_init(&timer, 5U);
    timing_key_short(&timer, true, 5U, 0U);
    timing_key_short(&timer, false, 17U, 100U); /* 10 */
    timing_key_short(&timer, false, 17U, 200U); /* 30 */
    timing_key_short(&timer, false, 17U, 300U); /* 17 */
    assert(timer.countdown_duration_minutes == 17U && timer.stopwatch_elapsed_ms == 300U);
    timing_key_hold(&timer, false, 400U);
    timing_key_hold(&timer, true, 500U);
    assert(timer.stopwatch_state == TIMING_IDLE && timer.countdown_state == TIMING_RUNNING);
    assert(timer.countdown_remaining_ms == 17U * 60000U - 100U);
    timing_key_hold(&timer, false, 600U);
    assert(timer.countdown_duration_minutes == 17U && timer.countdown_state == TIMING_IDLE);
    timing_key_short(&timer, false, 180U, 700U); /* custom was replaced: go to first */
    assert(timer.countdown_duration_minutes == 1U);
    timing_key_short(&timer, true, 180U, 800U);
    timing_key_short(&timer, true, 180U, 1800U);
    assert(timer.stopwatch_state == TIMING_PAUSED && timer.stopwatch_elapsed_ms == 1000U);
    timing_key_short(&timer, true, 180U, 2800U);
    timing_update(&timer, 2800U + TIMING_STOPWATCH_MAX_MS);
    assert(timer.stopwatch_state == TIMING_FINISHED);
    timing_key_short(&timer, true, 180U, 2800U + TIMING_STOPWATCH_MAX_MS);
    assert(timer.stopwatch_state == TIMING_RUNNING && timer.stopwatch_elapsed_ms == 0U);
    timing_init(&timer, TIMING_COUNTDOWN_DEFAULT_MINUTES);
    assert(timer.countdown_duration_minutes == 5U && timer.stopwatch_state == TIMING_IDLE);
}

static unsigned feed_button(timing_t *timer, button_state_t *button,
                            uint64_t *now, bool pressed, unsigned duration_ms)
{
    unsigned events = 0;
    for (unsigned elapsed = 0; elapsed < duration_ms; elapsed += 10U) {
        *now += 10U;
        const button_event_t event = button_state_update(button, pressed, 10U);
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            timing_key_short(timer, false, 5U, *now);
            ++events;
        } else if (event == BUTTON_EVENT_LONG_PRESS) {
            timing_key_hold(timer, false, *now);
            ++events;
        }
    }
    return events;
}

static void test_hold_release_does_not_select_or_pause(void)
{
    timing_t timer;
    timing_init(&timer, 5U);
    button_state_t button;
    button_state_init_custom(&button, false, 1000U, 2000U);
    uint64_t now = 0U;
    assert(feed_button(&timer, &button, &now, true, 1200U) == 0U);
    assert(feed_button(&timer, &button, &now, false, 100U) == 0U);
    assert(timer.countdown_state == TIMING_IDLE && timer.countdown_duration_minutes == 5U);
    assert(feed_button(&timer, &button, &now, true, 2300U) == 1U);
    assert(timer.countdown_state == TIMING_RUNNING);
    assert(feed_button(&timer, &button, &now, false, 100U) == 0U);
    assert(timer.countdown_state == TIMING_RUNNING); /* no release-pause */
    assert(feed_button(&timer, &button, &now, true, 2300U) == 1U);
    assert(timer.countdown_state == TIMING_IDLE);
    assert(feed_button(&timer, &button, &now, false, 100U) == 0U);
    assert(timer.countdown_duration_minutes == 5U); /* no release-select */
}

int main(void)
{
    test_choices();
    test_select_start_pause_reset();
    test_independent_modes_and_custom();
    test_hold_release_does_not_select_or_pause();
    puts("timing controls: bounded presets, explicit start, repeat duration and independent modes passed");
}
