#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

#include "conversation_turn_state.h"

static conversation_turn_state_t completed_turn(void)
{
    conversation_turn_state_t state = {0};
    conversation_turn_state_mark_session_ready(&state);
    state.send_speech_sent = true;
    state.audio_sent = true;
    state.audio_committed = true;
    state.stop_speech_sent = true;
    state.response_cancel_sent = true;
    state.local_started_sent = true;
    state.local_ended_sent = true;
    return state;
}

static void test_first_turn_is_one_based(void)
{
    conversation_turn_state_t state;
    conversation_turn_state_init(&state);
    assert(state.index == 0U);
    conversation_turn_state_mark_session_ready(&state);
    assert(state.index == 1U);
    conversation_turn_state_mark_session_ready(&state);
    assert(state.index == 1U);
}

static void test_next_turn_resets_only_turn_transients(void)
{
    conversation_turn_state_t state = completed_turn();
    assert(conversation_turn_state_begin_next(
        &state, true, true, true));
    assert(state.index == 2U);
    assert(!state.send_speech_sent);
    assert(!state.audio_sent);
    assert(!state.audio_committed);
    assert(!state.stop_speech_sent);
    assert(!state.cancel_speech_sent);
    assert(!state.response_requested);
    assert(!state.response_active);
    assert(!state.response_cancel_sent);
    assert(!state.local_started_sent);
    assert(!state.local_ended_sent);
}

static void test_next_turn_waits_for_a_clean_boundary(void)
{
    conversation_turn_state_t state = completed_turn();
    assert(!conversation_turn_state_can_begin_next(
        &state, false, true, true));
    assert(!conversation_turn_state_can_begin_next(
        &state, true, false, true));
    assert(conversation_turn_state_can_begin_next(
        &state, true, true, false));

    state.response_cancel_sent = false;
    assert(!conversation_turn_state_can_begin_next(
        &state, true, true, false));

    state.response_requested = true;
    assert(!conversation_turn_state_can_begin_next(
        &state, true, true, true));
    state.response_requested = false;
    state.response_active = true;
    assert(!conversation_turn_state_can_begin_next(
        &state, true, true, true));
    state.response_active = false;
    state.local_ended_sent = false;
    assert(!conversation_turn_state_can_begin_next(
        &state, true, true, true));

    state.local_started_sent = false;
    assert(conversation_turn_state_can_begin_next(
        &state, true, true, true));
}

static void test_turn_counter_does_not_wrap(void)
{
    conversation_turn_state_t state = completed_turn();
    state.index = UINT32_MAX;
    assert(!conversation_turn_state_begin_next(
        &state, true, true, true));
    assert(state.index == UINT32_MAX);
}

int main(void)
{
    test_first_turn_is_one_based();
    test_next_turn_resets_only_turn_transients();
    test_next_turn_waits_for_a_clean_boundary();
    test_turn_counter_does_not_wrap();
    puts("conversation_turn_state: OK");
    return 0;
}
