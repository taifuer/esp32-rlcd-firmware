#include "audio_response_watchdog.h"

#include <limits.h>
#include <stddef.h>

void audio_response_watchdog_init(audio_response_watchdog_t *watchdog)
{
    if (watchdog != NULL) {
        watchdog->idle_ms = 0U;
        watchdog->progress_started = false;
    }
}

void audio_response_watchdog_note_progress(
    audio_response_watchdog_t *watchdog)
{
    if (watchdog != NULL) {
        watchdog->idle_ms = 0U;
        watchdog->progress_started = true;
    }
}

bool audio_response_watchdog_tick(
    audio_response_watchdog_t *watchdog, uint32_t elapsed_ms)
{
    if (watchdog == NULL) {
        return true;
    }
    watchdog->idle_ms = elapsed_ms > UINT32_MAX - watchdog->idle_ms
                            ? UINT32_MAX
                            : watchdog->idle_ms + elapsed_ms;
    const uint32_t limit = watchdog->progress_started
                               ? AUDIO_RESPONSE_STALL_TIMEOUT_MS
                               : AUDIO_RESPONSE_FIRST_PROGRESS_TIMEOUT_MS;
    return watchdog->idle_ms >= limit;
}
