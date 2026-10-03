#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Runtime-only notice. A stale completion remains visible on the timing page
 * but must not ring minutes later when an unrelated transaction finishes. */
enum { TIMING_NOTICE_WAIT_MS = 60000U, TIMING_NOTICE_VISIBLE_MS = 10000U };

typedef struct {
    bool waiting;
    bool visible;
    bool release_required;
    uint64_t since_ms;
} timing_notice_t;

typedef struct {
    bool started;
    bool stopped;
    bool consume_input;
} timing_notice_result_t;

static inline void timing_notice_offer(timing_notice_t *notice, uint64_t now_ms)
{
    notice->waiting = true;
    notice->since_ms = now_ms;
}

static inline timing_notice_result_t timing_notice_update(
    timing_notice_t *notice, uint64_t now_ms, bool blocked,
    bool buttons_pressed, bool short_press)
{
    timing_notice_result_t result = {0};
    const uint64_t elapsed = now_ms >= notice->since_ms
        ? now_ms - notice->since_ms : 0U;
    if (notice->visible) {
        result.consume_input = true;
        const bool was_release_required = notice->release_required;
        if (!buttons_pressed) notice->release_required = false;
        if (blocked || elapsed >= TIMING_NOTICE_VISIBLE_MS ||
            (short_press && !was_release_required)) {
            notice->visible = false;
            result.stopped = true;
        }
    } else if (notice->waiting) {
        if (elapsed >= TIMING_NOTICE_WAIT_MS) {
            notice->waiting = false;
        } else if (!blocked) {
            notice->waiting = false;
            notice->visible = true;
            notice->since_ms = now_ms;
            notice->release_required = buttons_pressed;
            result.started = true;
            result.consume_input = true;
        }
    }
    return result;
}
