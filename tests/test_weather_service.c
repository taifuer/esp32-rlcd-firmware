#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Exercise the real service worker one event pass at a time. Only ESP-IDF,
 * network transport and NVS are replaced; configuration, location selection,
 * cache validation and freshness use the production implementations. */
#include "../src/weather/weather_service.c"

static weather_config_snapshot_t fake_config;
static EventBits_t fake_events;
static TickType_t fake_ticks;
static int64_t fake_epoch;
static enum { OWNER_NONE, OWNER_MAINTENANCE, OWNER_ONLINE } owner;
static bool portal_running;
static bool connection_failure_after_transfer;
static unsigned begin_calls, handoff_calls, end_calls, maintenance_releases;
static unsigned current_calls, daily_calls, cache_saves;
static uint32_t fetched_city;
static esp_err_t begin_error, current_error, cache_error, config_error;
static struct { bool held; } semaphores[2];
static unsigned semaphore_count;
static void (*operation_hook)(void);

const char *esp_err_to_name(esp_err_t error) { (void)error; return "test"; }
time_t time(time_t *result)
{
    if (result != NULL) *result = (time_t)fake_epoch;
    return (time_t)fake_epoch;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    assert(semaphore_count < 2U);
    return &semaphores[semaphore_count++];
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    (void)timeout;
    if (semaphore == s_operation_mutex && operation_hook != NULL) {
        void (*hook)(void) = operation_hook;
        operation_hook = NULL;
        hook();
    }
    for (unsigned i = 0U; i < semaphore_count; ++i) {
        if (semaphore == &semaphores[i]) {
            assert(!semaphores[i].held);
            semaphores[i].held = true;
            return pdTRUE;
        }
    }
    assert(false);
    return pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    for (unsigned i = 0U; i < semaphore_count; ++i) {
        if (semaphore == &semaphores[i]) {
            assert(semaphores[i].held);
            semaphores[i].held = false;
            return pdTRUE;
        }
    }
    assert(false);
    return pdFALSE;
}
EventGroupHandle_t xEventGroupCreate(void) { return &fake_events; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t events, EventBits_t bits)
{ assert(events == &fake_events); fake_events |= bits; return fake_events; }
EventBits_t xEventGroupGetBits(EventGroupHandle_t events)
{ assert(events == &fake_events); return fake_events; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t events, EventBits_t bits,
                              BaseType_t clear, BaseType_t all, TickType_t timeout)
{
    (void)all; (void)timeout;
    assert(events == &fake_events);
    const EventBits_t result = fake_events & bits;
    if (clear) fake_events &= ~bits;
    return result;
}
BaseType_t xTaskCreate(void (*task)(void *), const char *name,
                      unsigned stack, void *argument, unsigned priority,
                      TaskHandle_t *handle)
{
    assert(task == weather_task && stack == 16384U);
    (void)name; (void)argument; (void)priority; (void)handle;
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) { fake_ticks += ticks; }
TickType_t xTaskGetTickCount(void) { return fake_ticks; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle) { (void)handle; return 8192U; }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 100000U; }
size_t heap_caps_get_minimum_free_size(unsigned caps) { (void)caps; return 50000U; }
esp_err_t weather_http_json_self_test(void) { return ESP_OK; }
esp_err_t weather_config_init(void) { return ESP_OK; }
esp_err_t weather_config_get_snapshot(weather_config_snapshot_t *snapshot)
{ if (config_error == ESP_OK) *snapshot = fake_config; return config_error; }
esp_err_t weather_cache_init(void) { return ESP_OK; }
esp_err_t weather_cache_get(weather_cache_data_t *cache)
{ (void)cache; return ESP_ERR_NOT_FOUND; }
esp_err_t weather_cache_save(const weather_cache_data_t *cache)
{ assert(weather_cache_data_is_valid(cache)); ++cache_saves; return cache_error; }
esp_err_t weather_cache_clear(void) { return cache_error; }
esp_err_t network_time_begin_online_session(uint32_t timeout_ms)
{
    assert(timeout_ms == WEATHER_NETWORK_TIMEOUT_MS && !portal_running);
    ++begin_calls;
    if (owner != OWNER_NONE) return ESP_ERR_INVALID_STATE;
    if (begin_error == ESP_OK) owner = OWNER_ONLINE;
    return begin_error;
}
esp_err_t network_time_begin_online_session_from_maintenance(uint32_t timeout_ms)
{
    assert(timeout_ms == WEATHER_NETWORK_TIMEOUT_MS && !portal_running);
    assert(owner == OWNER_MAINTENANCE);
    ++handoff_calls;
    if (begin_error == ESP_OK) owner = OWNER_ONLINE;
    else if (connection_failure_after_transfer) {
        /* Real connect_acquired_online_session releases ONLINE itself after
         * a failed association. end_maintenance must then be a no-op. */
        owner = OWNER_NONE;
    }
    return begin_error;
}
esp_err_t network_time_end_online_session(void)
{ assert(owner == OWNER_ONLINE); owner = OWNER_NONE; ++end_calls; return ESP_OK; }
void network_time_end_maintenance(void)
{ if (owner == OWNER_MAINTENANCE) { owner = OWNER_NONE; ++maintenance_releases; } }
esp_err_t weather_client_resolve_location(const weather_config_t *config,
                                         weather_client_location_t *location)
{
    assert(owner == OWNER_ONLINE && !portal_running);
    fetched_city = config->city_id;
    *location = (weather_client_location_t){.latitude_ten_thousandths = 312304,
        .longitude_ten_thousandths = 1214737, .name = "上海市"};
    return ESP_OK;
}
esp_err_t weather_client_fetch_current(const weather_config_t *config,
                                       const weather_client_location_t *location,
                                       weather_current_t *current)
{
    (void)location;
    assert(owner == OWNER_ONLINE && !portal_running);
    fetched_city = config->city_id;
    ++current_calls;
    *current = (weather_current_t){.temperature_tenths_celsius = 245,
        .feels_like_tenths_celsius = 245, .condition_code = 100,
        .condition_text = "晴"};
    return current_error;
}
esp_err_t weather_client_fetch_daily(const weather_config_t *config,
                                     const weather_client_location_t *location,
                                     weather_daily_t *daily)
{
    (void)config; (void)location;
    assert(owner == OWNER_ONLINE && !portal_running);
    ++daily_calls;
    *daily = (weather_daily_t){.day_count = 1U, .days = {{
        .date = "2026-09-27", .temperature_high_tenths_celsius = 260,
        .temperature_low_tenths_celsius = 170, .daytime_condition_code = 100,
        .daytime_condition_text = "晴", .precipitation_probability_percent = 0U}}};
    return ESP_OK;
}

