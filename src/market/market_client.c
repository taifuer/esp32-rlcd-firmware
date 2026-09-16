#include "market_client.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

enum {
    MARKET_HTTP_OPERATION_TIMEOUT_MS = 4000,
    MARKET_HTTP_DEADLINE_MS = 16000,
    MARKET_HTTP_BUFFER_BYTES = 2048,
};

typedef struct {
    bool unsupported_encoding;
} response_headers_t;

typedef struct {
    int socket;
    esp_timer_handle_t timer;
    SemaphoreHandle_t completed;
    bool started;
    int shutdown_result;
    int shutdown_errno;
} deadline_guard_t;

static void expire_socket(void *argument)
{
    deadline_guard_t *guard = argument;
    /* Interrupt only the socket. The worker remains the sole owner of the
     * HTTP/TLS client and will not close or reuse this fd until we finish. */
    /* Receive-only shutdown wakes the read without queuing a TCP FIN or
     * entering TCP's close-retry wait on the shared timer task. The TCP/IP
     * mailbox must still make progress. Full close remains in the worker. */
    guard->shutdown_result = shutdown(guard->socket, SHUT_RD);
    guard->shutdown_errno = guard->shutdown_result == 0 ? 0 : errno;
    (void)xSemaphoreGive(guard->completed);
}

static esp_err_t start_deadline_guard(deadline_guard_t *guard,
                                      esp_http_client_handle_t client,
                                      int64_t deadline)
{
    guard->socket = esp_http_client_get_socket(client);
    if (guard->socket < 0) return ESP_ERR_INVALID_STATE;
    guard->completed = xSemaphoreCreateBinary();
    if (guard->completed == NULL) return ESP_ERR_NO_MEM;
    const esp_timer_create_args_t timer_config = {
        .callback = expire_socket,
        .arg = guard,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "market_deadline",
    };
    esp_err_t error = esp_timer_create(&timer_config, &guard->timer);
    if (error != ESP_OK) return error;
    const int64_t remaining = deadline - esp_timer_get_time();
    if (remaining <= 0) return ESP_ERR_TIMEOUT;
    error = esp_timer_start_once(guard->timer, (uint64_t)remaining);
    if (error == ESP_OK) guard->started = true;
    return error;
}

static bool finish_deadline_guard(deadline_guard_t *guard)
{
    bool expired = false;
    if (guard->started) {
        /* In the pinned ESP-IDF, a one-shot alarm is removed under the timer
         * list lock BEFORE its callback is dispatched. Successful stop means
         * no callback can run. Otherwise it may be about to run or still be
         * in shutdown(): drain its completion before closing/reusing the fd
         * or releasing this stack context. No timer callback touches TLS. */
        if (esp_timer_stop(guard->timer) != ESP_OK) {
            (void)xSemaphoreTake(guard->completed, portMAX_DELAY);
            expired = true;
            if (guard->shutdown_result != 0) {
                ESP_LOGW("market", "receive shutdown failed: errno=%d",
                         guard->shutdown_errno);
            }
        }
    }
    if (guard->timer != NULL) (void)esp_timer_delete(guard->timer);
    if (guard->completed != NULL) vSemaphoreDelete(guard->completed);
    return expired;
}

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    response_headers_t *headers = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key != NULL &&
        event->header_value != NULL &&
        strcasecmp(event->header_key, "Content-Encoding") == 0 &&
        event->header_value[0] != '\0' &&
        strcasecmp(event->header_value, "identity") != 0) {
        headers->unsupported_encoding = true;
    }
    return ESP_OK;
}

static esp_err_t request_allowed(market_cancel_callback_t cancelled,
                                 void *context, int64_t deadline)
{
    if (cancelled != NULL && cancelled(context)) return ESP_ERR_INVALID_STATE;
    return esp_timer_get_time() >= deadline ? ESP_ERR_TIMEOUT : ESP_OK;
}

