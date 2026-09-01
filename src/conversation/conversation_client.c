#include "conversation_client.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conversation_caption_sync.h"
#include "conversation_protocol.h"
#include "conversation_turn_state.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

enum {
    CONVERSATION_WS_BUFFER_BYTES = 4096,
    /* Keep several seconds of 24 kHz PCM in PSRAM and apply bounded
     * backpressure in the WebSocket callback. Realtime services may deliver
     * audio in bursts even though playback itself is real-time. */
    CONVERSATION_AUDIO_BUFFER_BYTES = 256 * 1024,
    CONVERSATION_AUDIO_BACKPRESSURE_MS = 1500,
    CONVERSATION_AUDIO_CANCEL_POLL_MS = 20,
    CONVERSATION_WS_TASK_STACK_BYTES = 8192,
    CONVERSATION_WS_TASK_PRIORITY = 5,
    /* PCM is captured on the shared audio worker. Bound each socket write so
     * weak Wi-Fi cannot stretch a 10-second PTT turn or alarm cancellation by
     * several seconds per 100 ms chunk. */
    CONVERSATION_SEND_TIMEOUT_MS = 500,
    CONVERSATION_CLOSE_TIMEOUT_MS = 1500,
};

#define CONVERSATION_EVENT_CONNECTED BIT0
#define CONVERSATION_EVENT_STATE_CHANGED BIT1
#define CONVERSATION_EVENT_AUDIO BIT2
#define CONVERSATION_EVENT_RESPONSE_ENDED BIT3
#define CONVERSATION_EVENT_STOPPED BIT4
#define CONVERSATION_EVENT_FAILED BIT5

struct conversation_client {
    esp_websocket_client_handle_t websocket;
    SemaphoreHandle_t mutex;
    EventGroupHandle_t events;
    StreamBufferHandle_t audio_stream;
    StaticStreamBuffer_t audio_stream_control;
    uint8_t *audio_storage;
    uint8_t *audio_decode;
    char *text_message;
    char *outgoing_message;
    size_t text_length;
    uint8_t fragmented_opcode;
    bool text_message_active;
    bool closing;
    bool start_sent;
    conversation_turn_state_t turn;
    bool finish_in_progress;
    bool stop_sent;
    conversation_model_t model;
    char endpoint[CONVERSATION_ENDPOINT_MAX_LENGTH + 1U];
    conversation_caption_sync_t caption;
    conversation_client_status_t status;
};

