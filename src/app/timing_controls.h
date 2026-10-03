#pragma once

#include "timing.h"
#include "timing_presets.h"

/* Physical short/hold events are already debounced and release-gated. There is
 * no NVS write, automatic start on page entry, or reset of the other timer. */
static inline void timing_key_short(timing_t *timing, bool stopwatch,
                                    uint16_t custom_minutes, uint64_t now_ms)
{
    timing_update(timing, now_ms);
    const timing_snapshot_t current = timing_snapshot(timing);
    if (stopwatch) {
        if (current.stopwatch_state == TIMING_FINISHED) timing_stopwatch_reset(timing);
        (void)timing_stopwatch_toggle(timing, now_ms);
    } else if (current.countdown_state == TIMING_IDLE) {
        (void)timing_countdown_set_duration(timing,
            timing_next_preset(current.countdown_duration_minutes, custom_minutes));
    } else if (current.countdown_state == TIMING_FINISHED) {
        timing_countdown_reset(timing);
    } else {
        (void)timing_countdown_toggle(timing, now_ms);
    }
}

static inline void timing_key_hold(timing_t *timing, bool stopwatch, uint64_t now_ms)
{
    timing_update(timing, now_ms);
    const timing_snapshot_t current = timing_snapshot(timing);
    if (stopwatch) {
        timing_stopwatch_reset(timing);
    } else if (current.countdown_state == TIMING_IDLE) {
        (void)timing_countdown_start(timing, now_ms);
    } else {
        timing_countdown_reset(timing);
    }
}
