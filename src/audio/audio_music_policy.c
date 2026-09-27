#include "audio_music_policy.h"

#include <limits.h>

bool audio_music_policy_mode_valid(audio_music_playback_mode_t mode)
{
    return mode == AUDIO_MUSIC_MODE_SEQUENTIAL ||
           mode == AUDIO_MUSIC_MODE_REPEAT_ONE ||
           mode == AUDIO_MUSIC_MODE_REPEAT_ALL;
}

bool audio_music_policy_set_mode(audio_music_policy_t *policy,
                                 audio_music_playback_mode_t mode)
{
    if (policy == NULL || !audio_music_policy_mode_valid(mode)) return false;
    policy->mode = mode;
    return true;
}

bool audio_music_policy_timer_minutes_valid(uint16_t minutes)
{
    return minutes == 0U || minutes == 15U || minutes == 30U ||
           minutes == 60U || minutes == 90U;
}

bool audio_music_policy_set_timer(audio_music_policy_t *policy,
                                  uint16_t minutes, bool active,
                                  uint64_t now_ms)
{
    if (policy == NULL || !audio_music_policy_timer_minutes_valid(minutes))
        return false;
    if (minutes == 0U) {
        policy->sleep_deadline_ms = 0U;
        return true;
    }
    const uint64_t duration = (uint64_t)minutes * 60U * 1000U;
    if (!active || now_ms > UINT64_MAX - duration) return false;
    policy->sleep_deadline_ms = now_ms + duration;
    return true;
}

void audio_music_policy_cancel_timer(audio_music_policy_t *policy)
{
    if (policy != NULL) policy->sleep_deadline_ms = 0U;
}

bool audio_music_policy_timer_expired(const audio_music_policy_t *policy,
                                       uint64_t now_ms)
{
    return policy != NULL && policy->sleep_deadline_ms != 0U &&
           now_ms >= policy->sleep_deadline_ms;
}

uint32_t audio_music_policy_remaining_seconds(
    const audio_music_policy_t *policy, uint64_t now_ms)
{
    if (policy == NULL || policy->sleep_deadline_ms == 0U ||
        now_ms >= policy->sleep_deadline_ms) return 0U;
    const uint64_t remaining = policy->sleep_deadline_ms - now_ms;
    /* Avoid addition overflow even for a corrupt/injected deadline. */
    const uint64_t seconds = remaining / 1000U + (remaining % 1000U != 0U);
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}

audio_music_finish_t audio_music_policy_finish(
    audio_music_policy_t *policy, bool current_generation, bool successful,
    bool playback_active, size_t index, size_t count, uint64_t now_ms,
    size_t *next_index)
{
    if (policy == NULL || !current_generation) return AUDIO_MUSIC_FINISH_IGNORE;
    if (!successful || !playback_active || count == 0U || index >= count ||
        next_index == NULL || !audio_music_policy_mode_valid(policy->mode) ||
        audio_music_policy_timer_expired(policy, now_ms)) {
        audio_music_policy_cancel_timer(policy);
        return AUDIO_MUSIC_FINISH_STOP;
    }
    if (policy->mode == AUDIO_MUSIC_MODE_REPEAT_ONE) {
        *next_index = index;
        return AUDIO_MUSIC_FINISH_NEXT;
    }
    if (index < count - 1U) {
        *next_index = index + 1U;
        return AUDIO_MUSIC_FINISH_NEXT;
    }
    if (policy->mode == AUDIO_MUSIC_MODE_REPEAT_ALL) {
        *next_index = 0U;
        return AUDIO_MUSIC_FINISH_NEXT;
    }
    audio_music_policy_cancel_timer(policy);
    return AUDIO_MUSIC_FINISH_STOP;
}
