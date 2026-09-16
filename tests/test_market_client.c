#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static void *client_malloc(size_t size);
static void client_free(void *allocation);
static int client_shutdown(int socket_fd, int direction);
/* Run the unmodified production client with deterministic HTTP/clock/heap
 * boundaries. Only its response-buffer allocation is fault-injected. */
#define malloc client_malloc
#define free client_free
#define shutdown client_shutdown
#include "../src/market/market_client.c"
#undef malloc
#undef free
#undef shutdown

struct esp_http_client { int unused; };
static struct esp_http_client handle;
static esp_http_client_config_t requested_http;
static char requested_url[160];
static char response[MARKET_RESPONSE_LIMIT + 2U];
static size_t response_length, response_offset;
static int64_t announced_length, fake_time, read_duration, header_duration;
static int init_count, header_count, open_count, read_count, close_count, cleanup_count;
static int allocation_count, release_count, status_code;
static int fail_header, interrupt_read, again_reads, cancel_after_reads;
static int interrupted_read_result;
static bool fail_init, fail_allocation, chunked, incomplete, force_complete;
static bool cancel_before_init, cancel_after_open, endless_again;
static esp_err_t open_error;
static const char *encoding;
static void *active_allocation;
static esp_timer_create_args_t requested_timer;
static bool timer_created, timer_armed, callback_pending, callback_finished;
static bool fail_timer_create, fail_timer_start, fail_semaphore, invalid_socket;
static bool socket_shutdown, semaphore_alive, stop_with_callback_pending;
static bool slow_headers, slow_body;
static unsigned semaphore_count, shutdown_count, drain_count;
static int64_t timer_deadline, timer_create_duration, open_duration;

static void fire_timer(void)
{
    assert(timer_created && semaphore_alive && !callback_finished);
    timer_armed = false;
    callback_pending = false;
    requested_timer.callback(requested_timer.arg);
    callback_finished = true;
}

static void advance_clock(int64_t elapsed)
{
    fake_time += elapsed;
    if (timer_armed && fake_time >= timer_deadline) fire_timer();
}

static const char QUOTE[] =
    "var hq_str_znb_NKY=\"\xc8\xd5\xbe\xad,101.25,1.25,1.25,ignored,1759126320,"
    "2026-09-16,14:30:01,0,0,0,0,0\";\n";

static void reset_http(void)
{
    assert(active_allocation == NULL);
    assert(!timer_created && !semaphore_alive && !timer_armed && !callback_pending);
    memset(&requested_http, 0, sizeof(requested_http));
    memset(requested_url, 0, sizeof(requested_url));
    memcpy(response, QUOTE, sizeof(QUOTE));
    response_length = strlen(QUOTE);
    response_offset = 0U;
    announced_length = (int64_t)response_length;
    fake_time = read_duration = header_duration = 0;
    init_count = header_count = open_count = read_count = close_count = cleanup_count = 0;
    allocation_count = release_count = 0;
    status_code = 200;
    fail_header = interrupt_read = again_reads = cancel_after_reads = 0;
    interrupted_read_result = -1;
    fail_init = fail_allocation = chunked = incomplete = force_complete = false;
    cancel_before_init = cancel_after_open = endless_again = false;
    open_error = ESP_OK;
    encoding = NULL;
    memset(&requested_timer, 0, sizeof(requested_timer));
    callback_finished = false;
    fail_timer_create = fail_timer_start = fail_semaphore = invalid_socket = false;
    socket_shutdown = stop_with_callback_pending = slow_headers = slow_body = false;
    semaphore_count = shutdown_count = drain_count = 0U;
    timer_deadline = timer_create_duration = open_duration = 0;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    assert(!semaphore_alive);
    if (fail_semaphore) return NULL;
    semaphore_alive = true;
    semaphore_count = 0U;
    return (void *)2;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    assert(semaphore == (void *)2 && semaphore_alive);
    assert(timeout == portMAX_DELAY);
    ++drain_count;
    if (callback_pending) fire_timer();
    assert(callback_finished && semaphore_count == 1U);
    semaphore_count = 0U;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(semaphore == (void *)2 && semaphore_alive);
    assert(socket_shutdown && semaphore_count == 0U);
    semaphore_count = 1U;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    assert(semaphore == (void *)2 && semaphore_alive);
    assert(!timer_armed && !callback_pending && !timer_created);
    semaphore_alive = false;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                           esp_timer_handle_t *out_handle)
{
    assert(args != NULL && args->callback != NULL && args->arg != NULL);
    assert(out_handle != NULL && !timer_created && semaphore_alive);
    if (fail_timer_create) return ESP_ERR_NO_MEM;
    requested_timer = *args;
    timer_created = true;
    *out_handle = (void *)3;
    advance_clock(timer_create_duration);
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    assert(timer == (void *)3 && timer_created && !timer_armed);
    assert(timeout_us > 0U && timeout_us <= 16000000U);
    if (fail_timer_start) return ESP_FAIL;
    timer_deadline = fake_time + (int64_t)timeout_us;
    timer_armed = true;
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    assert(timer == (void *)3 && timer_created);
    if (stop_with_callback_pending && timer_armed) {
        timer_armed = false;
        callback_pending = true;
        return ESP_ERR_INVALID_STATE;
    }
    if (!timer_armed) return ESP_ERR_INVALID_STATE;
    timer_armed = false;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    assert(timer == (void *)3 && timer_created);
    assert(!timer_armed && !callback_pending);
    assert(!callback_finished || (drain_count == 1U && semaphore_count == 0U));
    timer_created = false;
    return ESP_OK;
}

