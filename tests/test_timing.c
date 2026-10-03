#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "timing.h"

static void assert_snapshot(const timing_t *timing,
                            timing_state_t countdown_state,
                            uint32_t countdown_remaining_ms,
                            timing_state_t stopwatch_state,
                            uint32_t stopwatch_elapsed_ms)
{
    const timing_t before = *timing;
    const timing_snapshot_t result = timing_snapshot(timing);
    assert(result.countdown_state == countdown_state);
    assert(result.countdown_remaining_ms == countdown_remaining_ms);
    assert(result.countdown_remaining_seconds ==
           countdown_remaining_ms / 1000U + (countdown_remaining_ms % 1000U != 0U));
    assert(result.stopwatch_state == stopwatch_state);
    assert(result.stopwatch_elapsed_ms == stopwatch_elapsed_ms);
    assert(result.stopwatch_elapsed_seconds == stopwatch_elapsed_ms / 1000U);
    assert(memcmp(timing, &before, sizeof(before)) == 0);
}

static void test_durations_and_defaults(void)
{
    /* Exercise every accepted integer representation, including out-of-range. */
    for (uint32_t minutes = 0U; minutes <= UINT16_MAX; ++minutes) {
        const bool valid = minutes >= 1U && minutes <= 180U;
        assert(timing_countdown_duration_is_valid((uint16_t)minutes) == valid);
        timing_t timing;
        timing_init(&timing, (uint16_t)minutes);
        const uint16_t expected = valid ? (uint16_t)minutes : TIMING_COUNTDOWN_DEFAULT_MINUTES;
        const timing_snapshot_t result = timing_snapshot(&timing);
        assert(result.countdown_duration_minutes == expected);
        assert_snapshot(&timing, TIMING_IDLE, (uint32_t)expected * 60000U,
                        TIMING_IDLE, 0U);
        assert(!result.countdown_notification_pending);
        assert(!timing_take_countdown_notification(&timing));
    }

    timing_t timing;
    timing_init(&timing, TIMING_COUNTDOWN_DEFAULT_MINUTES);
    assert(timing_countdown_set_duration(&timing, 1U));
    assert_snapshot(&timing, TIMING_IDLE, 60000U, TIMING_IDLE, 0U);
    assert(timing_countdown_set_duration(&timing, 180U));
    const timing_t before = timing;
    assert(!timing_countdown_set_duration(&timing, 0U));
    assert(!timing_countdown_set_duration(&timing, 181U));
    assert(!timing_countdown_set_duration(&timing, UINT16_MAX));
    assert(memcmp(&timing, &before, sizeof(before)) == 0);
    assert_snapshot(&timing, TIMING_IDLE, 10800000U, TIMING_IDLE, 0U);
}

static void test_countdown_exact_completion_and_notice_once(void)
{
    timing_t timing;
    timing_init(&timing, 1U);
    assert(timing_countdown_start(&timing, 100U));
    timing_update(&timing, 100U);
    assert_snapshot(&timing, TIMING_RUNNING, 60000U, TIMING_IDLE, 0U);
    timing_update(&timing, 101U);
    assert_snapshot(&timing, TIMING_RUNNING, 59999U, TIMING_IDLE, 0U);
    timing_update(&timing, 1100U);
    assert_snapshot(&timing, TIMING_RUNNING, 59000U, TIMING_IDLE, 0U);
    timing_update(&timing, 60099U);
    assert_snapshot(&timing, TIMING_RUNNING, 1U, TIMING_IDLE, 0U);
    assert(!timing_take_countdown_notification(&timing));
    timing_update(&timing, 60100U);
    assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_IDLE, 0U);
    assert(timing_snapshot(&timing).countdown_notification_pending);

    /* Reading and further ticks preserve an unconsumed notification. */
    timing_update(&timing, 60101U);
    timing_update(&timing, 99999U);
    assert(timing_snapshot(&timing).countdown_notification_pending);
    assert(timing_take_countdown_notification(&timing));
    assert(!timing_take_countdown_notification(&timing));
    assert(!timing_snapshot(&timing).countdown_notification_pending);
    assert(!timing_countdown_start(&timing, 100000U));
    assert(!timing_countdown_pause(&timing, 100000U));
    assert(!timing_countdown_resume(&timing, 100000U));
    assert(!timing_countdown_toggle(&timing, 100000U));
    assert(!timing_countdown_set_duration(&timing, 5U));
    timing_update(&timing, UINT64_MAX);
    assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_IDLE, 0U);
    assert(!timing_take_countdown_notification(&timing));
}

