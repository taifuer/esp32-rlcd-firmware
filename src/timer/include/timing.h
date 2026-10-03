#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TIMING_COUNTDOWN_MIN_MINUTES = 1U,
    TIMING_COUNTDOWN_MAX_MINUTES = 180U,
    TIMING_COUNTDOWN_DEFAULT_MINUTES = 5U,
    TIMING_STOPWATCH_MAX_SECONDS = 99U * 60U * 60U + 59U * 60U + 59U,
    TIMING_STOPWATCH_MAX_MS = TIMING_STOPWATCH_MAX_SECONDS * 1000U,
};

typedef enum {
    TIMING_IDLE = 0,
    TIMING_RUNNING,
    TIMING_PAUSED,
    TIMING_FINISHED,
} timing_state_t;

typedef struct {
    timing_state_t countdown_state;
    timing_state_t stopwatch_state;
    uint16_t countdown_duration_minutes;
    uint32_t countdown_remaining_ms;
    /* Remaining seconds round up; elapsed seconds round down. */
    uint32_t countdown_remaining_seconds;
    uint32_t stopwatch_elapsed_ms;
    uint32_t stopwatch_elapsed_seconds;
    bool countdown_notification_pending;
} timing_snapshot_t;

/* Stack-allocatable runtime state; callers should use the functions below. */
typedef struct {
    timing_state_t countdown_state;
    timing_state_t stopwatch_state;
    uint16_t countdown_duration_minutes;
    uint32_t countdown_remaining_ms;
    uint32_t stopwatch_elapsed_ms;
    uint64_t last_now_ms;
    bool has_now;
    bool countdown_notification_pending;
} timing_t;

/* Invalid initial durations use the 5-minute default. Nothing is persisted. */
void timing_init(timing_t *timing, uint16_t countdown_minutes);
bool timing_countdown_duration_is_valid(uint16_t minutes);

/*
 * Advance both timers using the same injected monotonic observation, regardless
 * of the selected page or mode. Backward observations are clamped to the last
 * accepted time, including during pause/resume. Large forward jumps saturate;
 * no absolute deadlines or wrapping additions are used. RTC is never consulted.
 *
 * Call this before reading a snapshot. Control functions that accept now_ms
 * also advance both timers first, even when their requested action is invalid.
 */
void timing_update(timing_t *timing, uint64_t now_ms);
timing_snapshot_t timing_snapshot(const timing_t *timing);

/*
 * A changed duration is accepted only while the countdown is IDLE. Applying a
 * saved preference during another state must wait for a reset, so it cannot
 * shorten or extend the active run. Invalid/rejected values leave it unchanged.
 */
bool timing_countdown_set_duration(timing_t *timing, uint16_t minutes);

/*
 * Start requires IDLE; resume requires PAUSED; pause requires RUNNING after the
 * current time has been evaluated. Toggle selects start/pause/resume. FINISHED
 * requires an explicit reset before another run, for both timers.
 * Return true only if the requested action was applied.
 */
bool timing_countdown_start(timing_t *timing, uint64_t now_ms);
bool timing_countdown_pause(timing_t *timing, uint64_t now_ms);
bool timing_countdown_resume(timing_t *timing, uint64_t now_ms);
bool timing_countdown_toggle(timing_t *timing, uint64_t now_ms);
bool timing_stopwatch_start(timing_t *timing, uint64_t now_ms);
bool timing_stopwatch_pause(timing_t *timing, uint64_t now_ms);
bool timing_stopwatch_resume(timing_t *timing, uint64_t now_ms);
bool timing_stopwatch_toggle(timing_t *timing, uint64_t now_ms);

/* Reset changes only the requested timer; countdown reset cancels its notice. */
void timing_countdown_reset(timing_t *timing);
void timing_stopwatch_reset(timing_t *timing);

/*
 * A countdown completion stays pending until taken or reset. Taking it returns
 * true once per completed run and can also acknowledge/cancel a deferred notice.
 * It does not reset FINISHED. Stopwatch completion never queues a notification.
 */
bool timing_take_countdown_notification(timing_t *timing);

#ifdef __cplusplus
}
#endif
