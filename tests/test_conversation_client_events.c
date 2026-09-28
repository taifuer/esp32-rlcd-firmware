/* Exercise the production client's registered callback and public API with
 * local event injection. No sockets, cloud calls, credentials or device are
 * involved; these fakes do not claim to test ESP-IDF scheduling or TLS. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conversation_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_tls_errors.h"
#include "esp_websocket_client.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

struct test_client_mutex { bool held; };
struct test_client_events { EventBits_t bits; };
struct test_websocket {
    esp_event_handler_t handler;
    void *argument;
    bool connected;
    bool started;
};

typedef struct { void *pointer; size_t size; } allocation_t;
static allocation_t allocations[8];
static unsigned allocation_attempts;
static unsigned fail_allocation;
static unsigned heap_live;
static unsigned mutex_live;
static unsigned event_live;
static unsigned text_sends;
static unsigned stop_calls;
static TickType_t now;
static struct test_websocket *websocket;
static char captured_logs[4096];
static size_t log_length;

typedef enum {
    SEND_EVENT_NONE = 0,
    SEND_EVENT_SESSION_UPDATED,
    SEND_EVENT_CLOSED,
} synchronous_send_event_t;
static synchronous_send_event_t synchronous_send_event;

static void emit_event(esp_websocket_event_id_t event,
                        esp_websocket_event_data_t *data);
static void emit_json(const char *json);

void conversation_client_test_log(const char *tag, const char *format, ...)
{
    (void)tag;
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(captured_logs + log_length,
                                   sizeof(captured_logs) - log_length,
                                   format, arguments);
    va_end(arguments);
    assert(written >= 0 &&
           (size_t)written < sizeof(captured_logs) - log_length);
    log_length += (size_t)written;
}

const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "ESP_OK" : "TEST_ERROR";
}

void *heap_caps_malloc(size_t size, unsigned caps)
{
    assert((caps & MALLOC_CAP_8BIT) != 0U);
    if (++allocation_attempts == fail_allocation) {
        return NULL;
    }
    void *pointer = malloc(size);
    assert(pointer != NULL);
    memset(pointer, 0xa5, size);
    for (size_t index = 0U; index < 8U; ++index) {
        if (allocations[index].pointer == NULL) {
            allocations[index] = (allocation_t){pointer, size};
            ++heap_live;
            return pointer;
        }
    }
    assert(false);
    return NULL;
}

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    assert(size == 0U || count <= SIZE_MAX / size);
    void *pointer = heap_caps_malloc(count * size, caps);
    if (pointer != NULL) {
        memset(pointer, 0, count * size);
    }
    return pointer;
}

void heap_caps_free(void *pointer)
{
    if (pointer == NULL) {
        return;
    }
    for (size_t index = 0U; index < 8U; ++index) {
        if (allocations[index].pointer == pointer) {
            const uint8_t *bytes = pointer;
            for (size_t offset = 0U; offset < allocations[index].size; ++offset) {
                assert(bytes[offset] == 0U); /* text, PCM and client wiped */
            }
            free(pointer);
            allocations[index] = (allocation_t){0};
            --heap_live;
            return;
        }
    }
    assert(false);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t mutex = calloc(1U, sizeof(*mutex));
    assert(mutex != NULL);
    ++mutex_live;
    return mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout)
{
    (void)timeout;
    assert(mutex != NULL && !mutex->held);
    mutex->held = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex != NULL && mutex->held);
    mutex->held = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    assert(mutex != NULL && !mutex->held);
    free(mutex);
    --mutex_live;
}

EventGroupHandle_t xEventGroupCreate(void)
{
    EventGroupHandle_t events = calloc(1U, sizeof(*events));
    assert(events != NULL);
    ++event_live;
    return events;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t events, EventBits_t bits)
{
    assert(events != NULL);
    events->bits |= bits;
    return events->bits;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t events, EventBits_t bits)
{
    assert(events != NULL);
    const EventBits_t previous = events->bits;
    events->bits &= ~bits;
    return previous;
}

void vEventGroupDelete(EventGroupHandle_t events)
{
    assert(events != NULL);
    free(events);
    --event_live;
}

