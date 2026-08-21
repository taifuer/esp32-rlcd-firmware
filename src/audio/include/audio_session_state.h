#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_SESSION_STATE_IDLE = 0,
    AUDIO_SESSION_STATE_PLAYING_TONE,
    AUDIO_SESSION_STATE_PREPARING_RECORDING,
    AUDIO_SESSION_STATE_RECORDING,
    AUDIO_SESSION_STATE_ANALYZING,
    AUDIO_SESSION_STATE_PLAYBACK,
    AUDIO_SESSION_STATE_COMPLETED,
    AUDIO_SESSION_STATE_CANCELLED,
    AUDIO_SESSION_STATE_FAILED,
} audio_session_state_t;

typedef enum {
    AUDIO_SESSION_INPUT_NONE = 0,
    AUDIO_SESSION_INPUT_KEY_SHORT_PRESS,
    AUDIO_SESSION_INPUT_BOOT_SHORT_PRESS,
} audio_session_input_t;

typedef enum {
    AUDIO_SESSION_ACTION_NONE = 0,
    AUDIO_SESSION_ACTION_STOP,
    AUDIO_SESSION_ACTION_CANCEL,
} audio_session_action_t;

bool audio_session_state_is_active(audio_session_state_t state);
audio_session_action_t audio_session_input_action(
    audio_session_state_t state, audio_session_input_t input);
const char *audio_session_state_name(audio_session_state_t state);

#ifdef __cplusplus
}
#endif
