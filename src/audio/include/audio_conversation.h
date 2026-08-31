#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_CONVERSATION_CAPTURE_SAMPLE_RATE_HZ 16000U
#define AUDIO_CONVERSATION_PLAYBACK_SAMPLE_RATE_HZ 24000U
#define AUDIO_CONVERSATION_MAX_LISTENING_MS 10000U
#define AUDIO_CONVERSATION_TEXT_CAPACITY 513U

typedef enum {
    AUDIO_CONVERSATION_STATE_IDLE = 0,
    AUDIO_CONVERSATION_STATE_CONNECTING,
    AUDIO_CONVERSATION_STATE_WAITING_FOR_RELEASE,
    AUDIO_CONVERSATION_STATE_LISTENING,
    AUDIO_CONVERSATION_STATE_THINKING,
    AUDIO_CONVERSATION_STATE_SPEAKING,
    AUDIO_CONVERSATION_STATE_ADVANCING,
    AUDIO_CONVERSATION_STATE_FOLLOW_UP,
    AUDIO_CONVERSATION_STATE_COMPLETED,
    AUDIO_CONVERSATION_STATE_CANCELLED,
    AUDIO_CONVERSATION_STATE_FAILED,
} audio_conversation_state_t;

typedef struct {
    bool initialized;
    bool microphone_ready;
    bool speaker_ready;
    bool running;
    uint32_t generation;
    uint32_t elapsed_ms;
    uint32_t session_elapsed_ms;
    uint32_t revision;
    uint8_t turn_number;
    uint8_t max_turns;
    audio_conversation_state_t state;
    esp_err_t last_error;
    int service_error_code;
    char service_error_name[65];
    char transcript[AUDIO_CONVERSATION_TEXT_CAPACITY];
    char response[AUDIO_CONVERSATION_TEXT_CAPACITY];
} audio_conversation_status_t;

void audio_conversation_get_status(audio_conversation_status_t *status);
/* Start network/protocol preparation as soon as the VOICE-page hold is
 * accepted. Capture begins only after release_key() is called. */
esp_err_t audio_conversation_start(uint32_t generation);
esp_err_t audio_conversation_release_key(void);
esp_err_t audio_conversation_request_stop(void);
/* Continue a retained cloud session from FOLLOW_UP. While SPEAKING, this
 * first cancels and drains the current response before opening the next
 * microphone turn. */
esp_err_t audio_conversation_continue(void);
/* Gracefully end from FOLLOW_UP while retaining the last visible result. */
esp_err_t audio_conversation_end(void);
esp_err_t audio_conversation_cancel(void);
esp_err_t audio_conversation_dismiss(void);
const char *audio_conversation_state_name(
    audio_conversation_state_t state);

#ifdef __cplusplus
}
#endif