StreamBufferHandle_t xStreamBufferCreateStatic(size_t bytes, size_t trigger,
                                              uint8_t *storage,
                                              StaticStreamBuffer_t *control)
{
    assert(bytes > 1U && trigger == 1U && storage != NULL && control != NULL);
    *control = (StaticStreamBuffer_t){.storage = storage, .capacity = bytes - 1U};
    return control;
}

size_t xStreamBufferSend(StreamBufferHandle_t stream, const void *data,
                         size_t size, TickType_t timeout)
{
    assert(stream != NULL && data != NULL);
    const size_t free_bytes = stream->capacity - stream->count;
    if (size > free_bytes) {
        size = free_bytes;
    }
    const uint8_t *bytes = data;
    for (size_t offset = 0U; offset < size; ++offset) {
        stream->storage[stream->write_index] = bytes[offset];
        stream->write_index = (stream->write_index + 1U) % stream->capacity;
    }
    stream->count += size;
    if (size == 0U) {
        now += timeout;
    }
    return size;
}

size_t xStreamBufferReceive(StreamBufferHandle_t stream, void *data,
                            size_t size, TickType_t timeout)
{
    assert(stream != NULL && data != NULL);
    if (size > stream->count) {
        size = stream->count;
    }
    uint8_t *bytes = data;
    for (size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = stream->storage[stream->read_index];
        stream->read_index = (stream->read_index + 1U) % stream->capacity;
    }
    stream->count -= size;
    if (size == 0U) {
        now += timeout;
    }
    return size;
}

size_t xStreamBufferBytesAvailable(StreamBufferHandle_t stream)
{
    assert(stream != NULL);
    return stream->count;
}

BaseType_t xStreamBufferReset(StreamBufferHandle_t stream)
{
    assert(stream != NULL);
    stream->read_index = 0U;
    stream->write_index = 0U;
    stream->count = 0U;
    return pdPASS;
}

TickType_t xTaskGetTickCount(void) { return now; }
void vTaskDelay(TickType_t ticks) { now += ticks; }
esp_err_t esp_crt_bundle_attach(void *configuration)
{
    (void)configuration;
    return ESP_OK;
}

void esp_fill_random(void *buffer, size_t length)
{
    memset(buffer, 0x3c, length);
}

esp_websocket_client_handle_t esp_websocket_client_init(
    const esp_websocket_client_config_t *configuration)
{
    assert(configuration != NULL && websocket == NULL);
    assert(configuration->disable_auto_reconnect);
    assert(!configuration->enable_close_reconnect);
    assert(configuration->crt_bundle_attach == esp_crt_bundle_attach);
    assert(configuration->network_timeout_ms > 0 &&
           configuration->network_timeout_ms <= 3000);
    assert(strstr(configuration->uri, "model=qwen-audio-3.0-realtime-flash") != NULL);
    websocket = calloc(1U, sizeof(*websocket));
    assert(websocket != NULL);
    return websocket;
}

esp_err_t esp_websocket_client_append_header(esp_websocket_client_handle_t client,
                                            const char *key, const char *value)
{
    assert(client == websocket && strcmp(key, "Authorization") == 0);
    assert(strcmp(value, "Bearer sk-fixture-only-no-network") == 0);
    return ESP_OK;
}

esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client,
                                        esp_websocket_event_id_t event,
                                        esp_event_handler_t handler, void *arg)
{
    assert(client == websocket && event == WEBSOCKET_EVENT_ANY);
    assert(handler != NULL && arg != NULL);
    client->handler = handler;
    client->argument = arg;
    return ESP_OK;
}

esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client)
{
    assert(client == websocket && !client->started);
    client->started = true;
    return ESP_OK;
}

bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client)
{
    assert(client == websocket && client != NULL);
    return client->connected;
}