static void secure_wipe(void *memory, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    while (bytes != NULL && size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

static void lock_client(conversation_client_t *client)
{
    if (client != NULL && client->mutex != NULL) {
        (void)xSemaphoreTake(client->mutex, portMAX_DELAY);
    }
}

static void unlock_client(conversation_client_t *client)
{
    if (client != NULL && client->mutex != NULL) {
        (void)xSemaphoreGive(client->mutex);
    }
}

static void bump_revision_locked(conversation_client_t *client)
{
    ++client->status.revision;
    if (client->status.revision == 0U) {
        client->status.revision = 1U;
    }
}

static void set_phase(conversation_client_t *client,
                      conversation_client_phase_t phase)
{
    lock_client(client);
    client->status.phase = phase;
    bump_revision_locked(client);
    unlock_client(client);
    xEventGroupSetBits(client->events, CONVERSATION_EVENT_STATE_CHANGED);
}

static void set_failed(conversation_client_t *client, esp_err_t error,
                       int service_code, const char *service_name)
{
    lock_client(client);
    client->status.phase = CONVERSATION_CLIENT_PHASE_FAILED;
    client->status.last_error = error != ESP_OK ? error : ESP_FAIL;
    client->status.service_error_code = service_code;
    if (service_name != NULL) {
        snprintf(client->status.service_error_name,
                 sizeof(client->status.service_error_name), "%s",
                 service_name);
    }
    bump_revision_locked(client);
    unlock_client(client);
    xEventGroupSetBits(client->events,
                       CONVERSATION_EVENT_FAILED |
                           CONVERSATION_EVENT_STATE_CHANGED);
}

static int format_uuid(char *output, size_t capacity, uint8_t uuid[16])
{
    uuid[6] = (uint8_t)((uuid[6] & 0x0fU) | 0x40U);
    uuid[8] = (uint8_t)((uuid[8] & 0x3fU) | 0x80U);
    return snprintf(
        output, capacity,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
        "%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6],
        uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12],
        uuid[13], uuid[14], uuid[15]);
}

static bool make_event_id(char output[CONVERSATION_EVENT_ID_CAPACITY])
{
    uint8_t uuid[16];
    esp_fill_random(uuid, sizeof(uuid));
    const int written = format_uuid(output,
                                    CONVERSATION_EVENT_ID_CAPACITY, uuid);
    secure_wipe(uuid, sizeof(uuid));
    return written == 36;
}

static esp_err_t send_text(conversation_client_t *client,
                           const char *message, size_t length)
{
    if (client == NULL || client->websocket == NULL || message == NULL ||
        length == 0U || length > INT_MAX ||
        !esp_websocket_client_is_connected(client->websocket)) {
        return ESP_ERR_INVALID_STATE;
    }
    const int sent = esp_websocket_client_send_text(
        client->websocket, message, (int)length,
        pdMS_TO_TICKS(CONVERSATION_SEND_TIMEOUT_MS));
    return sent == (int)length ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_client_event(conversation_client_t *client,
                                   conversation_client_event_t event)
{
    char event_id[CONVERSATION_EVENT_ID_CAPACITY];
    char message[128];
    size_t length = 0U;
    if (client == NULL || !make_event_id(event_id) ||
        !conversation_protocol_build_client_event(
            message, sizeof(message), event_id, event, &length)) {
        secure_wipe(event_id, sizeof(event_id));
        secure_wipe(message, sizeof(message));
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = send_text(client, message, length);
    secure_wipe(event_id, sizeof(event_id));
    secure_wipe(message, sizeof(message));
    return error;
}

static void copy_status_text(char *destination, size_t capacity,
                             const char *text)
{
    if (capacity > 0U) {
        secure_wipe(destination, capacity);
        snprintf(destination, capacity, "%s", text != NULL ? text : "");
    }
}

static bool sync_visible_caption_locked(conversation_client_t *client,
                                        bool force_complete)
{
    char visible[CONVERSATION_CLIENT_TEXT_CAPACITY];
    if (!conversation_caption_sync_copy_visible(
            &client->caption, visible, sizeof(visible),
            force_complete)) {
        return false;
    }
    const bool changed = strcmp(client->status.response, visible) != 0;
    if (changed) {
        secure_wipe(client->status.response,
                    sizeof(client->status.response));
        memcpy(client->status.response, visible, strlen(visible) + 1U);
    }
    secure_wipe(visible, sizeof(visible));
    return changed;
}

static bool response_cancel_requested(conversation_client_t *client)
{
    lock_client(client);
    const bool requested = client->turn.response_cancel_sent;
    unlock_client(client);
    return requested;
}

static bool enqueue_pcm_bytes(conversation_client_t *client,
                              const uint8_t *data, size_t length)
{
    if (length == 0U) {
        return true;
    }
    size_t sent = 0U;
    const TickType_t started = xTaskGetTickCount();
    const TickType_t timeout =
        pdMS_TO_TICKS(CONVERSATION_AUDIO_BACKPRESSURE_MS);
    TickType_t cancel_poll =
        pdMS_TO_TICKS(CONVERSATION_AUDIO_CANCEL_POLL_MS);
    if (cancel_poll == 0U) {
        cancel_poll = 1U;
    }
    bool cancelled = false;
    while (sent < length) {
        if (response_cancel_requested(client)) {
            cancelled = true;
            break;
        }
        const TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) {
            break;
        }
        const TickType_t remaining = timeout - elapsed;
        const size_t chunk = xStreamBufferSend(
            client->audio_stream, data + sent, length - sent,
            remaining < cancel_poll ? remaining : cancel_poll);
        if (chunk == 0U) {
            continue;
        }
        sent += chunk;
    }
    if (cancelled) {
        return true;
    }
    lock_client(client);
    client->status.received_audio_bytes += (uint32_t)sent;
    conversation_caption_sync_note_audio_received(
        &client->caption, sent);
    if (sent != length) {
        client->status.audio_overflow = true;
        client->status.phase = CONVERSATION_CLIENT_PHASE_FAILED;
        client->status.last_error = ESP_ERR_NO_MEM;
        snprintf(client->status.service_error_name,
                 sizeof(client->status.service_error_name), "%s",
                 "AudioBufferFull");
    }
    bump_revision_locked(client);
    unlock_client(client);
    xEventGroupSetBits(client->events, CONVERSATION_EVENT_AUDIO |
                                       CONVERSATION_EVENT_STATE_CHANGED |
                                       (sent == length
                                            ? 0U
                                            : CONVERSATION_EVENT_FAILED));
    return sent == length;
}

static void apply_server_event(conversation_client_t *client,
                               const conversation_server_event_t *event,
                               const uint8_t *audio)
{
    if (event->kind == CONVERSATION_SERVER_EVENT_AUDIO_DELTA) {
        if (response_cancel_requested(client)) {
            return;
        }
        if (audio == NULL || event->audio_length == 0U ||
            (event->audio_length & 1U) != 0U ||
            !enqueue_pcm_bytes(client, audio, event->audio_length)) {
            if (event->audio_length == 0U ||
                (event->audio_length & 1U) != 0U) {
                set_failed(client, ESP_ERR_INVALID_RESPONSE, 0,
                           "InvalidAudioDelta");
            }
            return;
        }
        if (response_cancel_requested(client)) {
            return;
        }
    }

    EventBits_t bits = CONVERSATION_EVENT_STATE_CHANGED;
    lock_client(client);
    const bool already_failed =
        client->status.phase == CONVERSATION_CLIENT_PHASE_FAILED;
    switch (event->kind) {
    case CONVERSATION_SERVER_EVENT_SESSION_CREATED:
        if (!already_failed) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_STARTING;
        }
        break;
    case CONVERSATION_SERVER_EVENT_SESSION_UPDATED:
        if (!already_failed) {
            conversation_turn_state_mark_session_ready(&client->turn);
            client->status.turn_index = client->turn.index;
            client->status.phase = CONVERSATION_CLIENT_PHASE_LISTENING;
        }
        break;
    case CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DELTA:
    case CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_DONE:
        copy_status_text(client->status.transcript,
                         sizeof(client->status.transcript), event->text);
        break;
    case CONVERSATION_SERVER_EVENT_INPUT_TRANSCRIPT_FAILED:
        /* Auxiliary ASR failure does not prevent the model from producing
         * audio/text output for the committed user turn. */
        client->status.transcript[0] = '\0';
        break;
    case CONVERSATION_SERVER_EVENT_RESPONSE_CREATED:
        client->turn.response_requested = true;
        client->turn.response_active = true;
        if (!already_failed) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_THINKING;
        }
        break;
    case CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DELTA:
        (void)conversation_caption_sync_append(
            &client->caption, event->text);
        (void)sync_visible_caption_locked(client, false);
        break;
    case CONVERSATION_SERVER_EVENT_RESPONSE_TRANSCRIPT_DONE:
        (void)conversation_caption_sync_set_final(
            &client->caption, event->text);
        (void)sync_visible_caption_locked(client, false);
        break;
    case CONVERSATION_SERVER_EVENT_AUDIO_DELTA:
        if (!already_failed) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_RESPONDING;
        }
        break;
    case CONVERSATION_SERVER_EVENT_AUDIO_DONE:
        break;
    case CONVERSATION_SERVER_EVENT_RESPONSE_DONE:
    case CONVERSATION_SERVER_EVENT_RESPONSE_TRUNCATED:
        client->turn.response_requested = false;
        client->turn.response_active = false;
        if (!already_failed) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_RESPONSE_ENDED;
        }
        client->status.response_ended = true;
        conversation_caption_sync_mark_complete(&client->caption);
        (void)sync_visible_caption_locked(
            client, client->status.received_audio_bytes == 0U);
        bits |= CONVERSATION_EVENT_RESPONSE_ENDED;
        break;
    case CONVERSATION_SERVER_EVENT_RESPONSE_CANCELLED:
        client->turn.response_requested = false;
        client->turn.response_active = false;
        if (!already_failed &&
            (client->turn.response_cancel_sent || client->closing ||
             client->finish_in_progress || client->stop_sent)) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_RESPONSE_ENDED;
            client->status.response_ended = true;
            bits |= CONVERSATION_EVENT_RESPONSE_ENDED;
        } else if (!already_failed) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_FAILED;
            client->status.last_error = ESP_ERR_INVALID_RESPONSE;
            copy_status_text(client->status.service_error_name,
                             sizeof(client->status.service_error_name),
                             "ResponseCancelled");
            bits |= CONVERSATION_EVENT_FAILED;
        }
        break;
    case CONVERSATION_SERVER_EVENT_SESSION_FINISHED:
        if (!already_failed) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_STOPPED;
        }
        bits |= CONVERSATION_EVENT_STOPPED;
        break;
    case CONVERSATION_SERVER_EVENT_ERROR:
        client->turn.response_requested = false;
        client->turn.response_active = false;
        client->status.phase = CONVERSATION_CLIENT_PHASE_FAILED;
        client->status.last_error = ESP_FAIL;
        client->status.service_error_code = event->error_code;
        copy_status_text(client->status.service_error_name,
                         sizeof(client->status.service_error_name),
                         event->error_name);
        bits |= CONVERSATION_EVENT_FAILED;
        break;
    case CONVERSATION_SERVER_EVENT_UNKNOWN:
    default:
        break;
    }
    bump_revision_locked(client);
    unlock_client(client);
    xEventGroupSetBits(client->events, bits);
}

