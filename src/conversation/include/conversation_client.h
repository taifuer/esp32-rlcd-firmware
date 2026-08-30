#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "conversation_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONVERSATION_UPSTREAM_PCM_SAMPLE_RATE_HZ 16000U
#define CONVERSATION_DOWNSTREAM_PCM_SAMPLE_RATE_HZ 24000U
#define CONVERSATION_DOWNSTREAM_PCM_BYTES_PER_SECOND 48000U
#define CONVERSATION_CLIENT_TEXT_CAPACITY 513U

typedef enum {
    CONVERSATION_CLIENT_PHASE_IDLE = 0,
    CONVERSATION_CLIENT_PHASE_CONNECTING,
    CONVERSATION_CLIENT_PHASE_STARTING,
    CONVERSATION_CLIENT_PHASE_LISTENING,
    CONVERSATION_CLIENT_PHASE_CAPTURING,
    CONVERSATION_CLIENT_PHASE_THINKING,
    CONVERSATION_CLIENT_PHASE_RESPONDING,
    CONVERSATION_CLIENT_PHASE_RESPONSE_ENDED,
    CONVERSATION_CLIENT_PHASE_STOPPING,
    CONVERSATION_CLIENT_PHASE_STOPPED,
    CONVERSATION_CLIENT_PHASE_FAILED,
} conversation_client_phase_t;

typedef struct {
    conversation_client_phase_t phase;
    bool connected;
    bool speech_started;
    bool speech_ended;
    bool response_ended;
    bool audio_overflow;
    uint32_t revision;
    uint32_t received_audio_bytes;
    esp_err_t last_error;
    int service_error_code;
    char service_error_name[65];
    char transcript[CONVERSATION_CLIENT_TEXT_CAPACITY];
    char response[CONVERSATION_CLIENT_TEXT_CAPACITY];
} conversation_client_status_t;

typedef struct conversation_client conversation_client_t;

/* The configuration snapshot is copied by the transport during create; the
 * caller remains responsible for wiping its own snapshot afterwards. */
esp_err_t conversation_client_create(
    const conversation_config_snapshot_t *snapshot,
    conversation_client_t **client);
esp_err_t conversation_client_start_transport(
    conversation_client_t *client);
esp_err_t conversation_client_send_start(conversation_client_t *client);
esp_err_t conversation_client_send_speech(conversation_client_t *client);
esp_err_t conversation_client_send_pcm(conversation_client_t *client,
                                       const int16_t *samples,
                                       size_t sample_count);
esp_err_t conversation_client_stop_speech(conversation_client_t *client);
esp_err_t conversation_client_cancel_speech(conversation_client_t *client);
esp_err_t conversation_client_local_response_started(
    conversation_client_t *client);
esp_err_t conversation_client_local_response_ended(
    conversation_client_t *client);
esp_err_t conversation_client_finish(conversation_client_t *client);
/* Skip the protocol/close handshake and request a direct transport stop.
 * Reserved for higher-priority local audio such as an alarm preempting a
 * cloud turn; the underlying task may still take its bounded network timeout
 * to leave a TLS operation. */
esp_err_t conversation_client_abort(conversation_client_t *client);

void conversation_client_get_status(
    conversation_client_t *client, conversation_client_status_t *status);
size_t conversation_client_receive_pcm(
    conversation_client_t *client, int16_t *samples,
    size_t sample_capacity, uint32_t timeout_ms);
size_t conversation_client_buffered_pcm_bytes(
    conversation_client_t *client);

/* Performs a bounded clean close when possible, then releases and wipes all
 * transient text/audio buffers. Safe for partially initialized sessions. */
void conversation_client_destroy(conversation_client_t *client);
const char *conversation_client_phase_name(conversation_client_phase_t phase);

#ifdef __cplusplus
}
#endif
