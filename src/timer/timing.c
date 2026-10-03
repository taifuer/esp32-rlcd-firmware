#include "timing.h"

#include <stddef.h>
#include <string.h>

static uint32_t countdown_duration_ms(const timing_t *timing)
{
    return (uint32_t)timing->countdown_duration_minutes * 60U * 1000U;
}

bool timing_countdown_duration_is_valid(uint16_t minutes)
{
    return minutes >= TIMING_COUNTDOWN_MIN_MINUTES &&
           minutes <= TIMING_COUNTDOWN_MAX_MINUTES;
}

void timing_init(timing_t *timing, uint16_t countdown_minutes)
{
    if (timing == NULL) {
        return;
    }
    memset(timing, 0, sizeof(*timing));
    timing->countdown_duration_minutes =
        timing_countdown_duration_is_valid(countdown_minutes)
            ? countdown_minutes : TIMING_COUNTDOWN_DEFAULT_MINUTES;
    timing->countdown_remaining_ms = countdown_duration_ms(timing);
}

void timing_update(timing_t *timing, uint64_t now_ms)
{
    if (timing == NULL) {
        return;
    }
    if (!timing->has_now) {
        timing->last_now_ms = now_ms;
        timing->has_now = true;
        return;
    }
    if (now_ms <= timing->last_now_ms) {
        return;
    }
    const uint64_t elapsed_ms = now_ms - timing->last_now_ms;
    timing->last_now_ms = now_ms;

    if (timing->countdown_state == TIMING_RUNNING) {
        if (elapsed_ms >= timing->countdown_remaining_ms) {
            timing->countdown_remaining_ms = 0U;
            timing->countdown_state = TIMING_FINISHED;
            timing->countdown_notification_pending = true;
        } else {
            timing->countdown_remaining_ms -= (uint32_t)elapsed_ms;
        }
    }
    if (timing->stopwatch_state == TIMING_RUNNING) {
        const uint32_t room_ms =
            TIMING_STOPWATCH_MAX_MS - timing->stopwatch_elapsed_ms;
        if (elapsed_ms >= room_ms) {
            timing->stopwatch_elapsed_ms = TIMING_STOPWATCH_MAX_MS;
            timing->stopwatch_state = TIMING_FINISHED;
        } else {
            timing->stopwatch_elapsed_ms += (uint32_t)elapsed_ms;
        }
    }
}

timing_snapshot_t timing_snapshot(const timing_t *timing)
{
    if (timing == NULL) {
        return (timing_snapshot_t){0};
    }
    return (timing_snapshot_t){
        .countdown_state = timing->countdown_state,
        .stopwatch_state = timing->stopwatch_state,
        .countdown_duration_minutes = timing->countdown_duration_minutes,
        .countdown_remaining_ms = timing->countdown_remaining_ms,
        .countdown_remaining_seconds = timing->countdown_remaining_ms / 1000U +
                                       (timing->countdown_remaining_ms % 1000U != 0U),
        .stopwatch_elapsed_ms = timing->stopwatch_elapsed_ms,
        .stopwatch_elapsed_seconds = timing->stopwatch_elapsed_ms / 1000U,
        .countdown_notification_pending = timing->countdown_notification_pending,
    };
}

bool timing_countdown_set_duration(timing_t *timing, uint16_t minutes)
{
    if (timing == NULL || timing->countdown_state != TIMING_IDLE ||
        !timing_countdown_duration_is_valid(minutes)) {
        return false;
    }
    timing->countdown_duration_minutes = minutes;
    timing->countdown_remaining_ms = countdown_duration_ms(timing);
    return true;
}

static bool change_state(timing_t *timing, timing_state_t *state,
                         timing_state_t required, timing_state_t next,
                         uint64_t now_ms)
{
    timing_update(timing, now_ms);
    if (*state != required) {
        return false;
    }
    *state = next;
    return true;
}

bool timing_countdown_start(timing_t *timing, uint64_t now_ms)
{
    return timing != NULL &&
           change_state(timing, &timing->countdown_state,
                        TIMING_IDLE, TIMING_RUNNING, now_ms);
}

bool timing_countdown_pause(timing_t *timing, uint64_t now_ms)
{
    return timing != NULL &&
           change_state(timing, &timing->countdown_state,
                        TIMING_RUNNING, TIMING_PAUSED, now_ms);
}

bool timing_countdown_resume(timing_t *timing, uint64_t now_ms)
{
    return timing != NULL &&
           change_state(timing, &timing->countdown_state,
                        TIMING_PAUSED, TIMING_RUNNING, now_ms);
}

static bool toggle_state(timing_t *timing, timing_state_t *state,
                         uint64_t now_ms)
{
    timing_update(timing, now_ms);
    switch (*state) {
    case TIMING_IDLE:
    case TIMING_PAUSED:
        *state = TIMING_RUNNING;
        return true;
    case TIMING_RUNNING:
        *state = TIMING_PAUSED;
        return true;
    case TIMING_FINISHED:
    default:
        return false;
    }
}

bool timing_countdown_toggle(timing_t *timing, uint64_t now_ms)
{
    return timing != NULL &&
           toggle_state(timing, &timing->countdown_state, now_ms);
}

bool timing_stopwatch_start(timing_t *timing, uint64_t now_ms)
{
    return timing != NULL &&
           change_state(timing, &timing->stopwatch_state,
                        TIMING_IDLE, TIMING_RUNNING, now_ms);
}

bool timing_stopwatch_pause(timing_t *timing, uint64_t now_ms)
{
    return timing != NULL &&
           change_state(timing, &timing->stopwatch_state,
                        TIMING_RUNNING, TIMING_PAUSED, now_ms);
}

bool timing_stopwatch_resume(timing_t *timing, uint64_t now_ms)
{
    return timing != NULL &&
           change_state(timing, &timing->stopwatch_state,
                        TIMING_PAUSED, TIMING_RUNNING, now_ms);
}

bool timing_stopwatch_toggle(timing_t *timing, uint64_t now_ms)
{
    return timing != NULL &&
           toggle_state(timing, &timing->stopwatch_state, now_ms);
}

void timing_countdown_reset(timing_t *timing)
{
    if (timing == NULL) {
        return;
    }
    timing->countdown_state = TIMING_IDLE;
    timing->countdown_remaining_ms = countdown_duration_ms(timing);
    timing->countdown_notification_pending = false;
}

void timing_stopwatch_reset(timing_t *timing)
{
    if (timing == NULL) {
        return;
    }
    timing->stopwatch_state = TIMING_IDLE;
    timing->stopwatch_elapsed_ms = 0U;
}

bool timing_take_countdown_notification(timing_t *timing)
{
    if (timing == NULL || !timing->countdown_notification_pending) {
        return false;
    }
    timing->countdown_notification_pending = false;
    return true;
}