static void finish_text_message(conversation_client_t *client)
{
    if (!client->text_message_active || client->text_length == 0U ||
        client->text_length >
            CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES) {
        client->text_length = 0U;
        client->text_message_active = false;
        return;
    }
    client->text_message[client->text_length] = '\0';
    conversation_server_event_t event = {0};
    if (!conversation_protocol_parse_server_event(
            client->text_message, client->text_length, &event,
            client->audio_decode,
            CONVERSATION_PROTOCOL_MAX_DECODED_AUDIO_BYTES)) {
        set_failed(client, ESP_ERR_INVALID_RESPONSE, 0,
                   "InvalidResponse");
    } else {
        apply_server_event(client, &event, client->audio_decode);
    }
    if (event.audio_length > 0U &&
        event.audio_length <=
            CONVERSATION_PROTOCOL_MAX_DECODED_AUDIO_BYTES) {
        secure_wipe(client->audio_decode, event.audio_length);
    }
    secure_wipe(&event, sizeof(event));
    secure_wipe(client->text_message, client->text_length + 1U);
    client->text_length = 0U;
    client->text_message_active = false;
}

static void handle_data_event(conversation_client_t *client,
                              const esp_websocket_event_data_t *data)
{
    if (data == NULL || data->data_len < 0 || data->payload_len < 0 ||
        data->payload_offset < 0 ||
        data->payload_offset > data->payload_len ||
        data->data_len > data->payload_len - data->payload_offset ||
        (data->data_len > 0 && data->data_ptr == NULL)) {
        set_failed(client, ESP_ERR_INVALID_RESPONSE, 0, "InvalidFrame");
        return;
    }
    const bool frame_complete =
        data->data_len == data->payload_len - data->payload_offset;
    uint8_t opcode = data->op_code;
    if (opcode == 0x01U) {
        if (data->payload_offset == 0) {
            client->fragmented_opcode = opcode;
            client->text_length = 0U;
            client->text_message_active = true;
        }
    } else if (opcode == 0x00U) {
        opcode = client->fragmented_opcode;
        if (opcode != 0x01U) {
            set_failed(client, ESP_ERR_INVALID_RESPONSE, 0,
                       "InvalidContinuation");
            return;
        }
    } else if (opcode == 0x02U) {
        set_failed(client, ESP_ERR_INVALID_RESPONSE, 0,
                   "UnexpectedBinaryFrame");
        return;
    } else {
        return;
    }

    const size_t fragment_length = (size_t)data->data_len;
    if (!client->text_message_active ||
        fragment_length >
            CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES -
                client->text_length) {
        set_failed(client, ESP_ERR_INVALID_SIZE, 0, "TextTooLarge");
        return;
    }
    if (fragment_length > 0U) {
        memcpy(client->text_message + client->text_length,
               data->data_ptr, fragment_length);
        client->text_length += fragment_length;
    }
    if (frame_complete && data->fin) {
        finish_text_message(client);
        client->fragmented_opcode = 0U;
    }
}

