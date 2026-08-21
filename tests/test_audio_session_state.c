#include <assert.h>
#include <stdio.h>

#include "audio_session_state.h"

int main(void)
{
    assert(!audio_session_state_is_active(AUDIO_SESSION_STATE_IDLE));
    assert(audio_session_state_is_active(
        AUDIO_SESSION_STATE_PLAYING_TONE));
    assert(audio_session_state_is_active(
        AUDIO_SESSION_STATE_PREPARING_RECORDING));
    assert(audio_session_state_is_active(AUDIO_SESSION_STATE_RECORDING));
    assert(audio_session_state_is_active(AUDIO_SESSION_STATE_ANALYZING));
    assert(audio_session_state_is_active(AUDIO_SESSION_STATE_PLAYBACK));
    assert(!audio_session_state_is_active(
        AUDIO_SESSION_STATE_COMPLETED));
    assert(!audio_session_state_is_active(
        AUDIO_SESSION_STATE_CANCELLED));
    assert(!audio_session_state_is_active(AUDIO_SESSION_STATE_FAILED));

    assert(audio_session_input_action(
               AUDIO_SESSION_STATE_RECORDING,
               AUDIO_SESSION_INPUT_KEY_SHORT_PRESS) ==
           AUDIO_SESSION_ACTION_STOP);
    assert(audio_session_input_action(
               AUDIO_SESSION_STATE_PLAYBACK,
               AUDIO_SESSION_INPUT_KEY_SHORT_PRESS) ==
           AUDIO_SESSION_ACTION_STOP);
    assert(audio_session_input_action(
               AUDIO_SESSION_STATE_PLAYING_TONE,
               AUDIO_SESSION_INPUT_KEY_SHORT_PRESS) ==
           AUDIO_SESSION_ACTION_NONE);
    assert(audio_session_input_action(
               AUDIO_SESSION_STATE_ANALYZING,
               AUDIO_SESSION_INPUT_KEY_SHORT_PRESS) ==
           AUDIO_SESSION_ACTION_NONE);

    for (audio_session_state_t state =
             AUDIO_SESSION_STATE_PLAYING_TONE;
         state <= AUDIO_SESSION_STATE_PLAYBACK; ++state) {
        assert(audio_session_input_action(
                   state, AUDIO_SESSION_INPUT_BOOT_SHORT_PRESS) ==
               AUDIO_SESSION_ACTION_CANCEL);
    }
    assert(audio_session_input_action(
               AUDIO_SESSION_STATE_COMPLETED,
               AUDIO_SESSION_INPUT_BOOT_SHORT_PRESS) ==
           AUDIO_SESSION_ACTION_NONE);
    assert(audio_session_input_action(
               AUDIO_SESSION_STATE_RECORDING,
               AUDIO_SESSION_INPUT_NONE) ==
           AUDIO_SESSION_ACTION_NONE);

    assert(audio_session_state_name(AUDIO_SESSION_STATE_RECORDING) !=
           NULL);
    puts("audio session state tests passed");
    return 0;
}
