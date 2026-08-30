#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "conversation_config_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONVERSATION_EVENT_ID_CAPACITY 37U
#define CONVERSATION_EVENT_TEXT_CAPACITY 513U
#define CONVERSATION_ERROR_NAME_CAPACITY 65U
#define CONVERSATION_PROTOCOL_MESSAGE_CAPACITY 6144U
#define CONVERSATION_PROTOCOL_MAX_UPSTREAM_PCM_BYTES 4096U
#define CONVERSATION_PROTOCOL_MAX_DECODED_AUDIO_BYTES (24U * 1024U)
#define CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES (64U * 1024U)

typedef enum {
    CONVERSATION_CLIENT_EVENT_AUDIO_COMMIT = 0,
    CONVERSATION_CLIENT_EVENT_AUDIO_CLEAR,
    CONVERSATION_CLIENT_EVENT_RESPONSE_CREATE,
    CONVERSATION_CLIENT_EVENT_RESPONSE_CANCEL,
} conversation_client_event_t;

typedef enum {
    CONVERSATION_SERVER_EVENT_UNKNOWN = 0,
    CONVERSATION_SERVER_EVENT_SESSION_CREATED,
    CONVERSATION_SERVER_EVENT_SESSION_UPDATED,
    CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DELTA,
    CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DONE,
    CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_FAILED,
    CONVERSATION_SERVER_EVENT_RESPONSE_CREATED,
    CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DELTA,
    CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DONE,
    CONVERSATION_SERVER_EVENT_AUDIO_DELTA,
    CONVERSATION_SERVER_EVENT_AUDIO_DONE,
    CONVERSATION_SERVER_EVENT_RESPONSE_DONE,
    CONVERSATION_SERVER_EVENT_RESPONSE_CANCELLED,
    CONVERSATION_SERVER_EVENT_SESSION_FINISHED,
    CONVERSATION_SERVER_EVENT_ERROR,
} conversation_server_event_kind_t;

typedef struct {
    conversation_server_event_kind_t kind;
    char text[CONVERSATION_EVENT_TEXT_CAPACITY];
    int error_code;
    char error_name[CONVERSATION_ERROR_NAME_CAPACITY];
    size_t audio_length;
} conversation_server_event_t;

/* Model selection is controlled by conversation_model_t so voice and session
 * fields cannot be mixed across incompatible model families. */
bool conversation_protocol_build_session_update(
    char *output, size_t capacity, const char *event_id,
    conversation_model_t model, size_t *written);
bool conversation_protocol_build_audio_append(
    char *output, size_t capacity, const char *event_id,
    const uint8_t *pcm, size_t pcm_length, size_t *written);
bool conversation_protocol_build_client_event(
    char *output, size_t capacity, const char *event_id,
    conversation_client_event_t event, size_t *written);

/* Parses one complete bounded WebSocket text message. Audio delta payloads are
 * strictly decoded into caller-owned storage and never copied into status or
 * logs. For non-audio events audio may be NULL with zero capacity. */
bool conversation_protocol_parse_server_event(
    const char *json, size_t length, conversation_server_event_t *event,
    uint8_t *audio, size_t audio_capacity);

const char *conversation_client_event_name(
    conversation_client_event_t event);
const char *conversation_server_event_name(
    conversation_server_event_kind_t kind);

#ifdef __cplusplus
}
#endif