static void handle_websocket_error(
    conversation_client_t *client,
    const esp_websocket_event_data_t *data)
{
    lock_client(client);
    const bool expected_shutdown = client->closing ||
                                   client->finish_in_progress ||
                                   client->stop_sent;
    unlock_client(client);
    if (expected_shutdown) {
        return;
    }
    esp_err_t error = ESP_FAIL;
    int service_code = 0;
    const char *service_name = "TransportError";
    if (data != NULL) {
        switch (data->error_handle.error_type) {
        case WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT:
            if (data->error_handle.esp_tls_last_esp_err != ESP_OK) {
                error = data->error_handle.esp_tls_last_esp_err;
            }
            service_code = data->error_handle.esp_transport_sock_errno;
            break;
        case WEBSOCKET_ERROR_TYPE_HANDSHAKE:
            service_code = data->error_handle.esp_ws_handshake_status_code;
            if (service_code == 401) {
                service_name = "Unauthorized";
            } else if (service_code == 403) {
                service_name = "Forbidden";
            } else {
                service_name = "HandshakeError";
            }
            break;
        case WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT:
            service_name = "PongTimeout";
            break;
        case WEBSOCKET_ERROR_TYPE_SERVER_CLOSE:
            service_code = data->close_status_code;
            service_name = "ServerClose";
            break;
        case WEBSOCKET_ERROR_TYPE_NONE:
        default:
            break;
        }
    }
    set_failed(client, error, service_code, service_name);
}

static void websocket_event_handler(void *handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id, void *event_data)
{
    (void)event_base;
    conversation_client_t *client = handler_arg;
    if (client == NULL) {
        return;
    }
    switch ((esp_websocket_event_id_t)event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        lock_client(client);
        client->status.connected = true;
        client->status.last_error = ESP_OK;
        bump_revision_locked(client);
        unlock_client(client);
        xEventGroupSetBits(client->events, CONVERSATION_EVENT_CONNECTED |
                                               CONVERSATION_EVENT_STATE_CHANGED);
        break;
    case WEBSOCKET_EVENT_DATA:
        handle_data_event(client,
                          (const esp_websocket_event_data_t *)event_data);
        break;
    case WEBSOCKET_EVENT_ERROR:
        handle_websocket_error(
            client, (const esp_websocket_event_data_t *)event_data);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        lock_client(client);
        client->status.connected = false;
        const bool expected = client->closing || client->stop_sent ||
                              client->finish_in_progress;
        if (!expected &&
            client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_FAILED;
            client->status.last_error = ESP_ERR_INVALID_STATE;
        }
        bump_revision_locked(client);
        unlock_client(client);
        xEventGroupSetBits(client->events,
                           CONVERSATION_EVENT_STATE_CHANGED |
                               (expected ? 0U : CONVERSATION_EVENT_FAILED));
        break;
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
    case WEBSOCKET_EVENT_BEGIN:
    case WEBSOCKET_EVENT_FINISH:
    case WEBSOCKET_EVENT_ANY:
    case WEBSOCKET_EVENT_MAX:
    default:
        break;
    }
}

