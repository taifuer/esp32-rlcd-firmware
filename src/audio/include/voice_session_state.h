#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_SESSION_RELEASE_TIMEOUT_MS 5000U
#define VOICE_SESSION_LISTENING_TIMEOUT_MS 5000U
#define VOICE_SESSION_RECOGNITION_TIMEOUT_MS 3000U
#define VOICE_SESSION_FEEDBACK_HOLD_MS 2000U

typedef enum {
    VOICE_SESSION_PHASE_READY = 0,
    VOICE_SESSION_PHASE_WAITING_FOR_RELEASE,
    VOICE_SESSION_PHASE_LISTENING,
    VOICE_SESSION_PHASE_RECOGNIZING,
    VOICE_SESSION_PHASE_SUCCEEDED,
    VOICE_SESSION_PHASE_NO_VOICE,
    VOICE_SESSION_PHASE_NOT_UNDERSTOOD,
    VOICE_SESSION_PHASE_TARGET_UNAVAILABLE,
    VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE,
    VOICE_SESSION_PHASE_CANCELLED,
    VOICE_SESSION_PHASE_FAILED,
} voice_session_phase_t;

typedef enum {
    VOICE_SESSION_RESULT_MATCHED = 0,
    VOICE_SESSION_RESULT_NO_VOICE,
    VOICE_SESSION_RESULT_NOT_UNDERSTOOD,
    VOICE_SESSION_RESULT_TARGET_UNAVAILABLE,
    VOICE_SESSION_RESULT_ENGINE_UNAVAILABLE,
    VOICE_SESSION_RESULT_ERROR,
} voice_session_result_t;

typedef enum {
    VOICE_SESSION_ACTION_NONE = 0,
    /* Start capture only after the initiating KEY has been released. */
    VOICE_SESSION_ACTION_START_LISTENING,
    /* Stop capture and let the recognition worker finish. */
    VOICE_SESSION_ACTION_FINISH_LISTENING,
    /* Stop workers, reset the front end, and zero temporary PCM. */
    VOICE_SESSION_ACTION_CANCEL_AND_CLEAR,
    /* Apply the stored, policy-approved command exactly once. */
    VOICE_SESSION_ACTION_COMPLETE_COMMAND,
    /* Drop any pending command and restore the idle VOICE page. */
    VOICE_SESSION_ACTION_RETURN_TO_READY,
} voice_session_action_t;

typedef struct {
    voice_session_phase_t phase;
    uint32_t elapsed_ms;
    uint32_t generation;
    bool speech_detected;
} voice_session_state_t;

void voice_session_state_init(voice_session_state_t *state);
voice_session_phase_t voice_session_state_phase(
    const voice_session_state_t *state);
uint32_t voice_session_state_generation(
    const voice_session_state_t *state);
/* Active includes terminal feedback; processing is limited to live work. */
bool voice_session_state_is_active(const voice_session_state_t *state);
bool voice_session_state_is_processing(const voice_session_state_t *state);

/* Begin after the VOICE-page hold completes; capture starts on key release. */
bool voice_session_state_begin(voice_session_state_t *state,
                               bool engine_available,
                               uint32_t *generation);
voice_session_action_t voice_session_state_key_released(
    voice_session_state_t *state);
void voice_session_state_note_speech(voice_session_state_t *state);
voice_session_action_t voice_session_state_finish_listening(
    voice_session_state_t *state);
voice_session_action_t voice_session_state_key_short_press(
    voice_session_state_t *state);
voice_session_action_t voice_session_state_cancel(
    voice_session_state_t *state);
voice_session_action_t voice_session_state_boot_short_press(
    voice_session_state_t *state);
voice_session_action_t voice_session_state_alarm_started(
    voice_session_state_t *state);
bool voice_session_state_report_result(voice_session_state_t *state,
                                       uint32_t generation,
                                       voice_session_result_t result);
/* Transport/setup can fail before capture reaches RECOGNIZING. Accept a
 * matching failure from any live processing phase without pretending speech
 * was detected. */
bool voice_session_state_report_failure(voice_session_state_t *state,
                                        uint32_t generation);
voice_session_action_t voice_session_state_tick(
    voice_session_state_t *state, uint32_t elapsed_ms);
const char *voice_session_state_name(voice_session_phase_t phase);

#ifdef __cplusplus
}
#endif
