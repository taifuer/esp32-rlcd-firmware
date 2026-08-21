#include "audio_session_state.h"

bool audio_session_state_is_active(audio_session_state_t state)
{
    return state >= AUDIO_SESSION_STATE_PLAYING_TONE &&
           state <= AUDIO_SESSION_STATE_PLAYBACK;
}

audio_session_action_t audio_session_input_action(
    audio_session_state_t state, audio_session_input_t input)
{
    if (!audio_session_state_is_active(state)) {
        return AUDIO_SESSION_ACTION_NONE;
    }
    if (input == AUDIO_SESSION_INPUT_BOOT_SHORT_PRESS) {
        return AUDIO_SESSION_ACTION_CANCEL;
    }
    if (input == AUDIO_SESSION_INPUT_KEY_SHORT_PRESS &&
        (state == AUDIO_SESSION_STATE_RECORDING ||
         state == AUDIO_SESSION_STATE_PLAYBACK)) {
        return AUDIO_SESSION_ACTION_STOP;
    }
    return AUDIO_SESSION_ACTION_NONE;
}

const char *audio_session_state_name(audio_session_state_t state)
{
    switch (state) {
    case AUDIO_SESSION_STATE_PLAYING_TONE:
        return "PLAYING TONE";
    case AUDIO_SESSION_STATE_PREPARING_RECORDING:
        return "PREPARING";
    case AUDIO_SESSION_STATE_RECORDING:
        return "RECORDING";
    case AUDIO_SESSION_STATE_ANALYZING:
        return "ANALYZING";
    case AUDIO_SESSION_STATE_PLAYBACK:
        return "PLAYBACK";
    case AUDIO_SESSION_STATE_COMPLETED:
        return "COMPLETED";
    case AUDIO_SESSION_STATE_CANCELLED:
        return "CANCELLED";
    case AUDIO_SESSION_STATE_FAILED:
        return "FAILED";
    case AUDIO_SESSION_STATE_IDLE:
    default:
        return "IDLE";
    }
}
