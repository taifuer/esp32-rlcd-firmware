#include "conversation_turn_state.h"

#include <limits.h>
#include <string.h>

void conversation_turn_state_init(conversation_turn_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

void conversation_turn_state_mark_session_ready(
    conversation_turn_state_t *state)
{
    if (state != NULL && state->index == 0U) {
        state->index = 1U;
    }
}

bool conversation_turn_state_can_begin_next(
    const conversation_turn_state_t *state, bool connected,
    bool response_ended, bool response_audio_empty)
{
    return state != NULL && connected && state->index > 0U &&
           state->index < UINT32_MAX && response_ended &&
           !state->response_requested && !state->response_active &&
           (!state->local_started_sent || state->local_ended_sent) &&
           (response_audio_empty || state->response_cancel_sent);
}

bool conversation_turn_state_begin_next(
    conversation_turn_state_t *state, bool connected,
    bool response_ended, bool response_audio_empty)
{
    if (!conversation_turn_state_can_begin_next(
            state, connected, response_ended, response_audio_empty)) {
        return false;
    }
    const uint32_t next_index = state->index + 1U;
    memset(state, 0, sizeof(*state));
    state->index = next_index;
    return true;
}