static int client_shutdown(int socket_fd, int direction)
{
    assert(socket_fd == 42 && direction == SHUT_RD);
    assert(close_count == 0 && cleanup_count == 0 && semaphore_alive);
    socket_shutdown = true;
    ++shutdown_count;
    return 0;
}

static void *client_malloc(size_t size)
{
    assert(size == MARKET_RESPONSE_LIMIT + 1U && active_allocation == NULL);
    ++allocation_count;
    if (fail_allocation) return NULL;
    active_allocation = malloc(size);
    assert(active_allocation != NULL);
    return active_allocation;
}

static void client_free(void *allocation)
{
    if (allocation == NULL) return;
    assert(allocation == active_allocation);
    ++release_count;
    free(allocation);
    active_allocation = NULL;
}

int64_t esp_timer_get_time(void) { return fake_time; }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    ++init_count;
    assert(config != NULL && config->url != NULL);
    assert(strlen(config->url) < sizeof(requested_url));
    strcpy(requested_url, config->url);
    requested_http = *config;
    assert(config->crt_bundle_attach == esp_crt_bundle_attach);
    assert(config->disable_auto_redirect);
    assert(config->timeout_ms > 0 && config->timeout_ms <= 4000);
    assert(config->buffer_size > 0 && config->buffer_size <= 2048);
    assert(config->user_agent != NULL && config->user_agent[0] != '\0');
    return fail_init ? NULL : &handle;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                    const char *name, const char *value)
{
    assert(client == &handle);
    ++header_count;
    if (header_count == 1) {
        assert(strcmp(name, "Referer") == 0);
        assert(strcmp(value, "https://finance.sina.com.cn/") == 0);
    } else {
        assert(header_count == 2 && strcmp(name, "Accept-Encoding") == 0);
        assert(strcmp(value, "identity") == 0);
    }
    return fail_header == header_count ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_length)
{
    assert(client == &handle && write_length == 0 && header_count == 2);
    ++open_count;
    advance_clock(open_duration);
    return open_error;
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client)
{
    assert(client == &handle && open_count == 1);
    advance_clock(header_duration);
    if (slow_headers) {
        /* One SDK call continuously receives small header fragments. Outer
         * request checks cannot run; only the actual watchdog callback can
         * interrupt it. No real socket or host timing assumption is used. */
        for (unsigned step = 0U; step < 60U && !socket_shutdown; ++step)
            advance_clock(1000000);
        assert(socket_shutdown && shutdown_count == 1U);
        return -1;
    }
    if (encoding != NULL) {
        esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_HEADER,
            .user_data = requested_http.user_data,
            .header_key = "cOnTeNt-EnCoDiNg", .header_value = encoding};
        assert(requested_http.event_handler(&event) == ESP_OK);
    }
    return announced_length;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{ assert(client == &handle); return status_code; }

int esp_http_client_get_socket(esp_http_client_handle_t client)
{ assert(client == &handle); return invalid_socket ? -1 : 42; }