static void test_every_countdown_duration_at_uint64_limit(void)
{
    for (uint16_t minutes = TIMING_COUNTDOWN_MIN_MINUTES;
         minutes <= TIMING_COUNTDOWN_MAX_MINUTES; ++minutes) {
        timing_t timing;
        timing_init(&timing, minutes);
        const uint32_t duration_ms = (uint32_t)minutes * 60000U;
        assert(timing_countdown_start(&timing, UINT64_MAX - duration_ms));
        timing_update(&timing, UINT64_MAX - 1U);
        assert_snapshot(&timing, TIMING_RUNNING, 1U, TIMING_IDLE, 0U);
        assert(!timing_take_countdown_notification(&timing));
        timing_update(&timing, UINT64_MAX);
        assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_IDLE, 0U);
        assert(timing_take_countdown_notification(&timing));
        assert(!timing_take_countdown_notification(&timing));
    }
}

static void test_countdown_pause_resume_and_toggle(void)
{
    timing_t timing;
    timing_init(&timing, 1U);
    assert(!timing_countdown_pause(&timing, 100U));
    assert(!timing_countdown_resume(&timing, 100U));
    assert(timing_countdown_toggle(&timing, 1000U));
    assert(!timing_countdown_start(&timing, 2000U));
    assert(!timing_countdown_resume(&timing, 2000U));
    assert(!timing_countdown_set_duration(&timing, 2U));
    assert(timing_countdown_pause(&timing, 2234U));
    assert_snapshot(&timing, TIMING_PAUSED, 58766U, TIMING_IDLE, 0U);
    assert(!timing_countdown_start(&timing, 2234U));
    assert(!timing_countdown_pause(&timing, 2234U));
    assert(!timing_countdown_set_duration(&timing, 2U));
    timing_update(&timing, 60000U);
    assert_snapshot(&timing, TIMING_PAUSED, 58766U, TIMING_IDLE, 0U);
    assert(timing_countdown_resume(&timing, 70000U));
    assert(timing_countdown_toggle(&timing, 70001U));
    assert_snapshot(&timing, TIMING_PAUSED, 58765U, TIMING_IDLE, 0U);
    assert(timing_countdown_toggle(&timing, 80000U));
    timing_update(&timing, 138764U);
    assert_snapshot(&timing, TIMING_RUNNING, 1U, TIMING_IDLE, 0U);
    /* A pause at the exact deadline must not erase or postpone completion. */
    assert(!timing_countdown_pause(&timing, 138765U));
    assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_IDLE, 0U);
    assert(timing_take_countdown_notification(&timing));
}

static void test_stopwatch_fractional_pause_resume(void)
{
    timing_t timing;
    timing_init(&timing, 25U);
    assert(!timing_stopwatch_pause(&timing, 0U));
    assert(!timing_stopwatch_resume(&timing, 0U));
    assert(timing_stopwatch_start(&timing, 100U));
    assert(!timing_stopwatch_start(&timing, 1099U));
    assert(!timing_stopwatch_resume(&timing, 1099U));
    assert_snapshot(&timing, TIMING_IDLE, 1500000U, TIMING_RUNNING, 999U);
    assert(timing_stopwatch_pause(&timing, 1100U));
    assert(!timing_stopwatch_start(&timing, 1100U));
    assert(!timing_stopwatch_pause(&timing, 1100U));
    timing_update(&timing, 100000U);
    assert_snapshot(&timing, TIMING_IDLE, 1500000U, TIMING_PAUSED, 1000U);
    assert(timing_stopwatch_resume(&timing, 100000U));
    assert(timing_stopwatch_toggle(&timing, 100234U));
    assert_snapshot(&timing, TIMING_IDLE, 1500000U, TIMING_PAUSED, 1234U);
    assert(timing_stopwatch_toggle(&timing, 200000U));
    timing_update(&timing, 200766U);
    assert_snapshot(&timing, TIMING_IDLE, 1500000U, TIMING_RUNNING, 2000U);
    assert(!timing_take_countdown_notification(&timing));
}

