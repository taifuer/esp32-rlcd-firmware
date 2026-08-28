#include "voice_session_state.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static uint32_t add_saturating(uint32_t value, uint32_t increment)
{
    return increment > UINT32_MAX - value ? UINT32_MAX :
                                            value + increment;
}

static uint32_t next_generation(uint32_t generation)
{
    generation++;
    return generation == 0U ? 1U : generation;
}

static void set_phase(voice_session_state_t *state,
                      voice_session_phase_t phase)
{
    state->phase = phase;
    state->elapsed_ms = 0U;
}

static void invalidate_generation(voice_session_state_t *state)
{
    state->generation = next_generation(state->generation);
}

static voice_session_action_t cancel_with_feedback(
    voice_session_state_t *state)
{
    invalidate_generation(state);
    state->speech_detected = false;
    set_phase(state, VOICE_SESSION_PHASE_CANCELLED);
    return VOICE_SESSION_ACTION_CANCEL_AND_CLEAR;
}

static voice_session_action_t finish_listening(
    voice_session_state_t *state)
{
    if (state->phase != VOICE_SESSION_PHASE_LISTENING) {
        return VOICE_SESSION_ACTION_NONE;
    }

    if (!state->speech_detected) {
        set_phase(state, VOICE_SESSION_PHASE_NO_VOICE);
        return VOICE_SESSION_ACTION_CANCEL_AND_CLEAR;
    }

    set_phase(state, VOICE_SESSION_PHASE_RECOGNIZING);
    return VOICE_SESSION_ACTION_FINISH_LISTENING;
}

void voice_session_state_init(voice_session_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->phase = VOICE_SESSION_PHASE_READY;
}

voice_session_phase_t voice_session_state_phase(
    const voice_session_state_t *state)
{
    return state != NULL ? state->phase : VOICE_SESSION_PHASE_READY;
}

uint32_t voice_session_state_generation(
    const voice_session_state_t *state)
{
    return state != NULL ? state->generation : 0U;
}

bool voice_session_state_is_active(const voice_session_state_t *state)
{
    return state != NULL && state->phase != VOICE_SESSION_PHASE_READY;
}

bool voice_session_state_is_processing(const voice_session_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    return state->phase >= VOICE_SESSION_PHASE_WAITING_FOR_RELEASE &&
           state->phase <= VOICE_SESSION_PHASE_RECOGNIZING;
}

bool voice_session_state_begin(voice_session_state_t *state,
                               bool engine_available,
                               uint32_t *generation)
{
    if (generation != NULL) {
        *generation = 0U;
    }
    if (state == NULL || state->phase != VOICE_SESSION_PHASE_READY) {
        return false;
    }

    state->generation = next_generation(state->generation);
    state->speech_detected = false;
    set_phase(state, engine_available
                         ? VOICE_SESSION_PHASE_WAITING_FOR_RELEASE
                         : VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE);
    if (generation != NULL) {
        *generation = state->generation;
    }
    return true;
}

voice_session_action_t voice_session_state_key_released(
    voice_session_state_t *state)
{
    if (state == NULL ||
        state->phase != VOICE_SESSION_PHASE_WAITING_FOR_RELEASE) {
        return VOICE_SESSION_ACTION_NONE;
    }

    set_phase(state, VOICE_SESSION_PHASE_LISTENING);
    return VOICE_SESSION_ACTION_START_LISTENING;
}

void voice_session_state_note_speech(voice_session_state_t *state)
{
    if (state != NULL &&
        state->phase == VOICE_SESSION_PHASE_LISTENING) {
        state->speech_detected = true;
    }
}

voice_session_action_t voice_session_state_finish_listening(
    voice_session_state_t *state)
{
    return state != NULL ? finish_listening(state) :
                           VOICE_SESSION_ACTION_NONE;
}

voice_session_action_t voice_session_state_key_short_press(
    voice_session_state_t *state)
{
    return voice_session_state_finish_listening(state);
}

voice_session_action_t voice_session_state_boot_short_press(
    voice_session_state_t *state)
{
    return voice_session_state_cancel(state);
}

voice_session_action_t voice_session_state_cancel(
    voice_session_state_t *state)
{
    if (!voice_session_state_is_processing(state)) {
        return VOICE_SESSION_ACTION_NONE;
    }
    return cancel_with_feedback(state);
}

voice_session_action_t voice_session_state_alarm_started(
    voice_session_state_t *state)
{
    if (!voice_session_state_is_active(state)) {
        return VOICE_SESSION_ACTION_NONE;
    }

    invalidate_generation(state);
    state->speech_detected = false;
    set_phase(state, VOICE_SESSION_PHASE_READY);
    return VOICE_SESSION_ACTION_CANCEL_AND_CLEAR;
}