int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int capacity)
{
    assert(client == &handle && buffer != NULL && capacity > 0);
    assert(capacity == 1); /* Return to cancellation checks per decoded byte. */
    ++read_count;
    advance_clock(read_duration);
    if (slow_body) {
        /* Chunk framing can still make a one-byte decoded read loop inside
         * IDF. The watchdog must interrupt that single call as well. */
        for (unsigned step = 0U; step < 60U && !socket_shutdown; ++step)
            advance_clock(1000000);
        assert(socket_shutdown && shutdown_count == 1U);
        return -1;
    }
    if (endless_again || read_count <= again_reads) return -ESP_ERR_HTTP_EAGAIN;
    if (interrupt_read == read_count) return interrupted_read_result;
    size_t count = response_length - response_offset;
    if (count > (size_t)capacity) count = (size_t)capacity;
    if (count > 37U) count = 37U;
    memcpy(buffer, response + response_offset, count);
    response_offset += count;
    return (int)count;
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client)
{
    assert(client == &handle);
    return force_complete || (!incomplete && response_offset == response_length);
}

bool esp_http_client_is_chunked_response(esp_http_client_handle_t client)
{ assert(client == &handle); return chunked; }
esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{
    assert(client == &handle && !timer_created && !timer_armed && !callback_pending);
    assert(!semaphore_alive);
    ++close_count;
    return ESP_OK;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{ assert(client == &handle); ++cleanup_count; return ESP_OK; }

static bool cancelled(void *context)
{
    assert(context == &handle);
    return cancel_before_init || (cancel_after_open && open_count > 0) ||
        (cancel_after_reads > 0 && read_count >= cancel_after_reads);
}

static esp_err_t fetch(market_row_t rows[MARKET_MAX_ROWS])
{
    const market_config_t config = {.enabled = true, .count = 5U,
                                    .ids = {13U, 0U, 7U, 9U, 14U}};
    const esp_err_t result = market_client_fetch(&config, rows, cancelled, &handle);
    assert(active_allocation == NULL);
    assert(close_count == (init_count != 0 && !fail_init ? 1 : 0));
    assert(cleanup_count == close_count);
    assert(release_count == (allocation_count != 0 && !fail_allocation ? 1 : 0));
    return result;
}

static void assert_rejected(esp_err_t expected)
{
    market_row_t rows[MARKET_MAX_ROWS];
    assert(fetch(rows) == expected);
    for (size_t i = 0U; i < MARKET_MAX_ROWS; ++i) assert(!rows[i].valid);
}

static void test_request_and_success(void)
{
    market_row_t rows[MARKET_MAX_ROWS];
    reset_http();
    assert(fetch(rows) == ESP_OK);
    assert(strcmp(requested_url,
        "https://hq.sinajs.cn/list=znb_NKY,sh000001,hkHSI,gb_ixic,znb_TWJQ") == 0);
    assert(rows[0].valid && rows[0].id == 13U && rows[0].price == 101.25);
    assert(strcmp(rows[0].quote_time, "2026-09-16 14:30:01") == 0);
    /* Missing lines remain invalid; the service handles a partial snapshot. */
    for (size_t i = 1U; i < MARKET_MAX_ROWS; ++i) assert(!rows[i].valid);
    assert(rows[1].id == 0U && rows[4].id == 14U);
    reset_http(); chunked = true; announced_length = 0;
    assert(fetch(rows) == ESP_OK && rows[0].valid);
    reset_http(); encoding = "IDENTITY";
    assert(fetch(rows) == ESP_OK);
    reset_http(); again_reads = 2;
    assert(fetch(rows) == ESP_OK && read_count > 2);
    reset_http(); open_duration = 3000000;
    assert(fetch(rows) == ESP_OK && timer_deadline == 16000000);
}

static void test_response_failures(void)
{
    reset_http(); status_code = 302; assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); status_code = 429; assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); encoding = "gzip"; assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); encoding = "br"; assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); announced_length = -ESP_ERR_HTTP_EAGAIN;
    assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); announced_length = MARKET_RESPONSE_LIMIT + 1U;
    assert_rejected(ESP_ERR_INVALID_SIZE); assert(allocation_count == 0);
    reset_http(); response_length = 0U; announced_length = 0;
    assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); response_length -= 4U; incomplete = true;
    assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); announced_length += 7; force_complete = true;
    assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); announced_length -= 7; force_complete = true;
    assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); chunked = true; announced_length = 0; incomplete = true;
    assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); interrupt_read = 2; assert_rejected(ESP_FAIL);
    reset_http(); interrupt_read = 2; interrupted_read_result = 0;
    assert_rejected(ESP_ERR_INVALID_RESPONSE);
    reset_http(); strcpy(response, "<html>upstream error</html>");
    response_length = strlen(response); announced_length = (int64_t)response_length;
    assert_rejected(ESP_ERR_INVALID_RESPONSE);
}