int esp_websocket_client_send_text(esp_websocket_client_handle_t client,
                                   const char *message, int length,
                                   TickType_t timeout)
{
    assert(client == websocket && client->connected);
    assert(message != NULL && length > 0 && timeout <= 500U);
    assert((size_t)length == strlen(message));
    assert(strstr(message, "sk-fixture-only-no-network") == NULL);
    ++text_sends;
    const synchronous_send_event_t event = synchronous_send_event;
    synchronous_send_event = SEND_EVENT_NONE;
    if (event == SEND_EVENT_SESSION_UPDATED) {
        emit_json("{\"type\":\"session.updated\"}");
    } else if (event == SEND_EVENT_CLOSED) {
        esp_websocket_event_data_t data = {.close_status_code = 1000};
        emit_event(WEBSOCKET_EVENT_CLOSED, &data);
    }
    return length;
}

esp_err_t esp_websocket_client_close(esp_websocket_client_handle_t client,
                                     TickType_t timeout)
{
    assert(client == websocket && timeout <= 1500U);
    if (!client->started) {
        return ESP_FAIL;
    }
    esp_websocket_event_data_t data = {.close_status_code = 1000};
    emit_event(WEBSOCKET_EVENT_CLOSED, &data);
    emit_event(WEBSOCKET_EVENT_FINISH, NULL);
    return ESP_OK;
}

esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client)
{
    assert(client == websocket);
    ++stop_calls;
    if (!client->started) {
        return ESP_FAIL; /* pinned upstream reports an already stopped task */
    }
    emit_event(WEBSOCKET_EVENT_DISCONNECTED, NULL);
    emit_event(WEBSOCKET_EVENT_FINISH, NULL);
    return ESP_OK;
}

esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client)
{
    assert(client == websocket && client != NULL);
    free(client);
    websocket = NULL;
    return ESP_OK;
}

static void emit_event(esp_websocket_event_id_t event,
                        esp_websocket_event_data_t *data)
{
    assert(websocket != NULL && websocket->handler != NULL);
    if (event == WEBSOCKET_EVENT_CONNECTED) {
        websocket->connected = true;
    } else if (event == WEBSOCKET_EVENT_CLOSED ||
               event == WEBSOCKET_EVENT_DISCONNECTED ||
               event == WEBSOCKET_EVENT_FINISH) {
        websocket->connected = false;
    }
    if (event == WEBSOCKET_EVENT_FINISH) {
        websocket->started = false;
    }
    websocket->handler(websocket->argument, "test_websocket", event, data);
}

static void emit_json(const char *json)
{
    const size_t length = strlen(json);
    assert(length <= INT32_MAX);
    esp_websocket_event_data_t data = {
        .data_ptr = json,
        .data_len = (int)length,
        .fin = true,
        .op_code = 1U,
        .payload_len = (int)length,
    };
    emit_event(WEBSOCKET_EVENT_DATA, &data);
}

static conversation_client_status_t status_of(conversation_client_t *client)
{
    conversation_client_status_t status = {0};
    conversation_client_get_status(client, &status);
    return status;
}

static void reset_fixture(void)
{
    assert(heap_live == 0U && mutex_live == 0U && event_live == 0U);
    assert(websocket == NULL);
    allocation_attempts = 0U;
    fail_allocation = 0U;
    text_sends = 0U;
    stop_calls = 0U;
    now = 0U;
    synchronous_send_event = SEND_EVENT_NONE;
    memset(captured_logs, 0, sizeof(captured_logs));
    log_length = 0U;
}

static conversation_config_snapshot_t fixture_configuration(void)
{
    conversation_config_snapshot_t snapshot = {.generation = 1U};
    conversation_config_defaults(&snapshot.config);
    snapshot.config.enabled = true;
    snprintf(snapshot.config.api_key, sizeof(snapshot.config.api_key), "%s",
             "sk-fixture-only-no-network");
    return snapshot;
}

static conversation_client_t *create_client(void)
{
    conversation_config_snapshot_t snapshot = fixture_configuration();
    conversation_client_t *client = NULL;
    assert(conversation_client_create(&snapshot, &client) == ESP_OK);
    conversation_config_clear_sensitive(&snapshot, sizeof(snapshot));
    assert(client != NULL);
    assert(conversation_client_start_transport(client) == ESP_OK);
    return client;
}