bool voice_session_state_report_result(voice_session_state_t *state,
                                       uint32_t generation,
                                       voice_session_result_t result)
{
    if (state == NULL ||
        state->phase != VOICE_SESSION_PHASE_RECOGNIZING ||
        generation == 0U || generation != state->generation) {
        return false;
    }

    switch (result) {
    case VOICE_SESSION_RESULT_MATCHED:
        set_phase(state, VOICE_SESSION_PHASE_SUCCEEDED);
        break;
    case VOICE_SESSION_RESULT_NO_VOICE:
        set_phase(state, VOICE_SESSION_PHASE_NO_VOICE);
        break;
    case VOICE_SESSION_RESULT_NOT_UNDERSTOOD:
        set_phase(state, VOICE_SESSION_PHASE_NOT_UNDERSTOOD);
        break;
    case VOICE_SESSION_RESULT_TARGET_UNAVAILABLE:
        set_phase(state, VOICE_SESSION_PHASE_TARGET_UNAVAILABLE);
        break;
    case VOICE_SESSION_RESULT_ENGINE_UNAVAILABLE:
        set_phase(state, VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE);
        break;
    case VOICE_SESSION_RESULT_ERROR:
        set_phase(state, VOICE_SESSION_PHASE_FAILED);
        break;
    default:
        return false;
    }
    state->speech_detected = false;
    return true;
}

voice_session_action_t voice_session_state_tick(
    voice_session_state_t *state, uint32_t elapsed_ms)
{
    if (!voice_session_state_is_active(state)) {
        return VOICE_SESSION_ACTION_NONE;
    }

    state->elapsed_ms = add_saturating(state->elapsed_ms, elapsed_ms);
    switch (state->phase) {
    case VOICE_SESSION_PHASE_WAITING_FOR_RELEASE:
        if (state->elapsed_ms >= VOICE_SESSION_RELEASE_TIMEOUT_MS) {
            return cancel_with_feedback(state);
        }
        break;
    case VOICE_SESSION_PHASE_LISTENING:
        if (state->elapsed_ms >= VOICE_SESSION_LISTENING_TIMEOUT_MS) {
            return finish_listening(state);
        }
        break;
    case VOICE_SESSION_PHASE_RECOGNIZING:
        if (state->elapsed_ms >=
            VOICE_SESSION_RECOGNITION_TIMEOUT_MS) {
            invalidate_generation(state);
            state->speech_detected = false;
            set_phase(state, VOICE_SESSION_PHASE_FAILED);
            return VOICE_SESSION_ACTION_CANCEL_AND_CLEAR;
        }
        break;
    case VOICE_SESSION_PHASE_SUCCEEDED:
    case VOICE_SESSION_PHASE_NO_VOICE:
    case VOICE_SESSION_PHASE_NOT_UNDERSTOOD:
    case VOICE_SESSION_PHASE_TARGET_UNAVAILABLE:
    case VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE:
    case VOICE_SESSION_PHASE_CANCELLED:
    case VOICE_SESSION_PHASE_FAILED:
        if (state->elapsed_ms >= VOICE_SESSION_FEEDBACK_HOLD_MS) {
            voice_session_action_t action =
                state->phase == VOICE_SESSION_PHASE_SUCCEEDED
                    ? VOICE_SESSION_ACTION_COMPLETE_COMMAND
                    : VOICE_SESSION_ACTION_RETURN_TO_READY;
            state->speech_detected = false;
            set_phase(state, VOICE_SESSION_PHASE_READY);
            return action;
        }
        break;
    case VOICE_SESSION_PHASE_READY:
    default:
        break;
    }

    return VOICE_SESSION_ACTION_NONE;
}

const char *voice_session_state_name(voice_session_phase_t phase)
{
    switch (phase) {
    case VOICE_SESSION_PHASE_WAITING_FOR_RELEASE:
        return "RELEASE KEY";
    case VOICE_SESSION_PHASE_LISTENING:
        return "LISTENING";
    case VOICE_SESSION_PHASE_RECOGNIZING:
        return "RECOGNIZING";
    case VOICE_SESSION_PHASE_SUCCEEDED:
        return "DONE";
    case VOICE_SESSION_PHASE_NO_VOICE:
        return "NO VOICE";
    case VOICE_SESSION_PHASE_NOT_UNDERSTOOD:
        return "NOT UNDERSTOOD";
    case VOICE_SESSION_PHASE_TARGET_UNAVAILABLE:
        return "NOT AVAILABLE";
    case VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE:
        return "VOICE UNAVAILABLE";
    case VOICE_SESSION_PHASE_CANCELLED:
        return "CANCELLED";
    case VOICE_SESSION_PHASE_FAILED:
        return "FAILED";
    case VOICE_SESSION_PHASE_READY:
    default:
        return "READY";
    }
}
