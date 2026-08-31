#include <assert.h>
#include <stdio.h>

#include "audio_conversation_control.h"
#include "audio_conversation_flow.h"

static void test_repeated_continue_advances_only_once(void)
{
    audio_conversation_control_t control;
    audio_conversation_control_reset(&control);

    assert(audio_conversation_control_request_continue(&control));
    assert(!audio_conversation_control_request_continue(&control));
    assert(audio_conversation_control_take_continue(&control));
    assert(!audio_conversation_control_take_continue(&control));

    /* Taking the request does not reopen the input gate. The gate is released
     * only once the worker has published the next LISTENING phase. */
    assert(!audio_conversation_control_request_continue(&control));
    audio_conversation_control_next_turn_started(&control);
    assert(audio_conversation_control_request_continue(&control));
}

static void test_end_supersedes_pending_continue(void)
{
    audio_conversation_control_t control;
    audio_conversation_control_reset(&control);

    assert(audio_conversation_control_request_continue(&control));
    assert(audio_conversation_control_request_end(&control));
    assert(!audio_conversation_control_take_continue(&control));
    assert(audio_conversation_control_take_end(&control));
    assert(!audio_conversation_control_take_end(&control));
}

static void test_end_does_not_revoke_consumed_continue(void)
{
    audio_conversation_control_t control;
    audio_conversation_control_reset(&control);

    assert(audio_conversation_control_request_continue(&control));
    assert(audio_conversation_control_take_continue(&control));
    /* Once the worker has consumed CONTINUE, NEXT TURN is the committed
     * transition. A late BOOT must not be accepted and then silently cleared
     * by next_turn_started(). */
    assert(!audio_conversation_control_request_end(&control));
    audio_conversation_control_next_turn_started(&control);
    assert(audio_conversation_control_request_end(&control));
}

static void test_reset_and_null_safety(void)
{
    audio_conversation_control_t control = {
        .continue_requested = true,
        .continue_in_flight = true,
        .end_requested = true,
    };
    audio_conversation_control_reset(&control);
    assert(!control.continue_requested);
    assert(!control.continue_in_flight);
    assert(!control.end_requested);

    audio_conversation_control_reset(NULL);
    assert(!audio_conversation_control_request_continue(NULL));
    assert(!audio_conversation_control_take_continue(NULL));
    audio_conversation_control_next_turn_started(NULL);
    assert(!audio_conversation_control_request_end(NULL));
    assert(!audio_conversation_control_take_end(NULL));
}

static void test_deadline_wins_over_pending_continue(void)
{
    audio_conversation_control_t control;
    audio_conversation_flow_t flow;
    audio_conversation_control_reset(&control);
    audio_conversation_flow_init(&flow);
    assert(audio_conversation_flow_turn_completed(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(audio_conversation_control_request_continue(&control));

    /* Model the narrow polling race: the first check is still one millisecond
     * early, then the worker consumes KEY. A second clock sample at the real
     * turn boundary must end the session before flow_continue can advance. */
    assert(audio_conversation_flow_tick(
               &flow, AUDIO_CONVERSATION_FOLLOW_UP_TIMEOUT_MS - 1U,
               AUDIO_CONVERSATION_FOLLOW_UP_TIMEOUT_MS - 1U) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(audio_conversation_control_take_continue(&control));
    assert(audio_conversation_flow_tick(
               &flow, 1U,
               AUDIO_CONVERSATION_FOLLOW_UP_TIMEOUT_MS) ==
           AUDIO_CONVERSATION_FLOW_END_SESSION);
    assert(audio_conversation_flow_continue(&flow) ==
           AUDIO_CONVERSATION_FLOW_NONE);
    assert(flow.turn_number == 1U);
    assert(flow.ended);
}

int main(void)
{
    test_repeated_continue_advances_only_once();
    test_end_supersedes_pending_continue();
    test_end_does_not_revoke_consumed_continue();
    test_reset_and_null_safety();
    test_deadline_wins_over_pending_continue();
    puts("audio conversation control tests passed");
    return 0;
}
