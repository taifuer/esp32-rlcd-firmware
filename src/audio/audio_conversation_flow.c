#include "audio_conversation_flow.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static uint32_t add_saturating(uint32_t value, uint32_t increment)
{
    return increment > UINT32_MAX - value ? UINT32_MAX
                                           : value + increment;
}

void audio_conversation_flow_init(audio_conversation_flow_t *flow)
{
    if (flow == NULL) {
        return;
    }
    memset(flow, 0, sizeof(*flow));
    flow->turn_number = 1U;
}

void audio_conversation_flow_note_session_elapsed(
    audio_conversation_flow_t *flow, uint32_t elapsed_ms)
{
    if (flow != NULL && elapsed_ms > flow->session_elapsed_ms) {
        flow->session_elapsed_ms = elapsed_ms;
    }
}

bool audio_conversation_flow_session_expired(
    const audio_conversation_flow_t *flow)
{
    return flow != NULL &&
           flow->session_elapsed_ms >=
               AUDIO_CONVERSATION_SESSION_TIMEOUT_MS;
}

bool audio_conversation_flow_admit_next(
    audio_conversation_flow_t *flow)
{
    if (flow == NULL || flow->ended || flow->awaiting_follow_up ||
        flow->next_turn_admitted ||
        flow->turn_number == 0U ||
        flow->turn_number >= AUDIO_CONVERSATION_MAX_TURNS ||
        audio_conversation_flow_session_expired(flow)) {
        return false;
    }
    flow->next_turn_admitted = true;
    return true;
}

bool audio_conversation_flow_can_continue(
    const audio_conversation_flow_t *flow)
{
    return flow != NULL && !flow->ended && flow->awaiting_follow_up &&
           flow->turn_number > 0U &&
           flow->turn_number < AUDIO_CONVERSATION_MAX_TURNS &&
           (flow->next_turn_admitted ||
            !audio_conversation_flow_session_expired(flow));
}

audio_conversation_flow_action_t audio_conversation_flow_turn_completed(
    audio_conversation_flow_t *flow)
{
    if (flow == NULL || flow->turn_number == 0U ||
        flow->awaiting_follow_up) {
        return AUDIO_CONVERSATION_FLOW_NONE;
    }
    if (flow->ended) {
        return AUDIO_CONVERSATION_FLOW_END_SESSION;
    }
    if (flow->turn_number >= AUDIO_CONVERSATION_MAX_TURNS ||
        (audio_conversation_flow_session_expired(flow) &&
         !flow->next_turn_admitted)) {
        flow->ended = true;
        return AUDIO_CONVERSATION_FLOW_END_SESSION;
    }
    flow->awaiting_follow_up = true;
    flow->follow_up_elapsed_ms = 0U;
    return AUDIO_CONVERSATION_FLOW_NONE;
}

audio_conversation_flow_action_t audio_conversation_flow_continue(
    audio_conversation_flow_t *flow)
{
    if (!audio_conversation_flow_can_continue(flow)) {
        return AUDIO_CONVERSATION_FLOW_NONE;
    }
    flow->awaiting_follow_up = false;
    flow->next_turn_admitted = false;
    flow->follow_up_elapsed_ms = 0U;
    ++flow->turn_number;
    return AUDIO_CONVERSATION_FLOW_START_NEXT_TURN;
}

audio_conversation_flow_action_t audio_conversation_flow_tick(
    audio_conversation_flow_t *flow, uint32_t elapsed_ms,
    uint32_t session_elapsed_ms)
{
    if (flow == NULL || !flow->awaiting_follow_up) {
        return AUDIO_CONVERSATION_FLOW_NONE;
    }
    audio_conversation_flow_note_session_elapsed(flow,
                                                 session_elapsed_ms);
    flow->follow_up_elapsed_ms = add_saturating(
        flow->follow_up_elapsed_ms, elapsed_ms);
    if (flow->follow_up_elapsed_ms >=
            AUDIO_CONVERSATION_FOLLOW_UP_TIMEOUT_MS ||
        audio_conversation_flow_session_expired(flow)) {
        flow->awaiting_follow_up = false;
        flow->ended = true;
        return AUDIO_CONVERSATION_FLOW_END_SESSION;
    }
    return AUDIO_CONVERSATION_FLOW_NONE;
}