static void test_stopwatch_limit(void)
{
    assert(TIMING_STOPWATCH_MAX_SECONDS == 359999U);
    assert(TIMING_STOPWATCH_MAX_MS == 359999000U);
    timing_t timing;
    timing_init(&timing, 25U);
    assert(timing_stopwatch_toggle(&timing, 1U));
    timing_update(&timing, TIMING_STOPWATCH_MAX_MS);
    assert_snapshot(&timing, TIMING_IDLE, 1500000U,
                    TIMING_RUNNING, TIMING_STOPWATCH_MAX_MS - 1U);
    assert(!timing_stopwatch_pause(&timing, (uint64_t)TIMING_STOPWATCH_MAX_MS + 1U));
    assert_snapshot(&timing, TIMING_IDLE, 1500000U,
                    TIMING_FINISHED, TIMING_STOPWATCH_MAX_MS);
    assert(!timing_stopwatch_start(&timing, TIMING_STOPWATCH_MAX_MS + 1U));
    assert(!timing_stopwatch_resume(&timing, TIMING_STOPWATCH_MAX_MS + 1U));
    assert(!timing_stopwatch_toggle(&timing, TIMING_STOPWATCH_MAX_MS + 1U));
    timing_update(&timing, UINT64_MAX);
    assert_snapshot(&timing, TIMING_IDLE, 1500000U,
                    TIMING_FINISHED, TIMING_STOPWATCH_MAX_MS);
    assert(!timing_take_countdown_notification(&timing));
    timing_stopwatch_reset(&timing);
    assert_snapshot(&timing, TIMING_IDLE, 1500000U, TIMING_IDLE, 0U);
}

static void test_stopwatch_limit_after_pause(void)
{
    timing_t timing;
    timing_init(&timing, 25U);
    assert(timing_stopwatch_start(&timing, 10U));
    const uint64_t paused_ms = (uint64_t)TIMING_STOPWATCH_MAX_MS + 10U - 500U;
    assert(timing_stopwatch_pause(&timing, paused_ms));
    timing_update(&timing, paused_ms + 1000000U);
    assert_snapshot(&timing, TIMING_IDLE, 1500000U,
                    TIMING_PAUSED, TIMING_STOPWATCH_MAX_MS - 500U);
    assert(timing_stopwatch_resume(&timing, paused_ms + 1000000U));
    timing_update(&timing, paused_ms + 1000499U);
    assert_snapshot(&timing, TIMING_IDLE, 1500000U,
                    TIMING_RUNNING, TIMING_STOPWATCH_MAX_MS - 1U);
    timing_update(&timing, paused_ms + 1000500U);
    assert_snapshot(&timing, TIMING_IDLE, 1500000U,
                    TIMING_FINISHED, TIMING_STOPWATCH_MAX_MS);
    assert(!timing_take_countdown_notification(&timing));
}

static void test_parallel_timers_and_independent_resets(void)
{
    timing_t timing;
    timing_init(&timing, 1U);
    assert(timing_countdown_start(&timing, 0U));
    assert(timing_stopwatch_start(&timing, 1500U));
    assert_snapshot(&timing, TIMING_RUNNING, 58500U, TIMING_RUNNING, 0U);
    assert(timing_countdown_pause(&timing, 2000U));
    assert_snapshot(&timing, TIMING_PAUSED, 58000U, TIMING_RUNNING, 500U);
    timing_update(&timing, 3000U);
    timing_countdown_reset(&timing);
    assert_snapshot(&timing, TIMING_IDLE, 60000U, TIMING_RUNNING, 1500U);
    assert(timing_countdown_set_duration(&timing, 2U));
    assert(timing_countdown_start(&timing, 4000U));
    assert_snapshot(&timing, TIMING_RUNNING, 120000U, TIMING_RUNNING, 2500U);
    assert(timing_stopwatch_pause(&timing, 5000U));
    assert_snapshot(&timing, TIMING_RUNNING, 119000U, TIMING_PAUSED, 3500U);
    timing_stopwatch_reset(&timing);
    assert_snapshot(&timing, TIMING_RUNNING, 119000U, TIMING_IDLE, 0U);
    assert(timing_stopwatch_start(&timing, 6000U));
    /* Neither navigation nor RTC is part of this API; an off-page gap counts. */
    timing_update(&timing, 124000U);
    assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_RUNNING, 118000U);
    timing_stopwatch_reset(&timing);
    assert(timing_snapshot(&timing).countdown_notification_pending);
    assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_IDLE, 0U);
    assert(timing_stopwatch_start(&timing, 125000U));
    timing_countdown_reset(&timing);
    assert(!timing_take_countdown_notification(&timing));
    assert_snapshot(&timing, TIMING_IDLE, 120000U, TIMING_RUNNING, 0U);
    assert(timing_countdown_set_duration(&timing, 1U));
    assert(timing_countdown_toggle(&timing, 126000U));
    timing_update(&timing, 186000U);
    assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_RUNNING, 61000U);
    assert(timing_take_countdown_notification(&timing));
    assert(!timing_take_countdown_notification(&timing));
}

