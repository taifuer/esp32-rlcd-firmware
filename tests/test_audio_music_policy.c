#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "audio_music_policy.h"

static void test_modes(void)
{
    for (unsigned mode = 0; mode <= 2U; ++mode) {
        for (size_t count = 1U; count <= 32U; ++count) {
            for (size_t index = 0U; index < count; ++index) {
                audio_music_policy_t policy = {0};
                assert(audio_music_policy_set_mode(&policy,
                    (audio_music_playback_mode_t)mode));
                assert(audio_music_policy_set_timer(&policy, 30U, true, 1000U));
                const uint64_t deadline = policy.sleep_deadline_ms;
                size_t next = SIZE_MAX;
                const audio_music_finish_t result = audio_music_policy_finish(
                    &policy, true, true, true, index, count, 2000U, &next);
                if (mode == AUDIO_MUSIC_MODE_SEQUENTIAL && index == count - 1U) {
                    assert(result == AUDIO_MUSIC_FINISH_STOP);
                    assert(policy.sleep_deadline_ms == 0U);
                    assert(next == SIZE_MAX);
                } else {
                    assert(result == AUDIO_MUSIC_FINISH_NEXT);
                    assert(next == (mode == AUDIO_MUSIC_MODE_REPEAT_ONE
                        ? index : (index + 1U) % count));
                    assert(policy.sleep_deadline_ms == deadline);
                }
            }
        }
    }
    audio_music_policy_t policy = {0};
    assert(!audio_music_policy_set_mode(NULL, AUDIO_MUSIC_MODE_REPEAT_ALL));
    assert(!audio_music_policy_set_mode(&policy, (audio_music_playback_mode_t)-1));
    assert(!audio_music_policy_set_mode(&policy, (audio_music_playback_mode_t)3));
    assert(policy.mode == AUDIO_MUSIC_MODE_SEQUENTIAL);
}

static void test_timers(void)
{
    const uint16_t minutes[] = {15U, 30U, 60U, 90U};
    for (size_t i = 0; i < sizeof(minutes) / sizeof(minutes[0]); ++i) {
        audio_music_policy_t policy = {0};
        assert(audio_music_policy_set_timer(&policy, minutes[i], true, 1234U));
        const uint64_t deadline = 1234U + (uint64_t)minutes[i] * 60000U;
        assert(policy.sleep_deadline_ms == deadline);
        assert(audio_music_policy_remaining_seconds(&policy, 1234U) == minutes[i] * 60U);
        assert(audio_music_policy_remaining_seconds(&policy, 1235U) == minutes[i] * 60U);
        assert(audio_music_policy_remaining_seconds(&policy, 2234U) == minutes[i] * 60U - 1U);
        assert(audio_music_policy_remaining_seconds(&policy, deadline - 1U) == 1U);
        assert(!audio_music_policy_timer_expired(&policy, deadline - 1U));
        assert(audio_music_policy_timer_expired(&policy, deadline));
        assert(audio_music_policy_remaining_seconds(&policy, deadline) == 0U);
        assert(audio_music_policy_remaining_seconds(&policy, UINT64_MAX) == 0U);
        assert(audio_music_policy_set_timer(&policy, minutes[i], true, deadline));
        assert(policy.sleep_deadline_ms == deadline + (uint64_t)minutes[i] * 60000U);
        assert(audio_music_policy_set_timer(&policy, 0U, false, UINT64_MAX));
        assert(policy.sleep_deadline_ms == 0U);
        assert(!audio_music_policy_timer_expired(&policy, UINT64_MAX));
    }
    audio_music_policy_t policy = {.mode = AUDIO_MUSIC_MODE_REPEAT_ALL};
    assert(audio_music_policy_set_timer(&policy, 15U, true, 0U));
    const uint64_t deadline = policy.sleep_deadline_ms;
    for (unsigned value = 0U; value <= UINT16_MAX; ++value) {
        const bool valid = value == 0U || value == 15U || value == 30U ||
            value == 60U || value == 90U;
        assert(audio_music_policy_timer_minutes_valid((uint16_t)value) == valid);
        if (!valid) {
            assert(!audio_music_policy_set_timer(&policy, (uint16_t)value, true, 0U));
            assert(policy.sleep_deadline_ms == deadline);
        }
    }
    assert(!audio_music_policy_set_timer(&policy, 15U, false, 0U));
    assert(!audio_music_policy_set_timer(&policy, 15U, true, UINT64_MAX));
    assert(policy.sleep_deadline_ms == deadline);
    assert(!audio_music_policy_set_timer(NULL, 15U, true, 0U));
    assert(audio_music_policy_remaining_seconds(NULL, 0U) == 0U);
    assert(!audio_music_policy_timer_expired(NULL, UINT64_MAX));
    audio_music_policy_cancel_timer(NULL);
    policy.sleep_deadline_ms = UINT64_MAX;
    assert(audio_music_policy_remaining_seconds(&policy, 0U) == UINT32_MAX);
}

static void test_completion_boundaries(void)
{
    for (unsigned scenario = 0; scenario < 8U; ++scenario) {
        audio_music_policy_t policy = {.mode = AUDIO_MUSIC_MODE_REPEAT_ALL};
        assert(audio_music_policy_set_timer(&policy, 15U, true, 0U));
        const uint64_t deadline = policy.sleep_deadline_ms;
        size_t next = 99U;
        const bool current = scenario != 0U;
        const bool successful = scenario != 1U;
        const bool playing = scenario != 2U;
        const size_t count = scenario == 3U ? 0U : 3U;
        const size_t index = scenario == 4U ? 3U : 2U;
        const uint64_t now = scenario == 5U ? deadline : 1000U;
        if (scenario == 6U) policy.mode = (audio_music_playback_mode_t)100;
        const audio_music_finish_t result = audio_music_policy_finish(
            &policy, current, successful, playing, index, count, now,
            scenario == 7U ? NULL : &next);
        assert(result == (current ? AUDIO_MUSIC_FINISH_STOP : AUDIO_MUSIC_FINISH_IGNORE));
        assert(policy.sleep_deadline_ms == (current ? 0U : deadline));
        assert(next == 99U);
    }
    audio_music_policy_t policy = {0};
    assert(audio_music_policy_set_timer(&policy, 30U, true, 0U));
    const uint64_t deadline = policy.sleep_deadline_ms;
    size_t next = 99U;
    /* A stale EOF/error cannot cancel even a new session's expired timer. */
    assert(audio_music_policy_finish(&policy, false, false, false, 0U, 0U,
        UINT64_MAX, &next) == AUDIO_MUSIC_FINISH_IGNORE);
    assert(policy.sleep_deadline_ms == deadline && next == 99U);
    assert(audio_music_policy_finish(NULL, true, true, true, 0U, 1U, 0U,
        &next) == AUDIO_MUSIC_FINISH_IGNORE);
}

int main(void)
{
    test_modes();
    test_timers();
    test_completion_boundaries();
    puts("audio music policy tests passed");
    return 0;
}