static void process_pending(void)
{
    const EventBits_t bits = fake_events;
    fake_events = 0U;
    process_weather_events(bits);
    assert(!semaphores[0].held && !semaphores[1].held);
}

static void reset_service(bool automatic)
{
    s_mutex = NULL; s_operation_mutex = NULL; s_events = NULL;
    s_initialized = false; s_portal_active = false; s_maintenance_refresh_pending = false;
    s_status = (weather_service_status_t){0}; s_cache = (weather_cache_data_t){0};
    s_cache_available = false; s_retry_pending = false; s_authentication_blocked = false;
    s_retry_started_tick = 0U; s_retry_duration_ticks = 0U; s_authentication_generation = 0U;
    memset(semaphores, 0, sizeof(semaphores)); semaphore_count = 0U;
    fake_events = 0U; fake_ticks = 0U; fake_epoch = 1790467200;
    owner = OWNER_NONE; portal_running = false; operation_hook = NULL;
    connection_failure_after_transfer = false;
    begin_calls = handoff_calls = end_calls = maintenance_releases = 0U;
    current_calls = daily_calls = cache_saves = fetched_city = 0U;
    begin_error = current_error = cache_error = config_error = ESP_OK;
    fake_config = (weather_config_snapshot_t){0};
    weather_config_defaults(&fake_config.config);
    fake_config.config.enabled = true;
    strcpy(fake_config.config.api_host, "test.qweatherapi.com");
    strcpy(fake_config.config.api_key, "test-key-not-a-secret");
    fake_config.config.province_id = 310000U;
    fake_config.config.city_id = 310000U;
    fake_config.generation = 1U;
    assert(weather_service_set_portal_active(true) == ESP_ERR_INVALID_STATE);
    assert(weather_service_init(automatic) == ESP_OK);
}

