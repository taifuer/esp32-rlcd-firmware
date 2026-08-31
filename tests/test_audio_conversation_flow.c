#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "audio_conversation_flow.h"

static void test_five_turn_boundary(void)
{
    audio_conversation_flow_t flow;
    audio_conversation_flow_init(&flow);
    assert(flow.turn_number == 1U);

    for (uint8_t turn = 1U; turn < AUDIO_CONVERSATION_MAX_TURNS; ++turn) {
        assert(audio_conversation_flow_turn_completed(&flow) ==
               AUDIO_CONVERSATION_FLOW_NONE);
        assert(flow.awaiting_follow_up);
        assert(audio_conversation_flow_continue(&flow) ==
               AUDIO_CONVERSATION_FLOW_START_NEXT_TURN);
        assert(flow.turn_number == (uint8_t)(turn + 1U));
    }
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_END_SESSION);
    assert(flow.ended);
    assert(!flow.awaiting_follow_up);
    assert(audio_conversation_flow_continue(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_END_SESSION);
}

static void test_session_limit_is_five_minutes(void)
{
    assert(AUDIO_CONVERSATION_SESSION_TIMEOUT_MS == 300000U);
}

static void test_follow_up_timeout(void)
{
    audio_conversation_flow_t flow;
    audio_conversation_flow_init(&flow);
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(audio_conversation_flow_tick(
               &flow, AUDIO_CONVERSATION_FOLLOW_UP_TIMEOUT_MS - 1U,
               1000U) == AUDIO_CONVERSATION_FLOW_NONE);
    assert(flow.awaiting_follow_up);
    assert(audio_conversation_flow_tick(&flow, 1U, 1001U) ==
           AUDIO_CONVERSATION_FLOW_END_SESSION);
    assert(!flow.awaiting_follow_up);
    assert(flow.ended);
    assert(audio_conversation_flow_continue(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
}

static void test_session_timeout(void)
{
    audio_conversation_flow_t flow;
    audio_conversation_flow_init(&flow);
    audio_conversation_flow_note_session_elapsed(
        &flow, AUDIO_CONVERSATION_SESSION_TIMEOUT_MS - 1U);
    assert(!audio_conversation_flow_session_expired(&flow));
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(audio_conversation_flow_tick(
               &flow, 1U, AUDIO_CONVERSATION_SESSION_TIMEOUT_MS) ==
           AUDIO_CONVERSATION_FLOW_END_SESSION);
    assert(audio_conversation_flow_session_expired(&flow));
}

static void test_admitted_next_turn_survives_deadline(void)
{
    audio_conversation_flow_t flow;
    audio_conversation_flow_init(&flow);
    audio_conversation_flow_note_session_elapsed(
        &flow, AUDIO_CONVERSATION_SESSION_TIMEOUT_MS - 1U);

    assert(audio_conversation_flow_admit_next(&flow));
    audio_conversation_flow_note_session_elapsed(
        &flow, AUDIO_CONVERSATION_SESSION_TIMEOUT_MS);
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(flow.awaiting_follow_up);
    assert(audio_conversation_flow_can_continue(&flow));
    assert(audio_conversation_flow_continue(&flow) ==
           AUDIO_CONVERSATION_FLOW_START_NEXT_TURN);
    assert(flow.turn_number == 2U);
    assert(!flow.next_turn_admitted);

    /* The admitted turn finishes normally, then the expired session ends
     * instead of offering another follow-up. */
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_END_SESSION);
    assert(flow.ended);
}

static void test_late_next_does_not_end_active_turn(void)
{
    audio_conversation_flow_t flow;
    audio_conversation_flow_init(&flow);
    audio_conversation_flow_note_session_elapsed(
        &flow, AUDIO_CONVERSATION_SESSION_TIMEOUT_MS);

    assert(!audio_conversation_flow_admit_next(&flow));
    assert(!flow.ended);
    assert(!flow.awaiting_follow_up);

    /* The caller keeps playing the current answer. Only its normal response
     * boundary turns the expired admission window into a terminal action. */
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_END_SESSION);
    assert(flow.ended);
}

static void test_monotonic_and_saturating_time(void)
{
    audio_conversation_flow_t flow;
    audio_conversation_flow_init(&flow);
    audio_conversation_flow_note_session_elapsed(&flow, 4000U);
    audio_conversation_flow_note_session_elapsed(&flow, 3000U);
    assert(flow.session_elapsed_ms == 4000U);
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    flow.follow_up_elapsed_ms = UINT32_MAX - 1U;
    assert(audio_conversation_flow_tick(&flow, UINT32_MAX, 4001U) ==
           AUDIO_CONVERSATION_FLOW_END_SESSION);
    assert(flow.follow_up_elapsed_ms == UINT32_MAX);
}

static void test_invalid_calls_and_null_safety(void)
{
    audio_conversation_flow_t flow;
    audio_conversation_flow_init(&flow);
    assert(audio_conversation_flow_continue(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(audio_conversation_flow_tick(&flow, UINT32_MAX, UINT32_MAX) ==
           AUDIO_CONVERSATION_FLOW_NONE);

    audio_conversation_flow_init(NULL);
    audio_conversation_flow_note_session_elapsed(NULL, UINT32_MAX);
    assert(!audio_conversation_flow_session_expired(NULL));
    assert(!audio_conversation_flow_admit_next(NULL));
    assert(!audio_conversation_flow_can_continue(NULL));
    assert(audio_conversation_flow_turn_completed(NULL) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(audio_conversation_flow_continue(NULL) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(audio_conversation_flow_tick(NULL, UINT32_MAX, UINT32_MAX) ==
           AUDIO_CONVERSATION_FLOW_NONE);
}

int main(void)
{
    test_five_turn_boundary();
    test_session_limit_is_five_minutes();
    test_follow_up_timeout();
    test_session_timeout();
    test_admitted_next_turn_survives_deadline();
    test_late_next_does_not_end_active_turn();
    test_monotonic_and_saturating_time();
    test_invalid_calls_and_null_safety();
    puts("audio conversation flow tests passed");
    return 0;
}