esp_err_t market_client_fetch(const market_config_t *config,
                              market_row_t rows[MARKET_MAX_ROWS],
                              market_cancel_callback_t cancelled,
                              void *context)
{
    if (rows == NULL || !market_config_valid(config)) return ESP_ERR_INVALID_ARG;
    memset(rows, 0, sizeof(*rows) * MARKET_MAX_ROWS);
    for (size_t i = 0; i < config->count; ++i) rows[i].id = config->ids[i];
    char url[160] = "https://hq.sinajs.cn/list=";
    size_t used = strlen(url);
    for (size_t i = 0; i < config->count; ++i) {
        const int written = snprintf(url + used, sizeof(url) - used, "%s%s",
            i == 0 ? "" : ",", market_preset_by_id(config->ids[i])->symbol);
        if (written < 0 || (size_t)written >= sizeof(url) - used) return ESP_ERR_INVALID_SIZE;
        used += (size_t)written;
    }
    const int64_t deadline = esp_timer_get_time() +
                             (int64_t)MARKET_HTTP_DEADLINE_MS * 1000;
    esp_err_t error = request_allowed(cancelled, context, deadline);
    if (error != ESP_OK) return error;

    response_headers_t headers = {0};
    const esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = MARKET_HTTP_OPERATION_TIMEOUT_MS,
        .buffer_size = MARKET_HTTP_BUFFER_BYTES,
        .disable_auto_redirect = true,
        .event_handler = on_http_event,
        .user_data = &headers,
        .user_agent = "Mozilla/5.0 ESP32-RLCD-Firmware",
    };
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (client == NULL) return ESP_ERR_NO_MEM;
    deadline_guard_t guard = {0};
    char *body = NULL;
    error = esp_http_client_set_header(client, "Referer", "https://finance.sina.com.cn/");
    if (error == ESP_OK) error = esp_http_client_set_header(client, "Accept-Encoding", "identity");
    if (error == ESP_OK) error = esp_http_client_open(client, 0);
    if (error != ESP_OK) goto done;
    error = request_allowed(cancelled, context, deadline);
    if (error != ESP_OK) goto done;
    /* fetch_headers/read each contain internal transport loops. A socket
     * deadline is necessary even when every individual read makes progress. */
    error = start_deadline_guard(&guard, client, deadline);
    if (error != ESP_OK) goto done;
    const int64_t announced_length = esp_http_client_fetch_headers(client);
    if (announced_length < 0) {
        error = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    if (esp_http_client_get_status_code(client) != 200 || headers.unsupported_encoding) {
        error = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    if (announced_length > MARKET_RESPONSE_LIMIT) {
        error = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    body = malloc(MARKET_RESPONSE_LIMIT + 1);
    if (body == NULL) {
        error = ESP_ERR_NO_MEM;
        goto done;
    }
    size_t received = 0;
    while (true) {
        error = request_allowed(cancelled, context, deadline);
        if (error != ESP_OK) break;
        if (received == MARKET_RESPONSE_LIMIT) {
            char extra;
            const int count = esp_http_client_read(client, &extra, 1);
            error = count == 0 && esp_http_client_is_complete_data_received(client)
                        ? ESP_OK : ESP_ERR_INVALID_SIZE;
            break;
        }
        /* A one-byte application read lets us check cancellation between body
         * bytes; transport/TLS still buffer the network packets efficiently.
         * Chunk framing and header slow-drip are bounded by the socket guard. */
        const int count = esp_http_client_read(client, body + received, 1);
        if (count == -ESP_ERR_HTTP_EAGAIN) continue;
        if (count < 0) {
            error = ESP_FAIL;
            break;
        }
        if (count == 0) {
            if (!esp_http_client_is_complete_data_received(client)) error = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        received += (size_t)count;
    }
    if (error == ESP_OK) error = request_allowed(cancelled, context, deadline);
    if (error == ESP_OK && (!received ||
        (!esp_http_client_is_chunked_response(client) &&
         (uint64_t)announced_length != received))) error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK) {
        body[received] = '\0';
        if (market_parse_response(body, received, config, rows) == 0) error = ESP_ERR_INVALID_RESPONSE;
    }
done:
    if (finish_deadline_guard(&guard)) error = ESP_ERR_TIMEOUT;
    free(body);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    /* The deadline may have fired after parsing but before stop acquired the
     * timer-list lock. A failed transaction must not publish those rows. */
    if (error != ESP_OK) {
        memset(rows, 0, sizeof(*rows) * MARKET_MAX_ROWS);
        for (size_t i = 0; i < config->count; ++i) rows[i].id = config->ids[i];
    }
    return error;
}
