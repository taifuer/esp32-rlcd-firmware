#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_CONVERSATION_MAX_TURNS 5U
#define AUDIO_CONVERSATION_FOLLOW_UP_TIMEOUT_MS 30000U
#define AUDIO_CONVERSATION_SESSION_TIMEOUT_MS 300000U

typedef enum {
    AUDIO_CONVERSATION_FLOW_NONE = 0,
    AUDIO_CONVERSATION_FLOW_START_NEXT_TURN,
    AUDIO_CONVERSATION_FLOW_END_SESSION,
} audio_conversation_flow_action_t;

typedef struct {
    uint32_t session_elapsed_ms;
    uint32_t follow_up_elapsed_ms;
    uint8_t turn_number;
    bool awaiting_follow_up;
    bool next_turn_admitted;
    bool ended;
} audio_conversation_flow_t;

void audio_conversation_flow_init(audio_conversation_flow_t *flow);
void audio_conversation_flow_note_session_elapsed(
    audio_conversation_flow_t *flow, uint32_t elapsed_ms);
bool audio_conversation_flow_session_expired(
    const audio_conversation_flow_t *flow);

/* Admit a playback-time NEXT request at the instant it is consumed. Once
 * admitted, the next turn may cross the whole-session deadline while the
 * current response reaches its clean protocol boundary. */
bool audio_conversation_flow_admit_next(
    audio_conversation_flow_t *flow);
bool audio_conversation_flow_can_continue(
    const audio_conversation_flow_t *flow);

/* Call after the current response has stopped and its local PCM queue is
 * empty. The last turn or an expired session ends immediately; otherwise the
 * flow enters the bounded FOLLOW-UP state. */
audio_conversation_flow_action_t audio_conversation_flow_turn_completed(
    audio_conversation_flow_t *flow);

/* A FOLLOW-UP KEY press starts the next turn without another long-press or
 * release gate. Invalid or late requests are ignored. */
audio_conversation_flow_action_t audio_conversation_flow_continue(
    audio_conversation_flow_t *flow);

/* Used while FOLLOW-UP is visible. Session elapsed time is supplied as an
 * absolute monotonic value; follow-up time is accumulated with saturation. */
audio_conversation_flow_action_t audio_conversation_flow_tick(
    audio_conversation_flow_t *flow, uint32_t elapsed_ms,
    uint32_t session_elapsed_ms);

#ifdef __cplusplus
}
#endif