static void destroy_client(conversation_client_t *client)
{
    conversation_client_destroy(client);
    assert(heap_live == 0U && mutex_live == 0U && event_live == 0U);
    assert(websocket == NULL);
    assert(strstr(captured_logs, "sk-fixture-only-no-network") == NULL);
    assert(strstr(captured_logs, "server-message-secret") == NULL);
    assert(strstr(captured_logs, "close-reason-secret") == NULL);
}

static void ready_client(conversation_client_t *client)
{
    emit_event(WEBSOCKET_EVENT_CONNECTED, NULL);
    emit_json("{\"type\":\"session.created\"}");
    assert(conversation_client_send_start(client) == ESP_OK);
    emit_json("{\"type\":\"session.updated\"}");
    const conversation_client_status_t status = status_of(client);
    assert(status.phase == CONVERSATION_CLIENT_PHASE_LISTENING);
    assert(status.connected && status.turn_index == 1U);
}

static void assert_terminal_failure(conversation_client_t *client)
{
    const conversation_client_status_t status = status_of(client);
    assert(status.phase == CONVERSATION_CLIENT_PHASE_FAILED);
    assert(!status.connected);
    assert(status.last_error != ESP_OK && status.last_error != ESP_ERR_TIMEOUT);
}

static void test_early_close_and_finish_fail_immediately(void)
{
    const esp_websocket_event_id_t endings[] = {
        WEBSOCKET_EVENT_CLOSED, WEBSOCKET_EVENT_FINISH,
        WEBSOCKET_EVENT_DISCONNECTED,
    };
    for (size_t event = 0U; event < sizeof(endings) / sizeof(endings[0]); ++event) {
        for (unsigned stage = 0U; stage < 4U; ++stage) {
            reset_fixture();
            conversation_client_t *client = create_client();
            if (stage > 0U) emit_event(WEBSOCKET_EVENT_CONNECTED, NULL);
            if (stage > 1U) emit_json("{\"type\":\"session.created\"}");
            if (stage > 2U) assert(conversation_client_send_start(client) == ESP_OK);
            esp_websocket_event_data_t data = {.close_status_code = 1000};
            emit_event(endings[event], &data);
            assert_terminal_failure(client);
            const conversation_client_status_t status = status_of(client);
            assert(status.diagnostics.websocket_connected == (stage > 0U));
            assert(status.diagnostics.session_created == (stage > 1U));
            assert(status.diagnostics.session_update_sent == (stage > 2U));
            assert(!status.diagnostics.session_ready);
            assert(status.diagnostics.close_code == 1000U);
            assert(status.diagnostics.transport_end ==
                   (endings[event] == WEBSOCKET_EVENT_CLOSED
                        ? CONVERSATION_TRANSPORT_END_CLOSED
                        : endings[event] == WEBSOCKET_EVENT_FINISH
                              ? CONVERSATION_TRANSPORT_END_FINISHED
                              : CONVERSATION_TRANSPORT_END_DISCONNECTED));
            assert(now == 0U); /* no artificial protocol-timeout wait */
            destroy_client(client);
        }
    }
}

static void test_protocol_session_finished_is_terminal(void)
{
    for (unsigned ready = 0U; ready <= 1U; ++ready) {
        reset_fixture();
        conversation_client_t *client = create_client();
        if (ready) {
            ready_client(client);
        } else {
            emit_event(WEBSOCKET_EVENT_CONNECTED, NULL);
        }
        emit_json("{\"type\":\"session.finished\"}");
        assert_terminal_failure(client);
        const conversation_client_status_t status = status_of(client);
        assert(strcmp(status.service_error_name, "TransportStopped") == 0);
        assert(status.diagnostics.transport_end ==
               CONVERSATION_TRANSPORT_END_FINISHED);
        assert(status.diagnostics.session_ready == (ready != 0U));
        assert(status.diagnostics.close_code == 0U && now == 0U);
        destroy_client(client);
    }
}

