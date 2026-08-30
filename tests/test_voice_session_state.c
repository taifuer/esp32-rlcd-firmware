#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "voice_session_state.h"

static uint32_t begin_listening(voice_session_state_t *state)
{
    uint32_t generation = 0U;
    assert(voice_session_state_begin(state, true, &generation));
    assert(generation != 0U);
    assert(voice_session_state_phase(state) ==
           VOICE_SESSION_PHASE_WAITING_FOR_RELEASE);
    assert(voice_session_state_is_active(state));
    assert(voice_session_state_is_processing(state));
    assert(voice_session_state_key_released(state) ==
           VOICE_SESSION_ACTION_START_LISTENING);
    assert(voice_session_state_phase(state) ==
           VOICE_SESSION_PHASE_LISTENING);
    return generation;
}

static void wait_for_feedback(voice_session_state_t *state,
                              voice_session_action_t expected)
{
    assert(voice_session_state_tick(
               state, VOICE_SESSION_FEEDBACK_HOLD_MS - 1U) ==
           VOICE_SESSION_ACTION_NONE);
    assert(voice_session_state_is_active(state));
    assert(voice_session_state_tick(state, 1U) == expected);
    assert(voice_session_state_phase(state) ==
           VOICE_SESSION_PHASE_READY);
    assert(!voice_session_state_is_active(state));
}

static void test_successful_session(void)
{
    voice_session_state_t state;
    voice_session_state_init(&state);
    uint32_t generation = begin_listening(&state);

    voice_session_state_note_speech(&state);
    assert(state.speech_detected);
    assert(voice_session_state_key_short_press(&state) ==
           VOICE_SESSION_ACTION_FINISH_LISTENING);
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_RECOGNIZING);

    assert(!voice_session_state_report_result(
        &state, 0U, VOICE_SESSION_RESULT_MATCHED));
    assert(!voice_session_state_report_result(
        &state, generation + 1U, VOICE_SESSION_RESULT_MATCHED));
    assert(voice_session_state_report_result(
        &state, generation, VOICE_SESSION_RESULT_MATCHED));
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_SUCCEEDED);
    assert(!voice_session_state_is_processing(&state));
    wait_for_feedback(&state,
                      VOICE_SESSION_ACTION_COMPLETE_COMMAND);
    assert(voice_session_state_generation(&state) == generation);

    assert(!voice_session_state_report_result(
        &state, generation, VOICE_SESSION_RESULT_MATCHED));
}

static void test_no_voice_and_not_understood(void)
{
    voice_session_state_t state;
    voice_session_state_init(&state);
    (void)begin_listening(&state);

    assert(voice_session_state_key_short_press(&state) ==
           VOICE_SESSION_ACTION_CANCEL_AND_CLEAR);
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_NO_VOICE);
    wait_for_feedback(&state,
                      VOICE_SESSION_ACTION_RETURN_TO_READY);

    uint32_t generation = begin_listening(&state);
    voice_session_state_note_speech(&state);
    assert(voice_session_state_finish_listening(&state) ==
           VOICE_SESSION_ACTION_FINISH_LISTENING);
    assert(voice_session_state_report_result(
        &state, generation,
        VOICE_SESSION_RESULT_NOT_UNDERSTOOD));
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_NOT_UNDERSTOOD);
    wait_for_feedback(&state,
                      VOICE_SESSION_ACTION_RETURN_TO_READY);
}

static void test_timeouts(void)
{
    voice_session_state_t state;
    voice_session_state_init(&state);
    uint32_t generation = 0U;
    assert(voice_session_state_begin(&state, true, &generation));

    assert(voice_session_state_tick(
               &state, VOICE_SESSION_RELEASE_TIMEOUT_MS - 1U) ==
           VOICE_SESSION_ACTION_NONE);
    assert(voice_session_state_tick(&state, 1U) ==
           VOICE_SESSION_ACTION_CANCEL_AND_CLEAR);
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_CANCELLED);
    assert(voice_session_state_generation(&state) != generation);
    wait_for_feedback(&state,
                      VOICE_SESSION_ACTION_RETURN_TO_READY);

    (void)begin_listening(&state);
    assert(voice_session_state_tick(
               &state, VOICE_SESSION_LISTENING_TIMEOUT_MS) ==
           VOICE_SESSION_ACTION_CANCEL_AND_CLEAR);
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_NO_VOICE);
    wait_for_feedback(&state,
                      VOICE_SESSION_ACTION_RETURN_TO_READY);

    generation = begin_listening(&state);
    voice_session_state_note_speech(&state);
    assert(voice_session_state_tick(
               &state, VOICE_SESSION_LISTENING_TIMEOUT_MS) ==
           VOICE_SESSION_ACTION_FINISH_LISTENING);
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_RECOGNIZING);
    assert(voice_session_state_tick(
               &state, VOICE_SESSION_RECOGNITION_TIMEOUT_MS) ==
           VOICE_SESSION_ACTION_CANCEL_AND_CLEAR);
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_FAILED);
    assert(!voice_session_state_report_result(
        &state, generation, VOICE_SESSION_RESULT_MATCHED));
    wait_for_feedback(&state,
                      VOICE_SESSION_ACTION_RETURN_TO_READY);

    (void)begin_listening(&state);
    state.elapsed_ms = UINT32_MAX - 1U;
    assert(voice_session_state_tick(&state, UINT32_MAX) ==
           VOICE_SESSION_ACTION_CANCEL_AND_CLEAR);
}

