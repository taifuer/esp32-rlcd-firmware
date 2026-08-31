#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t index;
    bool send_speech_sent;
    bool audio_sent;
    bool audio_committed;
    bool stop_speech_sent;
    bool cancel_speech_sent;
    bool response_requested;
    bool response_active;
    bool response_cancel_sent;
    bool local_started_sent;
    bool local_ended_sent;
} conversation_turn_state_t;

void conversation_turn_state_init(conversation_turn_state_t *state);
void conversation_turn_state_mark_session_ready(
    conversation_turn_state_t *state);
bool conversation_turn_state_can_begin_next(
    const conversation_turn_state_t *state, bool connected,
    bool response_ended, bool response_audio_empty);
bool conversation_turn_state_begin_next(
    conversation_turn_state_t *state, bool connected,
    bool response_ended, bool response_audio_empty);