static void test_service_error_is_not_overwritten_by_transport_end(void)
{
    reset_fixture();
    conversation_client_t *client = create_client();
    ready_client(client);
    emit_json("{\"type\":\"error\",\"error\":{\"code\":\"InvalidApiKey\","
              "\"message\":\"server-message-secret\"}}");
    const conversation_client_status_t failed = status_of(client);
    assert(failed.phase == CONVERSATION_CLIENT_PHASE_FAILED);
    assert(strcmp(failed.service_error_name, "InvalidApiKey") == 0);
    esp_websocket_event_data_t data = {.close_status_code = 1008};
    emit_event(WEBSOCKET_EVENT_CLOSED, &data);
    emit_event(WEBSOCKET_EVENT_DISCONNECTED, NULL);
    emit_event(WEBSOCKET_EVENT_FINISH, NULL);
    const conversation_client_status_t ended = status_of(client);
    assert(ended.phase == CONVERSATION_CLIENT_PHASE_FAILED && !ended.connected);
    assert(ended.last_error == failed.last_error);
    assert(ended.service_error_code == failed.service_error_code);
    assert(strcmp(ended.service_error_name, failed.service_error_name) == 0);
    assert(ended.diagnostics.close_code == 1008U);
    assert(ended.diagnostics.transport_end == CONVERSATION_TRANSPORT_END_CLOSED);
    destroy_client(client);
}

static void test_handshake_error_retains_http_status(void)
{
    const int http_statuses[] = {401, 403, 429, 503};
    for (size_t index = 0U; index < sizeof(http_statuses) / sizeof(http_statuses[0]); ++index) {
        reset_fixture();
        conversation_client_t *client = create_client();
        esp_websocket_event_data_t data = {
            .error_handle = {
                .error_type = WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT,
                .esp_ws_handshake_status_code = http_statuses[index],
                .esp_transport_sock_errno = 104,
            },
        };
        emit_event(WEBSOCKET_EVENT_ERROR, &data);
        emit_event(WEBSOCKET_EVENT_FINISH, NULL);
        const conversation_client_status_t status = status_of(client);
        assert_terminal_failure(client);
        assert(status.service_error_code == http_statuses[index]);
        assert(strcmp(status.service_error_name, "HandshakeError") == 0);
        destroy_client(client);
    }
}

static void emit_late_session_events(void)
{
    emit_event(WEBSOCKET_EVENT_CONNECTED, NULL);
    emit_json("{\"type\":\"session.created\"}");
    emit_json("{\"type\":\"session.updated\"}");
    emit_json("{\"type\":\"response.audio.delta\",\"delta\":\"AAH+/w==\"}");
}

static void test_failed_session_cannot_be_resurrected(void)
{
    reset_fixture();
    conversation_client_t *client = create_client();
    ready_client(client);
    esp_websocket_event_data_t data = {.close_status_code = 1000};
    emit_event(WEBSOCKET_EVENT_CLOSED, &data);
    assert_terminal_failure(client);
    const conversation_client_status_t failed = status_of(client);
    emit_late_session_events();
    const conversation_client_status_t late = status_of(client);
    assert_terminal_failure(client);
    assert(late.last_error == failed.last_error);
    assert(strcmp(late.service_error_name, failed.service_error_name) == 0);
    assert(conversation_client_buffered_pcm_bytes(client) == 0U);
    destroy_client(client);
}

static void test_expected_finish_and_abort_are_not_failures(void)
{
    for (unsigned aborting = 0U; aborting <= 1U; ++aborting) {
        reset_fixture();
        conversation_client_t *client = create_client();
        ready_client(client);
        assert((aborting ? conversation_client_abort(client)
                         : conversation_client_finish(client)) == ESP_OK);
        conversation_client_status_t status = status_of(client);
        assert(status.phase == CONVERSATION_CLIENT_PHASE_STOPPED);
        assert(!status.connected && status.last_error == ESP_OK);
        emit_late_session_events();
        status = status_of(client);
        assert(status.phase == CONVERSATION_CLIENT_PHASE_STOPPED);
        assert(!status.connected && status.last_error == ESP_OK);
        assert(conversation_client_buffered_pcm_bytes(client) == 0U);
        destroy_client(client);
    }
}