static uint32_t begin_waiting(voice_session_state_t *state)
{
    uint32_t generation = 0U;
    assert(voice_session_state_begin(state, true, &generation));
    assert(generation != 0U);
    assert(voice_session_state_phase(state) ==
           VOICE_SESSION_PHASE_WAITING_FOR_RELEASE);
    return generation;
}

static uint32_t begin_recognizing(voice_session_state_t *state)
{
    const uint32_t generation = begin_listening(state);
    voice_session_state_note_speech(state);
    assert(voice_session_state_finish_listening(state) ==
           VOICE_SESSION_ACTION_FINISH_LISTENING);
    assert(voice_session_state_phase(state) ==
           VOICE_SESSION_PHASE_RECOGNIZING);
    return generation;
}

static void assert_old_result_rejected(voice_session_state_t *state,
                                       uint32_t generation)
{
    assert(!voice_session_state_report_result(
        state, generation, VOICE_SESSION_RESULT_MATCHED));
}

static void cancel_with_boot_and_finish_feedback(
    voice_session_state_t *state, uint32_t generation)
{
    assert(voice_session_state_boot_short_press(state) ==
           VOICE_SESSION_ACTION_CANCEL_AND_CLEAR);
    assert(voice_session_state_phase(state) ==
           VOICE_SESSION_PHASE_CANCELLED);
    assert(voice_session_state_generation(state) != generation);
    assert_old_result_rejected(state, generation);
    assert(voice_session_state_boot_short_press(state) ==
           VOICE_SESSION_ACTION_NONE);
    wait_for_feedback(state, VOICE_SESSION_ACTION_RETURN_TO_READY);
}

static void test_boot_cancel_preemption(void)
{
    voice_session_state_t state;
    voice_session_state_init(&state);
    uint32_t generation = begin_waiting(&state);
    cancel_with_boot_and_finish_feedback(&state, generation);

    generation = begin_listening(&state);
    cancel_with_boot_and_finish_feedback(&state, generation);

    generation = begin_recognizing(&state);
    assert(voice_session_state_boot_short_press(&state) ==
           VOICE_SESSION_ACTION_CANCEL_AND_CLEAR);
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_CANCELLED);
    assert_old_result_rejected(&state, generation);
    wait_for_feedback(&state,
                      VOICE_SESSION_ACTION_RETURN_TO_READY);
}

static void alarm_preempts_and_returns_ready(
    voice_session_state_t *state, uint32_t generation)
{
    assert(voice_session_state_alarm_started(state) ==
           VOICE_SESSION_ACTION_CANCEL_AND_CLEAR);
    assert(voice_session_state_phase(state) ==
           VOICE_SESSION_PHASE_READY);
    assert(voice_session_state_generation(state) != generation);
    assert_old_result_rejected(state, generation);
    assert(voice_session_state_alarm_started(state) ==
           VOICE_SESSION_ACTION_NONE);
}

static void test_alarm_preemption_in_every_phase(void)
{
    voice_session_state_t state;
    voice_session_state_init(&state);

    uint32_t generation = begin_waiting(&state);
    alarm_preempts_and_returns_ready(&state, generation);

    generation = begin_listening(&state);
    alarm_preempts_and_returns_ready(&state, generation);

    generation = begin_recognizing(&state);
    alarm_preempts_and_returns_ready(&state, generation);

    generation = begin_recognizing(&state);
    assert(voice_session_state_report_result(
        &state, generation, VOICE_SESSION_RESULT_MATCHED));
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_SUCCEEDED);
    alarm_preempts_and_returns_ready(&state, generation);
    assert(voice_session_state_tick(
               &state, VOICE_SESSION_FEEDBACK_HOLD_MS) ==
           VOICE_SESSION_ACTION_NONE);
}

