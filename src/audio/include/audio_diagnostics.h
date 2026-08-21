#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_session_state.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ 24000U
#define AUDIO_DIAGNOSTICS_BITS_PER_SAMPLE 16U
#define AUDIO_DIAGNOSTICS_MAX_RECORDING_MS 5000U

typedef enum {
    AUDIO_DIAGNOSTICS_RESULT_NOT_RUN = 0,
    AUDIO_DIAGNOSTICS_RESULT_INPUT_DETECTED,
    AUDIO_DIAGNOSTICS_RESULT_CHECK_INPUT,
    AUDIO_DIAGNOSTICS_RESULT_FAILED,
} audio_diagnostics_result_t;

typedef struct {
    bool initialized;
    bool speaker_ready;
    bool microphones_ready;
    bool running;
    bool test_completed;
    bool tone_played;
    bool microphone_capture_completed;
    bool voice_played;
    bool playback_stopped;
    uint8_t microphone_1_level_percent;
    uint8_t microphone_2_level_percent;
    uint16_t microphone_1_rms;
    uint16_t microphone_2_rms;
    uint8_t playback_microphone;
    uint32_t recording_elapsed_ms;
    uint32_t recording_duration_ms;
    uint32_t playback_elapsed_ms;
    uint32_t revision;
    audio_session_state_t state;
    audio_diagnostics_result_t result;
    esp_err_t last_error;
} audio_diagnostics_status_t;

esp_err_t audio_diagnostics_init(i2c_master_bus_handle_t i2c_bus);
void audio_diagnostics_get_status(audio_diagnostics_status_t *status);
esp_err_t audio_diagnostics_start(void);
esp_err_t audio_diagnostics_request_stop(void);
esp_err_t audio_diagnostics_cancel(void);
const char *audio_diagnostics_result_name(audio_diagnostics_result_t result);

#ifdef __cplusplus
}
#endif