static void test_duplicate_created_does_not_regress_ready_or_capture(void)
{
    reset_fixture();
    conversation_client_t *client = create_client();
    ready_client(client);
    emit_json("{\"type\":\"session.created\"}");
    assert(status_of(client).phase == CONVERSATION_CLIENT_PHASE_LISTENING);
    assert(conversation_client_send_speech(client) == ESP_OK);
    assert(status_of(client).phase == CONVERSATION_CLIENT_PHASE_CAPTURING);
    emit_json("{\"type\":\"session.created\"}");
    assert(status_of(client).phase == CONVERSATION_CLIENT_PHASE_CAPTURING);
    emit_json("{\"type\":\"session.updated\"}");
    assert(status_of(client).phase == CONVERSATION_CLIENT_PHASE_CAPTURING);
    destroy_client(client);
}

static void test_synchronous_send_callbacks_keep_newer_state(void)
{
    for (unsigned closed = 0U; closed <= 1U; ++closed) {
        reset_fixture();
        conversation_client_t *client = create_client();
        emit_event(WEBSOCKET_EVENT_CONNECTED, NULL);
        synchronous_send_event = closed ? SEND_EVENT_CLOSED
                                        : SEND_EVENT_SESSION_UPDATED;
        assert(conversation_client_send_start(client) == ESP_OK);
        const conversation_client_status_t status = status_of(client);
        assert(status.diagnostics.session_update_sent);
        if (closed) {
            assert_terminal_failure(client);
            assert(!status.diagnostics.session_ready);
        } else {
            assert(status.phase == CONVERSATION_CLIENT_PHASE_LISTENING);
            assert(status.turn_index == 1U);
            assert(status.diagnostics.session_ready);
        }
        destroy_client(client);
    }
}

static void test_close_reason_is_not_treated_as_text_or_error_message(void)
{
    reset_fixture();
    conversation_client_t *client = create_client();
    ready_client(client);
    /* This reason intentionally has no NUL terminator. It must not be parsed
     * as a server event, copied to status, or passed to a %s logger. */
    const char reason[] = {'c','l','o','s','e','-','r','e','a','s','o','n','-',
                           's','e','c','r','e','t'};
    esp_websocket_event_data_t data = {
        .close_status_code = 1008,
        .data_ptr = reason,
        .data_len = (int)sizeof(reason),
    };
    emit_event(WEBSOCKET_EVENT_CLOSED, &data);
    assert_terminal_failure(client);
    const conversation_client_status_t status = status_of(client);
    assert(strstr(status.service_error_name, "close-reason") == NULL);
    assert(status.response[0] == '\0' && status.transcript[0] == '\0');
    destroy_client(client);
}

static void test_partial_allocation_failures_clean_up(void)
{
    /* Initial client allocation has a libc fallback; inject each subsequent
     * buffer failure to exercise the production partial-destroy path. */
    for (unsigned failure = 2U; failure <= 5U; ++failure) {
        reset_fixture();
        fail_allocation = failure;
        conversation_config_snapshot_t snapshot = fixture_configuration();
        conversation_client_t *client = NULL;
        assert(conversation_client_create(&snapshot, &client) == ESP_ERR_NO_MEM);
        assert(client == NULL);
        conversation_config_clear_sensitive(&snapshot, sizeof(snapshot));
        assert(heap_live == 0U && mutex_live == 0U && event_live == 0U);
        assert(websocket == NULL);
    }
}

int main(void)
{
    test_early_close_and_finish_fail_immediately();
    test_protocol_session_finished_is_terminal();
    test_service_error_is_not_overwritten_by_transport_end();
    test_handshake_error_retains_http_status();
    test_failed_session_cannot_be_resurrected();
    test_expected_finish_and_abort_are_not_failures();
    test_duplicate_created_does_not_regress_ready_or_capture();
    test_synchronous_send_callbacks_keep_newer_state();
    test_close_reason_is_not_treated_as_text_or_error_message();
    test_partial_allocation_failures_clean_up();
    puts("conversation client event integration tests passed");
    return 0;
}
