#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Include the production worker to drive exactly one pass deterministically
 * without running an RTOS or touching network/storage. */
#include "../src/market/market_service.c"

static market_config_t fake_config;
static uint32_t fake_generation;
static int64_t fake_time;
static int begin_count, release_count, fetch_count, notifications;
static esp_err_t begin_error, fetch_error;
static int fetch_mode;
static void (*on_lock)(void);

const char *esp_err_to_name(esp_err_t error) { (void)error; return "fake"; }
int64_t esp_timer_get_time(void) { return fake_time; }
time_t time(time_t *result)
{
    const time_t now = (time_t)1789560000; /* 2026-09-16 12:00 UTC. */
    if (result != NULL) *result = now;
    return now;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout)
{
    (void)mutex; (void)timeout;
    if (on_lock != NULL) {
        void (*hook)(void) = on_lock;
        on_lock = NULL;
        hook();
    }
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) { (void)mutex; return pdTRUE; }
BaseType_t xTaskCreate(void (*task)(void *), const char *name,
                      unsigned stack, void *argument, unsigned priority,
                      TaskHandle_t *handle)
{
    (void)task; (void)name; (void)argument; (void)priority;
    assert(stack >= 8192);
    *handle = (void *)2;
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { assert(task != NULL); ++notifications; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout)
{ (void)clear; (void)timeout; return 0; }
esp_err_t market_config_init(void) { return ESP_OK; }
esp_err_t market_config_get_snapshot(market_config_t *config, uint32_t *generation)
{ *config = fake_config; *generation = fake_generation; return ESP_OK; }
esp_err_t network_time_begin_online_session(uint32_t timeout_ms)
{
    assert(timeout_ms == 10000);
    ++begin_count;
    if (fetch_mode == 1) assert(market_service_set_activity(false, true) == ESP_OK);
    return begin_error;
}
esp_err_t network_time_end_online_session(void) { ++release_count; return ESP_OK; }
esp_err_t market_client_fetch(const market_config_t *config,
                              market_row_t rows[MARKET_MAX_ROWS],
                              market_cancel_callback_t cancelled, void *context)
{
    ++fetch_count;
    if (cancelled(context)) return ESP_ERR_INVALID_STATE;
    if (fetch_mode == 2) {
        ++fake_generation;
        fake_config.ids[0] = 14;
        (void)market_service_notify_configuration_changed();
    }
    if (fetch_mode == 3) {
        assert(market_service_set_activity(true, false) == ESP_OK);
        assert(!cancelled(context)); /* Manual work survives power transition. */
    }
    if (fetch_mode == 4) {
        assert(market_service_set_activity(true, false) == ESP_OK);
        assert(cancelled(context)); /* Automatic background work does not. */
    }
    if (fetch_error != ESP_OK) return fetch_error;
    for (size_t i = 0; i < config->count; ++i) {
        rows[i] = (market_row_t){.id = config->ids[i], .valid = true,
                                .price = 101.25, .percent = 1.25};
        memcpy(rows[i].quote_time, "2026-09-16 15:35:32", 20);
    }
    if (fetch_mode == 5) rows[0].valid = false;
    if (fetch_mode == 6) memcpy(rows[0].quote_time, "2199-01-01 00:00:00", 20);
    return ESP_OK;
}

static void reset_service(bool enabled)
{
    s_mutex = NULL; s_task = NULL; s_initialized = false;
    s_requested = false; s_running = false;
    s_generation = 0; s_visibility_generation = 0;
    memset(&s_status, 0, sizeof(s_status));
    memset(s_cache, 0, sizeof(s_cache));
    s_attempted = false; s_last_attempt_us = 0;
    market_config_defaults(&fake_config);
    fake_config.enabled = enabled;
    fake_generation = 0; fake_time = 0;
    begin_count = 0; release_count = 0; fetch_count = 0; notifications = 0;
    begin_error = ESP_OK; fetch_error = ESP_OK; fetch_mode = 0; on_lock = NULL;
    assert(market_service_get_status(&s_status) == ESP_ERR_INVALID_STATE);
    assert(market_service_init() == ESP_OK);
}

static void publish_newer_config(void)
{
    ++fake_generation;
    fake_config.ids[0] = 14;
    assert(market_service_notify_configuration_changed() == ESP_OK);
}

int main(void)
{
    reset_service(false);
    assert(market_service_set_activity(true, true) == ESP_OK);
    refresh_if_due();
    assert(begin_count == 0 && s_status.state == MARKET_SERVICE_STATE_DISABLED);
    assert(market_service_request_refresh() == ESP_ERR_INVALID_STATE);

    reset_service(true);
    refresh_if_due(); /* Not visible: no startup request. */
    assert(begin_count == 0);
    assert(market_service_set_activity(true, true) == ESP_OK);
    const uint32_t revision = s_status.revision;
    const int wakeups = notifications;
    for (int i = 0; i < 100; ++i) assert(market_service_set_activity(true, true) == ESP_OK);
    assert(s_status.revision == revision && notifications == wakeups);
    refresh_if_due();
    assert(begin_count == 1 && release_count == 1 && fetch_count == 1);
    assert(s_status.state == MARKET_SERVICE_STATE_READY && s_status.rows[0].valid);
    fake_time = 119999000;
    refresh_if_due(); assert(begin_count == 1);
    (void)market_service_set_activity(false, true);
    (void)market_service_set_activity(true, true);
    refresh_if_due(); assert(begin_count == 1);
    fake_time = 120000000;
    refresh_if_due(); assert(begin_count == 2);
    fetch_error = ESP_FAIL;
    fake_time = 240000000;
    refresh_if_due();
    assert(s_status.rows[0].valid && s_status.rows[0].stale);
    assert(s_status.rows[0].price == 101.25);
    assert(strcmp(s_status.rows[0].quote_time, "2026-09-16 15:35:32") == 0);
    assert(s_status.state == MARKET_SERVICE_STATE_FAILED);
    fake_time += 1000000; refresh_if_due(); assert(begin_count == 3);

    reset_service(true);
    (void)market_service_set_activity(true, false);
    refresh_if_due(); assert(begin_count == 0);
    assert(market_service_request_refresh() == ESP_OK);
    assert(market_service_request_refresh() == ESP_ERR_INVALID_STATE);
    (void)market_service_set_activity(false, false);
    refresh_if_due();
    assert(begin_count == 0 && s_status.state == MARKET_SERVICE_STATE_NO_DATA);
    (void)market_service_set_activity(true, false);
    assert(market_service_request_refresh() == ESP_OK);
    fetch_mode = 3;
    refresh_if_due();
    assert(begin_count == 1 && release_count == 1 && s_status.rows[0].valid);

    reset_service(true);
    (void)market_service_set_activity(true, true);
    begin_error = ESP_ERR_INVALID_STATE;
    refresh_if_due();
    assert(begin_count == 1 && release_count == 0 && fetch_count == 0);
    assert(s_status.state == MARKET_SERVICE_STATE_FAILED);
    fake_time = 119999000; refresh_if_due(); assert(begin_count == 1);

    reset_service(true);
    (void)market_service_set_activity(true, true);
    fetch_mode = 1; refresh_if_due();
    assert(begin_count == 1 && release_count == 1);
    assert(!s_status.rows[0].valid && s_status.state == MARKET_SERVICE_STATE_NO_DATA);

    reset_service(true);
    (void)market_service_set_activity(true, true);
    fetch_mode = 2; refresh_if_due();
    assert(!s_status.rows[0].valid && s_status.rows[0].id == 14);
    assert(!s_cache[0].valid && release_count == 1);

    reset_service(true);
    (void)market_service_set_activity(true, true);
    fetch_mode = 4; refresh_if_due();
    assert(!s_status.rows[0].valid && release_count == 1);

    reset_service(true);
    (void)market_service_set_activity(true, true);
    refresh_if_due();
    fake_time = 120000000; fetch_mode = 5; refresh_if_due();
    assert(s_status.rows[0].stale && !s_status.rows[1].stale);
    assert(s_status.state == MARKET_SERVICE_STATE_FAILED);
    fake_time += 120000000; fetch_mode = 6; refresh_if_due();
    assert(s_status.rows[0].stale);
    assert(strcmp(s_status.rows[0].quote_time, "2026-09-16 15:35:32") == 0);
    fake_config.ids[0] = 14; ++fake_generation;
    (void)market_service_notify_configuration_changed();
    assert(!s_status.rows[0].valid && s_status.rows[1].valid && s_cache[0].valid);

    reset_service(true);
    on_lock = publish_newer_config;
    assert(update_configuration() == ESP_OK);
    assert(s_generation == 1 && s_status.config.ids[0] == 14);
    /* Wrap-forward is accepted; delayed pre-wrap snapshot is not. */
    s_generation = UINT32_MAX;
    fake_generation = 0;
    fake_config.ids[0] = 13;
    assert(update_configuration() == ESP_OK);
    assert(s_generation == 0 && s_status.config.ids[0] == 13);
    fake_generation = UINT32_MAX;
    fake_config.ids[0] = 12;
    assert(update_configuration() == ESP_OK);
    assert(s_generation == 0 && s_status.config.ids[0] == 13);
    puts("market service tests passed");
    return 0;
}
