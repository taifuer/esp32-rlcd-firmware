#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool continue_requested;
    bool continue_in_flight;
    bool end_requested;
} audio_conversation_control_t;

void audio_conversation_control_reset(
    audio_conversation_control_t *control);
bool audio_conversation_control_request_continue(
    audio_conversation_control_t *control);
bool audio_conversation_control_take_continue(
    audio_conversation_control_t *control);
void audio_conversation_control_next_turn_started(
    audio_conversation_control_t *control);
bool audio_conversation_control_request_end(
    audio_conversation_control_t *control);
bool audio_conversation_control_take_end(
    audio_conversation_control_t *control);

#ifdef __cplusplus
}
#endif
