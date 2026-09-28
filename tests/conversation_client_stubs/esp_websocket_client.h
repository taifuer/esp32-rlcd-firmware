#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/* Only the public API surface used by the real conversation client is
 * represented. This fake neither opens a socket nor simulates TLS. */
typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
typedef struct test_websocket *esp_websocket_client_handle_t;
typedef enum {
    WEBSOCKET_EVENT_ANY = -1,
    WEBSOCKET_EVENT_ERROR = 0,
    WEBSOCKET_EVENT_CONNECTED,
    WEBSOCKET_EVENT_DISCONNECTED,
    WEBSOCKET_EVENT_DATA,
    WEBSOCKET_EVENT_CLOSED,
    WEBSOCKET_EVENT_BEFORE_CONNECT,
    WEBSOCKET_EVENT_BEGIN,
    WEBSOCKET_EVENT_FINISH,
    WEBSOCKET_EVENT_MAX,
} esp_websocket_event_id_t;
typedef enum {
    WEBSOCKET_ERROR_TYPE_NONE = 0,
    WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT,
    WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT,
    WEBSOCKET_ERROR_TYPE_HANDSHAKE,
    WEBSOCKET_ERROR_TYPE_SERVER_CLOSE,
} esp_websocket_error_type_t;
typedef struct {
    esp_err_t esp_tls_last_esp_err;
    int esp_tls_stack_err;
    int esp_tls_cert_verify_flags;
    esp_websocket_error_type_t error_type;
    int esp_ws_handshake_status_code;
    int esp_transport_sock_errno;
} esp_websocket_error_codes_t;
typedef struct {
    const char *data_ptr;
    int data_len;
    bool fin;
    uint8_t op_code;
    esp_websocket_client_handle_t client;
    void *user_context;
    int payload_len;
    int payload_offset;
    esp_websocket_error_codes_t error_handle;
    int close_status_code;
} esp_websocket_event_data_t;
typedef struct {
    const char *uri;
    bool disable_auto_reconnect;
    bool enable_close_reconnect;
    int task_prio;
    const char *task_name;
    int task_stack;
    int buffer_size;
    esp_err_t (*crt_bundle_attach)(void *);
    bool keep_alive_enable;
    int keep_alive_idle;
    int keep_alive_interval;
    int keep_alive_count;
    int network_timeout_ms;
    size_t ping_interval_sec;
} esp_websocket_client_config_t;

esp_websocket_client_handle_t esp_websocket_client_init(
    const esp_websocket_client_config_t *configuration);
esp_err_t esp_websocket_client_append_header(esp_websocket_client_handle_t client,
                                            const char *key, const char *value);
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client,
                                        esp_websocket_event_id_t event,
                                        esp_event_handler_t handler, void *arg);
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client);
bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client);
int esp_websocket_client_send_text(esp_websocket_client_handle_t client,
                                    const char *message, int length,
                                    TickType_t timeout);
esp_err_t esp_websocket_client_close(esp_websocket_client_handle_t client,
                                     TickType_t timeout);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client);