esp_err_t conversation_client_create(
    const conversation_config_snapshot_t *snapshot,
    conversation_client_t **client_out)
{
    if (snapshot == NULL || client_out == NULL ||
        !snapshot->config.enabled ||
        conversation_config_validate(&snapshot->config) !=
            CONVERSATION_CONFIG_RESULT_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    *client_out = NULL;
    conversation_client_t *client = heap_caps_calloc(
        1U, sizeof(*client), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (client == NULL) {
        client = calloc(1U, sizeof(*client));
    }
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    client->mutex = xSemaphoreCreateMutex();
    client->events = xEventGroupCreate();
    client->audio_storage = heap_caps_malloc(
        CONVERSATION_AUDIO_BUFFER_BYTES + 1U,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    client->audio_decode = heap_caps_malloc(
        CONVERSATION_PROTOCOL_MAX_DECODED_AUDIO_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    client->text_message = heap_caps_malloc(
        CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES + 1U,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    client->outgoing_message = heap_caps_malloc(
        CONVERSATION_PROTOCOL_MESSAGE_CAPACITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (client->mutex == NULL || client->events == NULL ||
        client->audio_storage == NULL || client->audio_decode == NULL ||
        client->text_message == NULL || client->outgoing_message == NULL) {
        conversation_client_destroy(client);
        return ESP_ERR_NO_MEM;
    }
    client->audio_stream = xStreamBufferCreateStatic(
        CONVERSATION_AUDIO_BUFFER_BYTES + 1U, 1U,
        client->audio_storage, &client->audio_stream_control);
    if (client->audio_stream == NULL ||
        !conversation_config_build_endpoint(
            snapshot->config.api_host, snapshot->config.model,
            client->endpoint, sizeof(client->endpoint))) {
        conversation_client_destroy(client);
        return ESP_ERR_INVALID_ARG;
    }
    client->model = snapshot->config.model;
    conversation_caption_sync_reset(&client->caption);
    conversation_turn_state_init(&client->turn);
    client->status.phase = CONVERSATION_CLIENT_PHASE_IDLE;
    client->status.revision = 1U;

    const esp_websocket_client_config_t websocket_config = {
        .uri = client->endpoint,
        .disable_auto_reconnect = true,
        .task_prio = CONVERSATION_WS_TASK_PRIORITY,
        .task_name = "cloud_voice_ws",
        .task_stack = CONVERSATION_WS_TASK_STACK_BYTES,
        .buffer_size = CONVERSATION_WS_BUFFER_BYTES,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .keep_alive_idle = 20,
        .keep_alive_interval = 10,
        .keep_alive_count = 3,
        /* The audio worker also owns alarm playback. A lost link during TLS
         * setup must not leave an alarm waiting on the WebSocket task's
         * otherwise unbounded stop wait. */
        .network_timeout_ms = 3000,
        .ping_interval_sec = 20U,
    };
    client->websocket = esp_websocket_client_init(&websocket_config);
    if (client->websocket == NULL) {
        conversation_client_destroy(client);
        return ESP_ERR_NO_MEM;
    }

    char bearer[CONVERSATION_API_KEY_MAX_LENGTH + 8U];
    const int bearer_length = snprintf(
        bearer, sizeof(bearer), "Bearer %s", snapshot->config.api_key);
    esp_err_t error = bearer_length > 7 &&
                              (size_t)bearer_length < sizeof(bearer)
                          ? esp_websocket_client_append_header(
                                client->websocket, "Authorization", bearer)
                          : ESP_ERR_INVALID_ARG;
    secure_wipe(bearer, sizeof(bearer));
    if (error == ESP_OK) {
        error = esp_websocket_register_events(
            client->websocket, WEBSOCKET_EVENT_ANY,
            websocket_event_handler, client);
    }
    if (error != ESP_OK) {
        conversation_client_destroy(client);
        return error;
    }
    *client_out = client;
    return ESP_OK;
}

esp_err_t conversation_client_start_transport(
    conversation_client_t *client)
{
    if (client == NULL || client->websocket == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    set_phase(client, CONVERSATION_CLIENT_PHASE_CONNECTING);
    const esp_err_t error = esp_websocket_client_start(client->websocket);
    if (error != ESP_OK) {
        set_failed(client, error, 0, "TransportStart");
    }
    return error;
}

esp_err_t conversation_client_send_start(conversation_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool already_sent = client->start_sent;
    unlock_client(client);
    if (already_sent ||
        !esp_websocket_client_is_connected(client->websocket)) {
        return ESP_ERR_INVALID_STATE;
    }
    char event_id[CONVERSATION_EVENT_ID_CAPACITY];
    size_t length = 0U;
    if (!make_event_id(event_id) ||
        !conversation_protocol_build_session_update(
            client->outgoing_message,
            CONVERSATION_PROTOCOL_MESSAGE_CAPACITY, event_id,
            client->model, &length)) {
        secure_wipe(event_id, sizeof(event_id));
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t error = send_text(
        client, client->outgoing_message, length);
    secure_wipe(event_id, sizeof(event_id));
    secure_wipe(client->outgoing_message, length + 1U);
    if (error == ESP_OK) {
        lock_client(client);
        client->start_sent = true;
        /* session.updated can be processed by the WebSocket task before the
         * synchronous send call returns. Never move that newer LISTENING (or
         * FAILED) state backwards to STARTING. */
        if (client->status.phase !=
                CONVERSATION_CLIENT_PHASE_LISTENING &&
            client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_STARTING;
        }
        bump_revision_locked(client);
        unlock_client(client);
        xEventGroupSetBits(client->events,
                           CONVERSATION_EVENT_STATE_CHANGED);
    }
    return error;
}

esp_err_t conversation_client_begin_next_turn(
    conversation_client_t *client)
{
    if (client == NULL || client->audio_stream == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool audio_empty =
        xStreamBufferBytesAvailable(client->audio_stream) == 0U;
    const bool can_begin = conversation_turn_state_can_begin_next(
        &client->turn, client->status.connected,
        client->status.response_ended, audio_empty) &&
        !client->closing && !client->finish_in_progress &&
        !client->stop_sent &&
        client->status.phase ==
            CONVERSATION_CLIENT_PHASE_RESPONSE_ENDED;
    if (!can_begin || xStreamBufferReset(client->audio_stream) != pdPASS ||
        !conversation_turn_state_begin_next(
            &client->turn, client->status.connected,
            client->status.response_ended, true)) {
        unlock_client(client);
        return ESP_ERR_INVALID_STATE;
    }

    client->status.speech_started = false;
    client->status.speech_ended = false;
    client->status.response_ended = false;
    client->status.audio_overflow = false;
    client->status.received_audio_bytes = 0U;
    client->status.last_error = ESP_OK;
    client->status.service_error_code = 0;
    secure_wipe(client->status.service_error_name,
                sizeof(client->status.service_error_name));
    secure_wipe(client->status.transcript,
                sizeof(client->status.transcript));
    secure_wipe(client->status.response,
                sizeof(client->status.response));
    conversation_caption_sync_reset(&client->caption);
    client->status.turn_index = client->turn.index;
    client->status.phase = CONVERSATION_CLIENT_PHASE_LISTENING;
    bump_revision_locked(client);
    unlock_client(client);
    xEventGroupClearBits(client->events,
                         CONVERSATION_EVENT_AUDIO |
                             CONVERSATION_EVENT_RESPONSE_ENDED);
    xEventGroupSetBits(client->events,
                       CONVERSATION_EVENT_STATE_CHANGED);
    return ESP_OK;
}

esp_err_t conversation_client_send_speech(conversation_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool can_start = client->start_sent && client->turn.index > 0U &&
                           !client->turn.send_speech_sent &&
                           client->status.phase ==
                               CONVERSATION_CLIENT_PHASE_LISTENING;
    if (can_start) {
        client->turn.send_speech_sent = true;
        client->status.speech_started = true;
        client->status.phase = CONVERSATION_CLIENT_PHASE_CAPTURING;
        bump_revision_locked(client);
    }
    unlock_client(client);
    if (!can_start) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(client->events, CONVERSATION_EVENT_STATE_CHANGED);
    return ESP_OK;
}

esp_err_t conversation_client_send_pcm(conversation_client_t *client,
                                       const int16_t *samples,
                                       size_t sample_count)
{
    if (client == NULL || samples == NULL || sample_count == 0U ||
        sample_count >
            CONVERSATION_PROTOCOL_MAX_UPSTREAM_PCM_BYTES /
                sizeof(samples[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_client(client);
    const bool can_send = client->turn.send_speech_sent &&
                          !client->turn.audio_committed &&
                          !client->turn.stop_speech_sent &&
                          !client->turn.cancel_speech_sent;
    unlock_client(client);
    if (!can_send ||
        !esp_websocket_client_is_connected(client->websocket)) {
        return ESP_ERR_INVALID_STATE;
    }

    char event_id[CONVERSATION_EVENT_ID_CAPACITY];
    size_t length = 0U;
    const size_t bytes = sample_count * sizeof(samples[0]);
    if (!make_event_id(event_id) ||
        !conversation_protocol_build_audio_append(
            client->outgoing_message,
            CONVERSATION_PROTOCOL_MESSAGE_CAPACITY, event_id,
            (const uint8_t *)samples, bytes, &length)) {
        secure_wipe(event_id, sizeof(event_id));
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t error = send_text(
        client, client->outgoing_message, length);
    secure_wipe(event_id, sizeof(event_id));
    secure_wipe(client->outgoing_message, length + 1U);
    if (error == ESP_OK) {
        lock_client(client);
        client->turn.audio_sent = true;
        unlock_client(client);
    }
    return error;
}

esp_err_t conversation_client_stop_speech(conversation_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool can_stop = client->turn.send_speech_sent &&
                          client->turn.audio_sent &&
                          !client->turn.stop_speech_sent &&
                          !client->turn.cancel_speech_sent;
    bool committed = client->turn.audio_committed;
    unlock_client(client);
    if (!can_stop) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = ESP_OK;
    if (!committed) {
        error = send_client_event(
            client, CONVERSATION_CLIENT_EVENT_AUDIO_COMMIT);
        if (error == ESP_OK) {
            lock_client(client);
            client->turn.audio_committed = true;
            unlock_client(client);
            committed = true;
        }
    }
    if (error == ESP_OK && committed) {
        lock_client(client);
        client->turn.response_requested = true;
        unlock_client(client);
        error = send_client_event(
            client, CONVERSATION_CLIENT_EVENT_RESPONSE_CREATE);
        if (error != ESP_OK) {
            lock_client(client);
            if (!client->turn.response_active &&
                !client->status.response_ended) {
                client->turn.response_requested = false;
            }
            unlock_client(client);
        }
    }
    if (error == ESP_OK) {
        lock_client(client);
        client->turn.stop_speech_sent = true;
        client->status.speech_ended = true;
        if (client->status.phase !=
                CONVERSATION_CLIENT_PHASE_RESPONDING &&
            client->status.phase !=
                CONVERSATION_CLIENT_PHASE_RESPONSE_ENDED &&
            client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED) {
            client->status.phase = CONVERSATION_CLIENT_PHASE_THINKING;
        }
        bump_revision_locked(client);
        unlock_client(client);
        xEventGroupSetBits(client->events,
                           CONVERSATION_EVENT_STATE_CHANGED);
    }
    return error;
}

static esp_err_t request_response_cancel(
    conversation_client_t *client)
{
    lock_client(client);
    const bool can_cancel =
        (client->turn.response_requested || client->turn.response_active) &&
        !client->status.response_ended &&
        !client->turn.response_cancel_sent;
    if (can_cancel) {
        /* Mark before the synchronous send: the WebSocket task may process
         * response.done(cancelled) before this call returns. */
        client->turn.response_cancel_sent = true;
    }
    unlock_client(client);
    if (!can_cancel) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = send_client_event(
        client, CONVERSATION_CLIENT_EVENT_RESPONSE_CANCEL);
    if (error != ESP_OK) {
        lock_client(client);
        if (!client->status.response_ended &&
            client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED) {
            client->turn.response_cancel_sent = false;
        }
        unlock_client(client);
    }
    return error;
}

esp_err_t conversation_client_cancel_response(
    conversation_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool locally_playing_completed_response =
        client->status.response_ended &&
        client->status.phase == CONVERSATION_CLIENT_PHASE_RESPONSE_ENDED &&
        client->turn.index > 0U &&
        (!client->turn.local_ended_sent ||
         xStreamBufferBytesAvailable(client->audio_stream) > 0U);
    if (locally_playing_completed_response) {
        /* response.done may arrive before buffered PCM finishes playing.
         * Treat a user interruption after that point as a local cancellation
         * so begin_next_turn can discard the remainder without sending a
         * stale response.cancel to the service. */
        client->turn.response_cancel_sent = true;
        bump_revision_locked(client);
    }
    unlock_client(client);
    if (locally_playing_completed_response) {
        xEventGroupSetBits(client->events,
                           CONVERSATION_EVENT_STATE_CHANGED);
        return ESP_OK;
    }
    return request_response_cancel(client);
}

esp_err_t conversation_client_cancel_turn(
    conversation_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool can_cancel = client->turn.send_speech_sent &&
                            !client->turn.cancel_speech_sent;
    const bool response_pending =
        (client->turn.response_requested || client->turn.response_active) &&
        !client->status.response_ended;
    const bool response_complete = client->status.response_ended;
    const bool audio_sent = client->turn.audio_sent;
    const bool audio_committed = client->turn.audio_committed;
    if (can_cancel) {
        client->turn.cancel_speech_sent = true;
        client->turn.stop_speech_sent = true;
        client->status.speech_ended = true;
        bump_revision_locked(client);
    }
    unlock_client(client);
    if (!can_cancel) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(client->events, CONVERSATION_EVENT_STATE_CHANGED);
    if (response_pending) {
        return request_response_cancel(client);
    }
    if (response_complete) {
        return conversation_client_cancel_response(client);
    }
    const esp_err_t error = audio_sent && !audio_committed
                                ? send_client_event(
                                      client,
                                      CONVERSATION_CLIENT_EVENT_AUDIO_CLEAR)
                                : ESP_OK;
    if (error == ESP_OK) {
        /* WebSocket client messages are ordered. Once clear has been sent,
         * the next turn may safely append new PCM without waiting for a
         * separate clear acknowledgement. No response was created for this
         * cancelled turn, so this is its local turn boundary. */
        lock_client(client);
        if (client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED) {
            client->status.phase =
                CONVERSATION_CLIENT_PHASE_RESPONSE_ENDED;
            client->status.response_ended = true;
            client->turn.response_requested = false;
            client->turn.response_active = false;
            bump_revision_locked(client);
        }
        unlock_client(client);
        xEventGroupSetBits(client->events,
                           CONVERSATION_EVENT_RESPONSE_ENDED |
                               CONVERSATION_EVENT_STATE_CHANGED);
    }
    return error;
}

esp_err_t conversation_client_cancel_speech(
    conversation_client_t *client)
{
    return conversation_client_cancel_turn(client);
}

esp_err_t conversation_client_local_response_started(
    conversation_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool can_start = !client->turn.local_started_sent &&
                           client->turn.index > 0U;
    if (can_start) {
        client->turn.local_started_sent = true;
    }
    unlock_client(client);
    return can_start ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t conversation_client_local_response_ended(
    conversation_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool can_end = client->turn.local_started_sent &&
                         !client->turn.local_ended_sent;
    bool caption_changed = false;
    if (can_end) {
        client->turn.local_ended_sent = true;
        if (!client->turn.response_cancel_sent) {
            caption_changed = sync_visible_caption_locked(client, true);
            if (caption_changed) {
                bump_revision_locked(client);
            }
        }
    }
    unlock_client(client);
    if (caption_changed) {
        xEventGroupSetBits(client->events,
                           CONVERSATION_EVENT_STATE_CHANGED);
    }
    return can_end ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t conversation_client_finish(conversation_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    const bool already_sent =
        client->stop_sent || client->finish_in_progress || client->closing;
    const bool response_pending =
        (client->turn.response_requested || client->turn.response_active) &&
        !client->status.response_ended &&
        !client->turn.response_cancel_sent;
    if (!already_sent) {
        client->finish_in_progress = true;
    }
    unlock_client(client);
    if (already_sent) {
        return ESP_ERR_INVALID_STATE;
    }

    lock_client(client);
    if (client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED &&
        client->status.phase != CONVERSATION_CLIENT_PHASE_STOPPED) {
        client->status.phase = CONVERSATION_CLIENT_PHASE_STOPPING;
        bump_revision_locked(client);
    }
    unlock_client(client);
    xEventGroupSetBits(client->events, CONVERSATION_EVENT_STATE_CHANGED);

    if (response_pending) {
        (void)request_response_cancel(client);
    }
    esp_err_t error = ESP_OK;
    if (client->websocket != NULL &&
        esp_websocket_client_is_connected(client->websocket)) {
        error = esp_websocket_client_close(
            client->websocket,
            pdMS_TO_TICKS(CONVERSATION_CLOSE_TIMEOUT_MS));
        if (error != ESP_OK) {
            (void)esp_websocket_client_stop(client->websocket);
            /* A bounded local shutdown is successful even when the peer does
             * not finish its close handshake before our deadline. */
            error = ESP_OK;
        }
    }
    lock_client(client);
    client->finish_in_progress = false;
    client->stop_sent = true;
    client->status.connected = false;
    if (client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED) {
        client->status.phase = CONVERSATION_CLIENT_PHASE_STOPPED;
        bump_revision_locked(client);
    }
    unlock_client(client);
    xEventGroupSetBits(client->events, CONVERSATION_EVENT_STOPPED |
                                       CONVERSATION_EVENT_STATE_CHANGED);
    return error;
}

esp_err_t conversation_client_abort(conversation_client_t *client)
{
    if (client == NULL || client->websocket == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lock_client(client);
    if (client->stop_sent || client->closing) {
        unlock_client(client);
        return ESP_ERR_INVALID_STATE;
    }
    client->closing = true;
    client->turn.response_requested = false;
    client->turn.response_active = false;
    if (client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED) {
        client->status.phase = CONVERSATION_CLIENT_PHASE_STOPPING;
        bump_revision_locked(client);
    }
    unlock_client(client);
    xEventGroupSetBits(client->events, CONVERSATION_EVENT_STATE_CHANGED);

    const esp_err_t error =
        esp_websocket_client_stop(client->websocket);
    lock_client(client);
    client->stop_sent = true;
    client->status.connected = false;
    if (client->status.phase != CONVERSATION_CLIENT_PHASE_FAILED) {
        client->status.phase = CONVERSATION_CLIENT_PHASE_STOPPED;
        bump_revision_locked(client);
    }
    unlock_client(client);
    xEventGroupSetBits(client->events, CONVERSATION_EVENT_STOPPED |
                                       CONVERSATION_EVENT_STATE_CHANGED);
    return error == ESP_ERR_INVALID_STATE ? ESP_OK : error;
}

void conversation_client_get_status(
    conversation_client_t *client, conversation_client_status_t *status)
{
    if (client == NULL || status == NULL) {
        return;
    }
    lock_client(client);
    *status = client->status;
    unlock_client(client);
}

size_t conversation_client_receive_pcm(
    conversation_client_t *client, int16_t *samples,
    size_t sample_capacity, uint32_t timeout_ms)
{
    if (client == NULL || client->audio_stream == NULL || samples == NULL ||
        sample_capacity == 0U) {
        return 0U;
    }
    const size_t bytes = xStreamBufferReceive(
        client->audio_stream, samples,
        sample_capacity * sizeof(samples[0]), pdMS_TO_TICKS(timeout_ms));
    return bytes / sizeof(samples[0]);
}

esp_err_t conversation_client_note_played_pcm(
    conversation_client_t *client, size_t sample_count)
{
    if (client == NULL || sample_count == 0U ||
        sample_count > SIZE_MAX / sizeof(int16_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_client(client);
    conversation_caption_sync_note_audio_played(
        &client->caption, sample_count * sizeof(int16_t));
    const bool changed = sync_visible_caption_locked(client, false);
    if (changed) {
        bump_revision_locked(client);
    }
    unlock_client(client);
    if (changed) {
        xEventGroupSetBits(client->events,
                           CONVERSATION_EVENT_STATE_CHANGED);
    }
    return ESP_OK;
}

size_t conversation_client_buffered_pcm_bytes(
    conversation_client_t *client)
{
    return client != NULL && client->audio_stream != NULL
               ? xStreamBufferBytesAvailable(client->audio_stream)
               : 0U;
}

void conversation_client_destroy(conversation_client_t *client)
{
    if (client == NULL) {
        return;
    }
    lock_client(client);
    client->closing = true;
    unlock_client(client);
    if (client->websocket != NULL) {
        if (esp_websocket_client_is_connected(client->websocket)) {
            const esp_err_t close_error = esp_websocket_client_close(
                client->websocket,
                pdMS_TO_TICKS(CONVERSATION_CLOSE_TIMEOUT_MS));
            if (close_error != ESP_OK) {
                (void)esp_websocket_client_stop(client->websocket);
            }
        } else {
            (void)esp_websocket_client_stop(client->websocket);
        }
        (void)esp_websocket_client_destroy(client->websocket);
        client->websocket = NULL;
    }
    if (client->events != NULL) {
        vEventGroupDelete(client->events);
        client->events = NULL;
    }
    if (client->audio_stream != NULL) {
        (void)xStreamBufferReset(client->audio_stream);
        client->audio_stream = NULL;
    }
    if (client->audio_storage != NULL) {
        secure_wipe(client->audio_storage,
                    CONVERSATION_AUDIO_BUFFER_BYTES + 1U);
        heap_caps_free(client->audio_storage);
        client->audio_storage = NULL;
    }
    if (client->audio_decode != NULL) {
        secure_wipe(client->audio_decode,
                    CONVERSATION_PROTOCOL_MAX_DECODED_AUDIO_BYTES);
        heap_caps_free(client->audio_decode);
        client->audio_decode = NULL;
    }
    if (client->text_message != NULL) {
        secure_wipe(client->text_message,
                    CONVERSATION_PROTOCOL_MAX_SERVER_MESSAGE_BYTES + 1U);
        heap_caps_free(client->text_message);
        client->text_message = NULL;
    }
    if (client->outgoing_message != NULL) {
        secure_wipe(client->outgoing_message,
                    CONVERSATION_PROTOCOL_MESSAGE_CAPACITY);
        heap_caps_free(client->outgoing_message);
        client->outgoing_message = NULL;
    }
    if (client->mutex != NULL) {
        vSemaphoreDelete(client->mutex);
        client->mutex = NULL;
    }
    secure_wipe(client, sizeof(*client));
    heap_caps_free(client);
}

const char *conversation_client_phase_name(conversation_client_phase_t phase)
{
    switch (phase) {
    case CONVERSATION_CLIENT_PHASE_CONNECTING:
        return "connecting";
    case CONVERSATION_CLIENT_PHASE_STARTING:
        return "starting";
    case CONVERSATION_CLIENT_PHASE_LISTENING:
        return "listening";
    case CONVERSATION_CLIENT_PHASE_CAPTURING:
        return "capturing";
    case CONVERSATION_CLIENT_PHASE_THINKING:
        return "thinking";
    case CONVERSATION_CLIENT_PHASE_RESPONDING:
        return "responding";
    case CONVERSATION_CLIENT_PHASE_RESPONSE_ENDED:
        return "response_ended";
    case CONVERSATION_CLIENT_PHASE_STOPPING:
        return "stopping";
    case CONVERSATION_CLIENT_PHASE_STOPPED:
        return "stopped";
    case CONVERSATION_CLIENT_PHASE_FAILED:
        return "failed";
    case CONVERSATION_CLIENT_PHASE_IDLE:
    default:
        return "idle";
    }
}