static void start_portal(void)
{
    assert(weather_service_set_portal_active(true) == ESP_OK);
    assert(owner == OWNER_NONE);
    owner = OWNER_MAINTENANCE;
    portal_running = true;
}

static void handoff_closed_portal(void)
{
    portal_running = false; /* HTTP handlers joined and SoftAP stopped. */
    assert(weather_service_request_refresh_from_maintenance() == ESP_OK);
    assert(weather_service_set_portal_active(false) == ESP_OK);
}

static void enter_portal_between_check_and_operation(void) { start_portal(); }
static void handoff_between_check_and_operation(void)
{
    owner = OWNER_MAINTENANCE;
    assert(weather_service_request_refresh_from_maintenance() == ESP_OK);
}

int main(void)
{
    reset_service(true);
    start_portal();
    for (unsigned i = 0U; i < 10U; ++i) {
        ++fake_config.generation;
        assert(weather_service_notify_configuration_changed() == ESP_OK);
        process_pending();
        fake_ticks += 30000U;
        assert(begin_calls == 0U && handoff_calls == 0U);
        assert(s_status.config_enabled && s_status.state == WEATHER_SERVICE_STATE_NO_DATA);
    }
    assert(weather_service_request_refresh() == ESP_ERR_INVALID_STATE);
    /* The latest save, not the first save, supplies the request configuration. */
    fake_config.config.province_id = 110000U;
    fake_config.config.city_id = 110000U;
    handoff_closed_portal();
    assert(!s_status.interactive_refresh);
    assert(weather_service_set_portal_active(true) == ESP_ERR_NOT_FINISHED);
    assert(weather_service_request_refresh_from_maintenance() == ESP_ERR_INVALID_STATE);
    process_weather_events(WEATHER_EVENT_CHECK); /* A stale check cannot win. */
    assert(handoff_calls == 1U && begin_calls == 0U && end_calls == 1U);
    assert(fetched_city == 110000U && current_calls == 1U && daily_calls == 1U);
    assert(s_status.state == WEATHER_SERVICE_STATE_READY && !s_status.interactive_refresh);
    process_pending(); /* Delayed maintenance event is not a second handoff. */
    assert(handoff_calls == 1U && begin_calls == 0U && owner == OWNER_NONE);

    reset_service(false); /* SAVING still permits the explicitly requested save. */
    start_portal();
    process_pending();
    handoff_closed_portal();
    process_pending();
    assert(handoff_calls == 1U && begin_calls == 0U && end_calls == 1U);
    assert(!s_status.interactive_refresh);
    fake_epoch += 36000;
    process_pending();
    assert(handoff_calls == 1U && begin_calls == 0U);

    reset_service(true);
    start_portal();
    fake_config.config.enabled = false;
    assert(weather_service_notify_configuration_changed() == ESP_OK);
    process_pending();
    assert(s_status.state == WEATHER_SERVICE_STATE_DISABLED);
    portal_running = false;
    assert(weather_service_request_refresh_from_maintenance() == ESP_ERR_INVALID_STATE);
    assert(owner == OWNER_MAINTENANCE); /* Caller still owns rejected handoff. */
    network_time_end_maintenance();
    (void)weather_service_set_portal_active(false);
    process_pending();
    assert(begin_calls == 0U && handoff_calls == 0U);
    assert(weather_service_clear_cache() == ESP_OK);
    assert(!s_status.data_available && !s_status.interactive_refresh);

    reset_service(false);
    start_portal();
    handoff_closed_portal();
    fake_config.config.enabled = false; /* Late disable cannot acquire radio. */
    process_pending();
    assert(handoff_calls == 0U && maintenance_releases == 1U && owner == OWNER_NONE);
    assert(s_status.state != WEATHER_SERVICE_STATE_REFRESHING);

    reset_service(false);
    start_portal();
    handoff_closed_portal();
    begin_error = ESP_ERR_TIMEOUT;
    process_pending();
    assert(handoff_calls == 1U && maintenance_releases == 1U && owner == OWNER_NONE);
    assert(s_status.state == WEATHER_SERVICE_STATE_FAILED && !s_status.interactive_refresh);
    assert(current_calls == 0U && daily_calls == 0U);

    reset_service(false);
    start_portal();
    handoff_closed_portal();
    begin_error = ESP_ERR_TIMEOUT;
    connection_failure_after_transfer = true;
    process_pending();
    assert(handoff_calls == 1U && maintenance_releases == 0U && owner == OWNER_NONE);
    assert(end_calls == 0U && !s_status.interactive_refresh);

    reset_service(false);
    start_portal();
    handoff_closed_portal();
    config_error = ESP_FAIL; /* NVS read fails after admission, before worker. */
    process_pending();
    assert(handoff_calls == 0U && maintenance_releases == 1U && owner == OWNER_NONE);
    assert(s_status.state == WEATHER_SERVICE_STATE_FAILED && !s_status.interactive_refresh);
    assert(s_status.failure_stage == WEATHER_FAILURE_STAGE_LOCATION);
    assert(!s_maintenance_refresh_pending);

    reset_service(false);
    start_portal();
    handoff_closed_portal();
    fake_config.config.city_id = 999999U;
    process_pending();
    assert(handoff_calls == 0U && maintenance_releases == 1U && owner == OWNER_NONE);
    assert(s_status.state == WEATHER_SERVICE_STATE_FAILED && !s_status.interactive_refresh);

    reset_service(false);
    start_portal();
    handoff_closed_portal();
    current_error = ESP_ERR_TIMEOUT;
    process_pending();
    assert(handoff_calls == 1U && end_calls == 1U && owner == OWNER_NONE);
    assert(s_status.state == WEATHER_SERVICE_STATE_FAILED && !s_status.interactive_refresh);
    assert(s_status.failure_stage == WEATHER_FAILURE_STAGE_CURRENT);

    reset_service(false);
    start_portal();
    handoff_closed_portal();
    cache_error = ESP_FAIL;
    process_pending();
    assert(handoff_calls == 1U && end_calls == 1U && owner == OWNER_NONE);
    assert(cache_saves == 1U && s_status.state == WEATHER_SERVICE_STATE_FAILED);
    assert(s_status.failure_stage == WEATHER_FAILURE_STAGE_CACHE && !s_status.interactive_refresh);

    reset_service(false);
    start_portal();
    handoff_closed_portal();
    process_pending();
    const int64_t retained_time = s_status.snapshot.fetched_at_epoch_seconds;
    const int16_t retained_temperature = s_status.snapshot.current.temperature_tenths_celsius;
    fake_epoch += 60;
    start_portal();
    handoff_closed_portal();
    current_error = ESP_ERR_TIMEOUT;
    process_pending();
    assert(handoff_calls == 2U && end_calls == 2U && owner == OWNER_NONE);
    assert(s_status.data_available && !s_status.interactive_refresh);
    assert(s_status.snapshot.fetched_at_epoch_seconds == retained_time);
    assert(s_status.snapshot.current.temperature_tenths_celsius == retained_temperature);
    assert(s_status.state == WEATHER_SERVICE_STATE_FAILED);

    reset_service(true);
    operation_hook = enter_portal_between_check_and_operation;
    process_pending();
    assert(begin_calls == 0U && owner == OWNER_MAINTENANCE && portal_running);

    reset_service(true);
    operation_hook = handoff_between_check_and_operation;
    process_pending();
    assert(begin_calls == 0U && handoff_calls == 0U && owner == OWNER_MAINTENANCE);
    process_pending();
    assert(begin_calls == 0U && handoff_calls == 1U && owner == OWNER_NONE);

    reset_service(false);
    assert(weather_service_request_refresh() == ESP_OK);
    assert(weather_service_set_portal_active(true) == ESP_ERR_NOT_FINISHED);
    process_pending();
    assert(begin_calls == 1U && end_calls == 1U && s_status.interactive_refresh);
    (void)weather_service_acknowledge_interactive_refresh();
    assert(!s_status.interactive_refresh);

    /* Ordinary close after another terminal action cancelled the pending
     * save must only restore the prior power policy, not force a fetch. */
    reset_service(false);
    start_portal();
    portal_running = false;
    network_time_end_maintenance();
    (void)weather_service_set_portal_active(false);
    process_pending();
    assert(begin_calls == 0U && handoff_calls == 0U && owner == OWNER_NONE);

    puts("Weather service: portal gate, latest configuration, one-shot background handoff, priority/races, SAVING, failure cleanup and manual refresh passed.");
    return 0;
}