static void test_reset_before_and_after_completion(void)
{
    timing_t timing;
    timing_init(&timing, 1U);
    assert(timing_countdown_start(&timing, 0U));
    timing_update(&timing, 59000U);
    timing_countdown_reset(&timing);
    timing_update(&timing, 61000U);
    assert_snapshot(&timing, TIMING_IDLE, 60000U, TIMING_IDLE, 0U);
    assert(!timing_take_countdown_notification(&timing));
    assert(timing_countdown_start(&timing, 100000U));
    timing_update(&timing, 160000U);
    assert(timing_snapshot(&timing).countdown_notification_pending);
    timing_countdown_reset(&timing);
    assert(!timing_take_countdown_notification(&timing));
    assert(timing_countdown_start(&timing, 200000U));
    timing_update(&timing, 260000U);
    assert(timing_take_countdown_notification(&timing));
    timing_countdown_reset(&timing);
    assert(timing_countdown_start(&timing, 300000U));
    assert(timing_countdown_pause(&timing, 300001U));
    timing_countdown_reset(&timing);
    assert_snapshot(&timing, TIMING_IDLE, 60000U, TIMING_IDLE, 0U);
    assert(!timing_take_countdown_notification(&timing));

    /* Reinitializing models a reboot; old running state and notices disappear. */
    assert(timing_countdown_start(&timing, 400000U));
    assert(timing_stopwatch_start(&timing, 400000U));
    timing_update(&timing, 500000U);
    timing_init(&timing, 45U);
    assert_snapshot(&timing, TIMING_IDLE, 2700000U, TIMING_IDLE, 0U);
    assert(!timing_take_countdown_notification(&timing));
    assert(timing_stopwatch_start(&timing, 0U));
    timing_update(&timing, 123U);
    assert_snapshot(&timing, TIMING_IDLE, 2700000U, TIMING_RUNNING, 123U);
}

static void test_rollback_never_invents_elapsed_time(void)
{
    timing_t timing;
    timing_init(&timing, 1U);
    assert(timing_countdown_start(&timing, 10000U));
    assert(timing_stopwatch_start(&timing, 10000U));
    timing_update(&timing, 12000U);
    assert_snapshot(&timing, TIMING_RUNNING, 58000U, TIMING_RUNNING, 2000U);
    const timing_t before = timing;
    timing_update(&timing, 0U);
    timing_update(&timing, 11000U);
    timing_update(&timing, 12000U);
    assert(memcmp(&timing, &before, sizeof(before)) == 0);
    timing_update(&timing, 13000U);
    assert_snapshot(&timing, TIMING_RUNNING, 57000U, TIMING_RUNNING, 3000U);

    assert(timing_countdown_pause(&timing, 13000U));
    assert(timing_stopwatch_pause(&timing, 13000U));
    timing_update(&timing, 15000U);
    assert(timing_countdown_resume(&timing, 1U));
    assert(timing_stopwatch_resume(&timing, 1U));
    timing_update(&timing, 14000U);
    assert_snapshot(&timing, TIMING_RUNNING, 57000U, TIMING_RUNNING, 3000U);
    timing_update(&timing, 16000U);
    assert_snapshot(&timing, TIMING_RUNNING, 56000U, TIMING_RUNNING, 4000U);

    timing_countdown_reset(&timing);
    timing_stopwatch_reset(&timing);
    assert(timing_countdown_start(&timing, 0U));
    assert(timing_stopwatch_start(&timing, 0U));
    timing_update(&timing, 16000U);
    assert_snapshot(&timing, TIMING_RUNNING, 60000U, TIMING_RUNNING, 0U);
    timing_update(&timing, 16001U);
    assert_snapshot(&timing, TIMING_RUNNING, 59999U, TIMING_RUNNING, 1U);
}

