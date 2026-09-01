#include <assert.h>
#include <stdio.h>

#include "audio_response_watchdog.h"

static void test_first_progress_timeout(void)
{
    audio_response_watchdog_t watchdog;
    audio_response_watchdog_init(&watchdog);
    assert(!audio_response_watchdog_tick(
        &watchdog, AUDIO_RESPONSE_FIRST_PROGRESS_TIMEOUT_MS - 1U));
    assert(audio_response_watchdog_tick(&watchdog, 1U));
}

static void test_stall_timeout_after_progress(void)
{
    audio_response_watchdog_t watchdog;
    audio_response_watchdog_init(&watchdog);
    audio_response_watchdog_note_progress(&watchdog);
    assert(!audio_response_watchdog_tick(
        &watchdog, AUDIO_RESPONSE_STALL_TIMEOUT_MS - 1U));
    assert(audio_response_watchdog_tick(&watchdog, 1U));
}

static void test_progress_can_continue_beyond_old_total_limit(void)
{
    audio_response_watchdog_t watchdog;
    audio_response_watchdog_init(&watchdog);
    for (unsigned step = 0U; step < 8U; ++step) {
        assert(!audio_response_watchdog_tick(&watchdog, 10000U));
        audio_response_watchdog_note_progress(&watchdog);
    }
}

static void test_invalid_watchdog_is_terminal(void)
{
    assert(audio_response_watchdog_tick(NULL, 1U));
    audio_response_watchdog_init(NULL);
    audio_response_watchdog_note_progress(NULL);
}

int main(void)
{
    test_first_progress_timeout();
    test_stall_timeout_after_progress();
    test_progress_can_continue_beyond_old_total_limit();
    test_invalid_watchdog_is_terminal();
    puts("audio response watchdog tests passed");
    return 0;
}