static void test_engine_and_result_failures(void)
{
    voice_session_state_t state;
    voice_session_state_init(&state);
    uint32_t generation = 0U;
    assert(voice_session_state_begin(&state, false, &generation));
    assert(generation != 0U);
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE);
    assert(voice_session_state_is_active(&state));
    assert(!voice_session_state_is_processing(&state));
    wait_for_feedback(&state,
                      VOICE_SESSION_ACTION_RETURN_TO_READY);

    const voice_session_result_t results[] = {
        VOICE_SESSION_RESULT_NO_VOICE,
        VOICE_SESSION_RESULT_TARGET_UNAVAILABLE,
        VOICE_SESSION_RESULT_ENGINE_UNAVAILABLE,
        VOICE_SESSION_RESULT_ERROR,
    };
    const voice_session_phase_t phases[] = {
        VOICE_SESSION_PHASE_NO_VOICE,
        VOICE_SESSION_PHASE_TARGET_UNAVAILABLE,
        VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE,
        VOICE_SESSION_PHASE_FAILED,
    };
    for (size_t i = 0U; i < sizeof(results) / sizeof(results[0]); ++i) {
        generation = begin_listening(&state);
        voice_session_state_note_speech(&state);
        assert(voice_session_state_finish_listening(&state) ==
               VOICE_SESSION_ACTION_FINISH_LISTENING);
        assert(voice_session_state_report_result(
            &state, generation, results[i]));
        assert(voice_session_state_phase(&state) == phases[i]);
        wait_for_feedback(&state,
                          VOICE_SESSION_ACTION_RETURN_TO_READY);
    }

    generation = begin_listening(&state);
    voice_session_state_note_speech(&state);
    assert(voice_session_state_finish_listening(&state) ==
           VOICE_SESSION_ACTION_FINISH_LISTENING);
    assert(!voice_session_state_report_result(
        &state, generation, (voice_session_result_t)99));
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_RECOGNIZING);
}

static void test_transport_failure_in_every_processing_phase(void)
{
    voice_session_state_t state;
    voice_session_state_init(&state);

    uint32_t generation = begin_waiting(&state);
    assert(!voice_session_state_report_failure(&state, generation + 1U));
    assert(voice_session_state_report_failure(&state, generation));
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_FAILED);
    wait_for_feedback(&state, VOICE_SESSION_ACTION_RETURN_TO_READY);

    generation = begin_listening(&state);
    assert(voice_session_state_report_failure(&state, generation));
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_FAILED);
    wait_for_feedback(&state, VOICE_SESSION_ACTION_RETURN_TO_READY);

    generation = begin_recognizing(&state);
    assert(voice_session_state_report_failure(&state, generation));
    assert(voice_session_state_phase(&state) ==
           VOICE_SESSION_PHASE_FAILED);
    wait_for_feedback(&state, VOICE_SESSION_ACTION_RETURN_TO_READY);

    assert(!voice_session_state_report_failure(&state, generation));
    assert(!voice_session_state_report_failure(NULL, generation));
}

static void test_generation_wrap_and_null_safety(void)
{
    voice_session_state_t state;
    voice_session_state_init(&state);
    state.generation = UINT32_MAX;
    uint32_t generation = 0U;
    assert(voice_session_state_begin(&state, true, &generation));
    assert(generation == 1U);

    uint32_t rejected_generation = 7U;
    assert(!voice_session_state_begin(
        &state, true, &rejected_generation));
    assert(rejected_generation == 0U);

    voice_session_state_init(NULL);
    assert(voice_session_state_phase(NULL) ==
           VOICE_SESSION_PHASE_READY);
    assert(voice_session_state_generation(NULL) == 0U);
    assert(!voice_session_state_is_active(NULL));
    assert(!voice_session_state_is_processing(NULL));
    assert(!voice_session_state_begin(NULL, true, NULL));
    assert(voice_session_state_key_released(NULL) ==
           VOICE_SESSION_ACTION_NONE);
    voice_session_state_note_speech(NULL);
    assert(voice_session_state_finish_listening(NULL) ==
           VOICE_SESSION_ACTION_NONE);
    assert(voice_session_state_key_short_press(NULL) ==
           VOICE_SESSION_ACTION_NONE);
    assert(voice_session_state_cancel(NULL) ==
           VOICE_SESSION_ACTION_NONE);
    assert(voice_session_state_boot_short_press(NULL) ==
           VOICE_SESSION_ACTION_NONE);
    assert(voice_session_state_alarm_started(NULL) ==
           VOICE_SESSION_ACTION_NONE);
    assert(!voice_session_state_report_result(
        NULL, 1U, VOICE_SESSION_RESULT_MATCHED));
    assert(!voice_session_state_report_failure(NULL, 1U));
    assert(voice_session_state_tick(NULL, UINT32_MAX) ==
           VOICE_SESSION_ACTION_NONE);
}

static void test_state_names(void)
{
    for (voice_session_phase_t phase = VOICE_SESSION_PHASE_READY;
         phase <= VOICE_SESSION_PHASE_FAILED; ++phase) {
        assert(voice_session_state_name(phase) != NULL);
    }
    assert(voice_session_state_name((voice_session_phase_t)99) !=
           NULL);
}

int main(void)
{
    test_successful_session();
    test_no_voice_and_not_understood();
    test_timeouts();
    test_boot_cancel_preemption();
    test_alarm_preemption_in_every_phase();
    test_engine_and_result_failures();
    test_transport_failure_in_every_processing_phase();
    test_generation_wrap_and_null_safety();
    test_state_names();

    puts("voice session state tests passed");
    return 0;
}
