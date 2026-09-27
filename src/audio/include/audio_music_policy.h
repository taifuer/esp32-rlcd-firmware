#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    AUDIO_MUSIC_MODE_SEQUENTIAL = 0,
    AUDIO_MUSIC_MODE_REPEAT_ONE = 1,
    AUDIO_MUSIC_MODE_REPEAT_ALL = 2,
} audio_music_playback_mode_t;

typedef struct {
    audio_music_playback_mode_t mode;
    /* Monotonic milliseconds only. Zero means no timer. Never persisted. */
    uint64_t sleep_deadline_ms;
} audio_music_policy_t;

typedef enum {
    AUDIO_MUSIC_FINISH_IGNORE = 0,
    AUDIO_MUSIC_FINISH_STOP,
    AUDIO_MUSIC_FINISH_NEXT,
} audio_music_finish_t;

bool audio_music_policy_mode_valid(audio_music_playback_mode_t mode);
bool audio_music_policy_set_mode(audio_music_policy_t *policy,
                                 audio_music_playback_mode_t mode);
bool audio_music_policy_timer_minutes_valid(uint16_t minutes);
/* Zero always cancels. Nonzero requires a playing or paused session. Invalid
 * input leaves an existing timer untouched. A new valid timer replaces it. */
bool audio_music_policy_set_timer(audio_music_policy_t *policy,
                                  uint16_t minutes, bool active,
                                  uint64_t now_ms);
void audio_music_policy_cancel_timer(audio_music_policy_t *policy);
bool audio_music_policy_timer_expired(const audio_music_policy_t *policy,
                                       uint64_t now_ms);
uint32_t audio_music_policy_remaining_seconds(
    const audio_music_policy_t *policy, uint64_t now_ms);
/* Only a successful EOF from the current generation may advance/repeat.
 * PLAYING and PAUSED both remain active: a pause during the final DMA drain
 * prepares the next track paused without losing the countdown. Error, stop,
 * empty/changed catalog, or a timer deadline terminates this session and
 * cancels its timer. A stale completion changes nothing. */
audio_music_finish_t audio_music_policy_finish(
    audio_music_policy_t *policy, bool current_generation, bool successful,
    bool playback_active, size_t index, size_t count, uint64_t now_ms,
    size_t *next_index);
