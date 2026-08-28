#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_VOICE_SAMPLE_RATE_HZ 16000U
#define AUDIO_VOICE_MAX_LISTENING_MS 5000U

typedef enum {
    AUDIO_VOICE_STATE_IDLE = 0,
    AUDIO_VOICE_STATE_PREPARING,
    AUDIO_VOICE_STATE_LISTENING,
    AUDIO_VOICE_STATE_COMPLETED,
    AUDIO_VOICE_STATE_CANCELLED,
    AUDIO_VOICE_STATE_FAILED,
} audio_voice_state_t;

typedef enum {
    AUDIO_VOICE_RESULT_NONE = 0,
    AUDIO_VOICE_RESULT_MATCHED,
    AUDIO_VOICE_RESULT_NO_VOICE,
    AUDIO_VOICE_RESULT_NOT_UNDERSTOOD,
    AUDIO_VOICE_RESULT_CANCELLED,
    AUDIO_VOICE_RESULT_FAILED,
} audio_voice_result_t;

typedef struct {
    bool initialized;
    bool model_ready;
    bool engine_preparing;
    bool engine_ready;
    bool microphone_ready;
    bool running;
    bool speech_detected;
    uint32_t generation;
    uint32_t elapsed_ms;
    uint32_t revision;
    int command_id;
    float confidence;
    audio_voice_state_t state;
    audio_voice_result_t result;
    esp_err_t last_error;
} audio_voice_status_t;

void audio_voice_get_status(audio_voice_status_t *status);
esp_err_t audio_voice_start(uint32_t generation);
esp_err_t audio_voice_request_stop(void);
esp_err_t audio_voice_cancel(void);
const char *audio_voice_result_name(audio_voice_result_t result);

#ifdef __cplusplus
}
#endif