static void test_bounds_deadline_cancellation(void)
{
    market_row_t rows[MARKET_MAX_ROWS];
    reset_http();
    memset(response + response_length, ' ', MARKET_RESPONSE_LIMIT - response_length);
    response_length = MARKET_RESPONSE_LIMIT; announced_length = MARKET_RESPONSE_LIMIT;
    assert(fetch(rows) == ESP_OK && rows[0].valid);
    reset_http(); chunked = true; announced_length = 0;
    memset(response + response_length, ' ', MARKET_RESPONSE_LIMIT + 1U - response_length);
    response_length = MARKET_RESPONSE_LIMIT + 1U;
    assert_rejected(ESP_ERR_INVALID_SIZE);
    reset_http(); endless_again = true; read_duration = 4000000;
    assert_rejected(ESP_ERR_TIMEOUT); assert(read_count == 4);
    reset_http(); header_duration = 16000000;
    assert_rejected(ESP_ERR_TIMEOUT); assert(read_count == 0);
    reset_http(); cancel_before_init = true;
    assert_rejected(ESP_ERR_INVALID_STATE); assert(init_count == 0);
    reset_http(); cancel_after_open = true;
    assert_rejected(ESP_ERR_INVALID_STATE); assert(read_count == 0);
    reset_http(); cancel_after_reads = 1;
    assert_rejected(ESP_ERR_INVALID_STATE); assert(read_count == 1);
    reset_http(); slow_headers = true;
    assert_rejected(ESP_ERR_TIMEOUT);
    assert(shutdown_count == 1U && drain_count == 1U && fake_time == 16000000);
    reset_http(); slow_body = true;
    assert_rejected(ESP_ERR_TIMEOUT);
    assert(shutdown_count == 1U && drain_count == 1U && read_count == 1);
    reset_http(); stop_with_callback_pending = true;
    /* The timer left its queue but has not executed when cleanup calls stop.
     * Waiting for its completion must happen before freeing/closing resources. */
    assert(fetch(rows) == ESP_ERR_TIMEOUT);
    assert(shutdown_count == 1U && drain_count == 1U && !callback_pending);
    for (size_t i = 0U; i < MARKET_MAX_ROWS; ++i) assert(!rows[i].valid);
    assert(rows[0].id == 13U && rows[1].id == 0U && rows[4].id == 14U);
    reset_http(); timer_create_duration = 16000000;
    assert_rejected(ESP_ERR_TIMEOUT); assert(read_count == 0);
}

static void test_resource_failures(void)
{
    reset_http(); fail_init = true; assert_rejected(ESP_ERR_NO_MEM);
    reset_http(); fail_allocation = true; assert_rejected(ESP_ERR_NO_MEM);
    reset_http(); fail_semaphore = true; assert_rejected(ESP_ERR_NO_MEM);
    reset_http(); fail_timer_create = true; assert_rejected(ESP_ERR_NO_MEM);
    reset_http(); fail_timer_start = true; assert_rejected(ESP_FAIL);
    reset_http(); invalid_socket = true; assert_rejected(ESP_ERR_INVALID_STATE);
    reset_http(); fail_header = 1; assert_rejected(ESP_FAIL); assert(open_count == 0);
    reset_http(); fail_header = 2; assert_rejected(ESP_FAIL); assert(open_count == 0);
    reset_http(); open_error = ESP_ERR_TIMEOUT; assert_rejected(ESP_ERR_TIMEOUT);
    reset_http();
    market_row_t rows[MARKET_MAX_ROWS];
    market_config_t invalid = {.count = 6};
    assert(market_client_fetch(&invalid, rows, cancelled, &handle) == ESP_ERR_INVALID_ARG);
    market_config_defaults(&invalid);
    assert(market_client_fetch(&invalid, NULL, cancelled, &handle) == ESP_ERR_INVALID_ARG);
    assert(init_count == 0);
}

int main(void)
{
    test_request_and_success();
    test_response_failures();
    test_bounds_deadline_cancellation();
    test_resource_failures();
    puts("market client: fixed HTTPS, response integrity, internal slow-drip watchdog, cancel and callback-safe cleanup passed");
    return 0;
}