static void test_uint64_boundaries_and_large_jumps(void)
{
    timing_t timing;
    timing_init(&timing, 1U);
    assert(timing_countdown_start(&timing, UINT64_MAX - 60000U));
    assert(timing_stopwatch_start(&timing, UINT64_MAX - 60000U));
    timing_update(&timing, UINT64_MAX - 1U);
    assert_snapshot(&timing, TIMING_RUNNING, 1U, TIMING_RUNNING, 59999U);
    timing_update(&timing, UINT64_MAX);
    assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_RUNNING, 60000U);
    assert(timing_take_countdown_notification(&timing));
    timing_update(&timing, 0U);
    timing_update(&timing, UINT64_MAX);
    assert_snapshot(&timing, TIMING_FINISHED, 0U, TIMING_RUNNING, 60000U);
    assert(!timing_take_countdown_notification(&timing));

    timing_init(&timing, 180U);
    assert(timing_countdown_start(&timing, 0U));
    assert(timing_stopwatch_start(&timing, 0U));
    timing_update(&timing, UINT64_MAX);
    assert_snapshot(&timing, TIMING_FINISHED, 0U,
                    TIMING_FINISHED, TIMING_STOPWATCH_MAX_MS);
    assert(timing_take_countdown_notification(&timing));
    timing_update(&timing, UINT64_MAX);
    assert(!timing_take_countdown_notification(&timing));

    timing_init(&timing, 1U);
    assert(timing_countdown_start(&timing, UINT64_MAX));
    assert(timing_stopwatch_start(&timing, UINT64_MAX));
    timing_update(&timing, 0U);
    assert_snapshot(&timing, TIMING_RUNNING, 60000U, TIMING_RUNNING, 0U);
}

static void test_update_frequency_does_not_change_elapsed(void)
{
    timing_t frequent;
    timing_t sparse;
    timing_init(&frequent, 180U);
    timing_init(&sparse, 180U);
    const uint64_t started_ms = UINT64_C(1000000000000);
    assert(timing_countdown_start(&frequent, started_ms));
    assert(timing_stopwatch_start(&frequent, started_ms));
    assert(timing_countdown_start(&sparse, started_ms));
    assert(timing_stopwatch_start(&sparse, started_ms));
    uint64_t now_ms = started_ms;
    for (uint32_t step = 0U; step < 10000U; ++step) {
        now_ms += 1U + (step * 7919U) % 2000U;
        timing_update(&frequent, now_ms);
        /* Any number of passive snapshots (or page changes) cannot alter it. */
        const timing_snapshot_t ignored = timing_snapshot(&frequent);
        (void)ignored;
    }
    timing_update(&sparse, now_ms);
    assert(memcmp(&frequent, &sparse, sizeof(frequent)) == 0);
    timing_update(&frequent, started_ms + 10800000U);
    timing_update(&sparse, started_ms + 10800000U);
    assert(memcmp(&frequent, &sparse, sizeof(frequent)) == 0);
    assert(timing_take_countdown_notification(&frequent));
    assert(timing_take_countdown_notification(&sparse));
    assert(memcmp(&frequent, &sparse, sizeof(frequent)) == 0);
}

static void test_null_safety(void)
{
    timing_init(NULL, 25U);
    timing_update(NULL, UINT64_MAX);
    timing_countdown_reset(NULL);
    timing_stopwatch_reset(NULL);
    assert(!timing_countdown_set_duration(NULL, 25U));
    assert(!timing_countdown_start(NULL, 0U));
    assert(!timing_countdown_pause(NULL, 0U));
    assert(!timing_countdown_resume(NULL, 0U));
    assert(!timing_countdown_toggle(NULL, 0U));
    assert(!timing_stopwatch_start(NULL, 0U));
    assert(!timing_stopwatch_pause(NULL, 0U));
    assert(!timing_stopwatch_resume(NULL, 0U));
    assert(!timing_stopwatch_toggle(NULL, 0U));
    assert(!timing_take_countdown_notification(NULL));
    const timing_snapshot_t result = timing_snapshot(NULL);
    assert(result.countdown_state == TIMING_IDLE);
    assert(result.stopwatch_state == TIMING_IDLE);
    assert(result.countdown_remaining_seconds == 0U);
    assert(result.stopwatch_elapsed_seconds == 0U);
    assert(!result.countdown_notification_pending);
}

int main(void)
{
    test_durations_and_defaults();
    test_countdown_exact_completion_and_notice_once();
    test_every_countdown_duration_at_uint64_limit();
    test_countdown_pause_resume_and_toggle();
    test_stopwatch_fractional_pause_resume();
    test_stopwatch_limit();
    test_stopwatch_limit_after_pause();
    test_parallel_timers_and_independent_resets();
    test_reset_before_and_after_completion();
    test_rollback_never_invents_elapsed_time();
    test_uint64_boundaries_and_large_jumps();
    test_update_frequency_does_not_change_elapsed();
    test_null_safety();
    puts("timing tests passed");
    return 0;
}
