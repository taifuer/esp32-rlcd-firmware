#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct esp_http_client *esp_http_client_handle_t;
enum { HTTP_EVENT_ON_HEADER = 3 };
typedef struct {
    int event_id;
    void *user_data;
    const char *header_key;
    const char *header_value;
} esp_http_client_event_t;
typedef struct {
    const char *url;
    esp_err_t (*crt_bundle_attach)(void *config);
    int timeout_ms;
    int buffer_size;
    bool disable_auto_redirect;
    esp_err_t (*event_handler)(esp_http_client_event_t *event);
    void *user_data;
    const char *user_agent;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                    const char *name, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_length);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int esp_http_client_get_socket(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int capacity);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client);
bool esp_http_client_is_chunked_response(esp_http_client_handle_t client);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
