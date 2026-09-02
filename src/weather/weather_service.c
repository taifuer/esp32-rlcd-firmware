#include "weather_service.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_time.h"
#include "weather_cache.h"
#include "weather_client.h"
#include "weather_config.h"
#include "weather_location_catalog.h"
#include "weather_request.h"

enum {
    WEATHER_EVENT_CHECK = BIT0,
    WEATHER_EVENT_FORCE_REFRESH = BIT1,
    WEATHER_EVENT_MAINTENANCE_REFRESH = BIT2,
    WEATHER_EVENT_ALL = WEATHER_EVENT_CHECK | WEATHER_EVENT_FORCE_REFRESH |
                        WEATHER_EVENT_MAINTENANCE_REFRESH,
    WEATHER_TASK_STACK_BYTES = 16384,
    WEATHER_TASK_PRIORITY = 4,
    WEATHER_NETWORK_TIMEOUT_MS = 20000,
    WEATHER_CHECK_INTERVAL_MS = 30000,
    WEATHER_CURRENT_REFRESH_SECONDS = 30 * 60,
    WEATHER_DAILY_REFRESH_SECONDS = 6 * 60 * 60,
    WEATHER_RETRY_SECONDS = 15 * 60,
    WEATHER_BUSY_RETRY_SECONDS = 60,
    WEATHER_VALID_EPOCH_MINIMUM = 1577836800,
};

static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_operation_mutex;
static EventGroupHandle_t s_events;
static bool s_initialized;
static weather_service_status_t s_status = {
    .state = WEATHER_SERVICE_STATE_DISABLED,
    .last_error = ESP_OK,
};
static weather_cache_data_t s_cache;
static bool s_cache_available;
static bool s_retry_pending;
static TickType_t s_retry_started_tick;
static TickType_t s_retry_duration_ticks;
static bool s_authentication_blocked;
static uint32_t s_authentication_generation;

static bool usable_epoch(int64_t epoch)
{
    return epoch >= WEATHER_VALID_EPOCH_MINIMUM;
}

static void increment_revision(void)
{
    ++s_status.revision;
    if (s_status.revision == 0U) {
        ++s_status.revision;
    }
}

static void set_state(weather_service_state_t state,
                      weather_failure_stage_t failure_stage,
                      esp_err_t error)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_status.state != state ||
        s_status.failure_stage != failure_stage ||
        s_status.last_error != error) {
        s_status.state = state;
        s_status.failure_stage = failure_stage;
        s_status.last_error = error;
        increment_revision();
    }
    xSemaphoreGive(s_mutex);
}

static void publish_cache(const weather_cache_data_t *cache,
                          weather_service_state_t state,
                          weather_failure_stage_t failure_stage,
                          esp_err_t error)
{
    if (cache == NULL ||
        xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_cache = *cache;
    s_cache_available = true;
    s_status.data_available = true;
    memcpy(s_status.location_name, cache->location_name,
           sizeof(s_status.location_name));
    s_status.snapshot = cache->snapshot;
    s_status.daily_fetched_at_epoch_seconds =
        cache->daily_fetched_at_epoch_seconds;
    s_status.state = state;
    s_status.failure_stage = failure_stage;
    s_status.last_error = error;
    increment_revision();
    xSemaphoreGive(s_mutex);
}

static void publish_configuration(const weather_config_t *config,
                                  uint32_t fingerprint)
{
    if (config == NULL ||
        xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    const bool configured = weather_config_is_configured(config);
    const bool selected_city_valid =
        weather_location_selection_is_valid(config->province_id,
                                            config->city_id);
    const weather_city_t *const selected_city =
        selected_city_valid
            ? weather_location_city_by_id(config->city_id)
            : NULL;
    const bool matching_cache = s_cache_available && configured &&
        s_cache.location_fingerprint == fingerprint;
    const bool available = config->enabled && matching_cache;
    const bool preserve_refresh =
        config->enabled && configured &&
        s_status.state == WEATHER_SERVICE_STATE_REFRESHING;
    const bool preserve_failure =
        config->enabled && configured &&
        s_status.state == WEATHER_SERVICE_STATE_FAILED &&
        s_status.data_available == available;
    const weather_service_state_t state =
        !config->enabled ? WEATHER_SERVICE_STATE_DISABLED
                         : (preserve_refresh
                                ? WEATHER_SERVICE_STATE_REFRESHING
                                : preserve_failure
                                ? WEATHER_SERVICE_STATE_FAILED
                                : (available ? WEATHER_SERVICE_STATE_READY
                                             : WEATHER_SERVICE_STATE_NO_DATA));
    const char *const location_name =
        available ? s_cache.location_name
                  : (config->enabled && selected_city != NULL
                         ? selected_city->name_zh
                         : "");
    if (s_status.config_enabled != config->enabled ||
        s_status.data_available != available || s_status.state != state ||
        strcmp(s_status.location_name, location_name) != 0) {
        s_status.config_enabled = config->enabled;
        s_status.data_available = available;
        s_status.state = state;
        if (!preserve_refresh && !preserve_failure) {
            s_status.failure_stage = WEATHER_FAILURE_STAGE_NONE;
            s_status.last_error = ESP_OK;
        }
        if (available) {
            memcpy(s_status.location_name, s_cache.location_name,
                   sizeof(s_status.location_name));
            s_status.snapshot = s_cache.snapshot;
            s_status.daily_fetched_at_epoch_seconds =
                s_cache.daily_fetched_at_epoch_seconds;
        } else {
            memcpy(s_status.location_name, location_name,
                   strlen(location_name) + 1U);
            weather_snapshot_clear(&s_status.snapshot);
            s_status.daily_fetched_at_epoch_seconds = 0;
        }
        increment_revision();
    }
    xSemaphoreGive(s_mutex);
}

static bool automatic_refresh_enabled(void)
{
    bool enabled = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        enabled = s_status.automatic_refresh_enabled;
        xSemaphoreGive(s_mutex);
    }
    return enabled;
}

static esp_err_t load_configuration(weather_config_snapshot_t *snapshot,
                                    uint32_t *fingerprint)
{
    esp_err_t error = weather_config_get_snapshot(snapshot);
    if (error != ESP_OK) {
        return error;
    }
    *fingerprint = weather_location_fingerprint(
        snapshot->config.province_id, snapshot->config.city_id);
    publish_configuration(&snapshot->config, *fingerprint);
    return ESP_OK;
}

static bool refresh_due(int64_t fetched_at, int64_t now,
                        int64_t interval)
{
    return !usable_epoch(fetched_at) || !usable_epoch(now) ||
           now < fetched_at || now - fetched_at >= interval;
}

static bool retry_wait_active(void)
{
    if (!s_retry_pending) {
        return false;
    }
    const TickType_t elapsed =
        xTaskGetTickCount() - s_retry_started_tick;
    if (elapsed < s_retry_duration_ticks) {
        return true;
    }
    s_retry_pending = false;
    return false;
}

static void clear_retry_policy(void)
{
    s_retry_pending = false;
    s_authentication_blocked = false;
}

static void schedule_retry(esp_err_t error, uint32_t generation)
{
    if (error == ESP_ERR_NOT_ALLOWED) {
        /* A bad Host/Key cannot heal by polling. Wait for the user to save a
         * new configuration, while still allowing an explicit forced try. */
        s_retry_pending = false;
        s_authentication_blocked = true;
        s_authentication_generation = generation;
        return;
    }
    const uint32_t delay_seconds =
        error == ESP_ERR_INVALID_STATE ? WEATHER_BUSY_RETRY_SECONDS
                                       : WEATHER_RETRY_SECONDS;
    s_retry_started_tick = xTaskGetTickCount();
    s_retry_duration_ticks =
        pdMS_TO_TICKS(delay_seconds * 1000U);
    s_retry_pending = true;
}

static void copy_cached_location(weather_client_location_t *location)
{
    location->latitude_ten_thousandths =
        s_cache.latitude_ten_thousandths;
    location->longitude_ten_thousandths =
        s_cache.longitude_ten_thousandths;
    memcpy(location->name, s_cache.location_name,
           sizeof(location->name));
}

static void release_failed_maintenance_handoff(bool from_maintenance)
{
    if (from_maintenance) {
        network_time_end_maintenance();
    }
}

static void perform_refresh_locked(bool force, bool from_maintenance)
{
    weather_config_snapshot_t config_snapshot = {0};
    uint32_t fingerprint = 0U;
    esp_err_t error = load_configuration(&config_snapshot, &fingerprint);
    if (s_authentication_blocked &&
        config_snapshot.generation != s_authentication_generation) {
        clear_retry_policy();
    }
    if (error != ESP_OK || !config_snapshot.config.enabled ||
        !weather_config_is_configured(&config_snapshot.config) ||
        !weather_location_selection_is_valid(
            config_snapshot.config.province_id,
            config_snapshot.config.city_id)) {
        release_failed_maintenance_handoff(from_maintenance);
        if (error != ESP_OK) {
            set_state(WEATHER_SERVICE_STATE_FAILED,
                      WEATHER_FAILURE_STAGE_LOCATION, error);
        } else if (from_maintenance) {
            /* A validated portal request should never reach this branch, but
             * always finish the interactive request if stored state changes
             * unexpectedly during the handoff. */
            set_state(WEATHER_SERVICE_STATE_FAILED,
                      WEATHER_FAILURE_STAGE_LOCATION,
                      ESP_ERR_INVALID_ARG);
        }
        weather_config_clear_sensitive(&config_snapshot,
                                       sizeof(config_snapshot));
        return;
    }

    const int64_t now = (int64_t)time(NULL);
    const bool matching_cache = s_cache_available &&
        s_cache.location_fingerprint == fingerprint;
    const bool current_due = force || !matching_cache ||
        refresh_due(s_cache.snapshot.fetched_at_epoch_seconds, now,
                    WEATHER_CURRENT_REFRESH_SECONDS);
    const bool daily_due = force || !matching_cache ||
        refresh_due(s_cache.daily_fetched_at_epoch_seconds, now,
                    WEATHER_DAILY_REFRESH_SECONDS);
    if (!current_due && !daily_due) {
        release_failed_maintenance_handoff(from_maintenance);
        weather_config_clear_sensitive(&config_snapshot,
                                       sizeof(config_snapshot));
        return;
    }
    if (!force &&
        ((s_authentication_blocked &&
          config_snapshot.generation == s_authentication_generation) ||
         retry_wait_active())) {
        release_failed_maintenance_handoff(from_maintenance);
        weather_config_clear_sensitive(&config_snapshot,
                                       sizeof(config_snapshot));
        return;
    }

    set_state(WEATHER_SERVICE_STATE_REFRESHING,
              WEATHER_FAILURE_STAGE_NONE, ESP_OK);
    error = from_maintenance
                ? network_time_begin_online_session_from_maintenance(
                      WEATHER_NETWORK_TIMEOUT_MS)
                : network_time_begin_online_session(
                      WEATHER_NETWORK_TIMEOUT_MS);
    if (error != ESP_OK) {
        release_failed_maintenance_handoff(from_maintenance);
        schedule_retry(error, config_snapshot.generation);
        set_state(WEATHER_SERVICE_STATE_FAILED,
                  WEATHER_FAILURE_STAGE_NETWORK, error);
        weather_config_clear_sensitive(&config_snapshot,
                                       sizeof(config_snapshot));
        return;
    }

    weather_cache_data_t candidate = {0};
    weather_client_location_t location = {0};
    weather_failure_stage_t failure_stage =
        WEATHER_FAILURE_STAGE_NONE;
    if (matching_cache) {
        candidate = s_cache;
        copy_cached_location(&location);
    } else {
        error = weather_client_resolve_location(
            &config_snapshot.config, &location);
        if (error != ESP_OK) {
            failure_stage = WEATHER_FAILURE_STAGE_LOCATION;
        }
        if (error == ESP_OK) {
            candidate.location_fingerprint = fingerprint;
            candidate.latitude_ten_thousandths =
                location.latitude_ten_thousandths;
            candidate.longitude_ten_thousandths =
                location.longitude_ten_thousandths;
            memcpy(candidate.location_name, location.name,
                   sizeof(candidate.location_name));
        }
    }

    esp_err_t current_error = ESP_OK;
    esp_err_t daily_error = ESP_OK;
    bool changed = false;
    const int64_t fetched_at = (int64_t)time(NULL);
    if (error == ESP_OK && current_due) {
        weather_current_t current = {0};
        current_error = weather_client_fetch_current(
            &config_snapshot.config, &location, &current);
        if (current_error == ESP_OK && usable_epoch(fetched_at)) {
            candidate.snapshot.current = current;
            candidate.snapshot.fetched_at_epoch_seconds = fetched_at;
            changed = true;
        } else if (current_error == ESP_OK) {
            current_error = ESP_ERR_INVALID_STATE;
        }
    }
    if (error == ESP_OK && daily_due) {
        weather_daily_t daily = {0};
        daily_error = weather_client_fetch_daily(
            &config_snapshot.config, &location, &daily);
        if (daily_error == ESP_OK && usable_epoch(fetched_at)) {
            candidate.snapshot.daily = daily;
            candidate.daily_fetched_at_epoch_seconds = fetched_at;
            changed = true;
        } else if (daily_error == ESP_OK) {
            daily_error = ESP_ERR_INVALID_STATE;
        }
    }
    if (error == ESP_OK && current_error != ESP_OK) {
        error = current_error;
        failure_stage = WEATHER_FAILURE_STAGE_CURRENT;
    }
    if (error == ESP_OK && daily_error != ESP_OK) {
        error = daily_error;
        failure_stage = WEATHER_FAILURE_STAGE_DAILY;
    }

    esp_err_t save_error = ESP_OK;
    if (changed && weather_cache_data_is_valid(&candidate)) {
        save_error = weather_cache_save(&candidate);
        publish_cache(&candidate,
                      error == ESP_OK && save_error == ESP_OK
                          ? WEATHER_SERVICE_STATE_READY
                          : WEATHER_SERVICE_STATE_FAILED,
                      error != ESP_OK
                          ? failure_stage
                          : (save_error != ESP_OK
                                 ? WEATHER_FAILURE_STAGE_CACHE
                                 : WEATHER_FAILURE_STAGE_NONE),
                      error != ESP_OK ? error : save_error);
    } else if (error != ESP_OK) {
        set_state(WEATHER_SERVICE_STATE_FAILED, failure_stage, error);
    } else if (!matching_cache) {
        error = ESP_ERR_INVALID_RESPONSE;
        failure_stage = WEATHER_FAILURE_STAGE_CACHE;
        set_state(WEATHER_SERVICE_STATE_FAILED, failure_stage, error);
    }

    const esp_err_t release_error = network_time_end_online_session();
    if (error == ESP_OK && save_error == ESP_OK &&
        release_error != ESP_OK) {
        error = release_error;
        failure_stage = WEATHER_FAILURE_STAGE_NETWORK;
        set_state(WEATHER_SERVICE_STATE_FAILED, failure_stage, error);
    }
    if (error == ESP_OK && save_error == ESP_OK) {
        clear_retry_policy();
    } else {
        schedule_retry(error != ESP_OK ? error : save_error,
                       config_snapshot.generation);
    }
    weather_config_clear_sensitive(&config_snapshot,
                                   sizeof(config_snapshot));
}

static void perform_refresh(bool force, bool from_maintenance)
{
    if (xSemaphoreTake(s_operation_mutex, portMAX_DELAY) == pdTRUE) {
        perform_refresh_locked(force, from_maintenance);
        xSemaphoreGive(s_operation_mutex);
    } else {
        release_failed_maintenance_handoff(from_maintenance);
    }
}

static void weather_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(5000U));
    while (true) {
        const EventBits_t bits = xEventGroupWaitBits(
            s_events, WEATHER_EVENT_ALL, pdTRUE, pdFALSE,
            pdMS_TO_TICKS(WEATHER_CHECK_INTERVAL_MS));
        if ((bits & WEATHER_EVENT_MAINTENANCE_REFRESH) != 0U) {
            perform_refresh(true, true);
        } else if ((bits & WEATHER_EVENT_FORCE_REFRESH) != 0U) {
            perform_refresh(true, false);
        } else {
            weather_config_snapshot_t snapshot = {0};
            uint32_t fingerprint = 0U;
            if (load_configuration(&snapshot, &fingerprint) == ESP_OK) {
                const bool should_refresh =
                    automatic_refresh_enabled() && snapshot.config.enabled &&
                    weather_config_is_configured(&snapshot.config);
                weather_config_clear_sensitive(&snapshot,
                                               sizeof(snapshot));
                if (should_refresh) {
                    perform_refresh(false, false);
                }
            }
        }
    }
}

esp_err_t weather_service_init(bool automatic_refresh_enabled)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_operation_mutex == NULL) {
        s_operation_mutex = xSemaphoreCreateMutex();
    }
    if (s_events == NULL) {
        s_events = xEventGroupCreate();
    }
    if (s_mutex == NULL || s_operation_mutex == NULL || s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = weather_config_init();
    if (error == ESP_OK) {
        error = weather_cache_init();
    }
    if (error != ESP_OK) {
        return error;
    }

    weather_cache_data_t cached = {0};
    if (weather_cache_get(&cached) == ESP_OK) {
        s_cache = cached;
        s_cache_available = true;
    }
    s_status.automatic_refresh_enabled = automatic_refresh_enabled;
    weather_config_snapshot_t config = {0};
    uint32_t fingerprint = 0U;
    error = load_configuration(&config, &fingerprint);
    weather_config_clear_sensitive(&config, sizeof(config));
    if (error != ESP_OK) {
        return error;
    }

    if (xTaskCreate(weather_task, "weather", WEATHER_TASK_STACK_BYTES,
                    NULL, WEATHER_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    if (automatic_refresh_enabled) {
        xEventGroupSetBits(s_events, WEATHER_EVENT_CHECK);
    }
    return ESP_OK;
}

esp_err_t weather_service_get_status(weather_service_status_t *status,
                                     int64_t now_epoch_seconds,
                                     bool rtc_valid)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *status = s_status;
    xSemaphoreGive(s_mutex);
    status->freshness = status->data_available
                            ? weather_snapshot_freshness(
                                  &status->snapshot,
                                  now_epoch_seconds, rtc_valid)
                            : WEATHER_FRESHNESS_UNKNOWN;
    status->daily_freshness = status->data_available
                                  ? weather_daily_freshness_evaluate(
                                        status->daily_fetched_at_epoch_seconds,
                                        now_epoch_seconds, rtc_valid)
                                  : WEATHER_FRESHNESS_UNKNOWN;
    return ESP_OK;
}

esp_err_t weather_service_set_automatic_refresh_enabled(bool enabled)
{
    if (!s_initialized || s_mutex == NULL || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const bool changed = s_status.automatic_refresh_enabled != enabled;
    s_status.automatic_refresh_enabled = enabled;
    if (changed) {
        increment_revision();
    }
    xSemaphoreGive(s_mutex);
    if (enabled) {
        xEventGroupSetBits(s_events, WEATHER_EVENT_CHECK);
    }
    return ESP_OK;
}

esp_err_t weather_service_request_refresh(void)
{
    if (!s_initialized || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(s_events, WEATHER_EVENT_FORCE_REFRESH);
    return ESP_OK;
}

esp_err_t weather_service_request_refresh_from_maintenance(void)
{
    if (!s_initialized || s_mutex == NULL || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    weather_config_snapshot_t config_snapshot = {0};
    uint32_t fingerprint = 0U;
    esp_err_t error = load_configuration(&config_snapshot,
                                         &fingerprint);
    const bool configured =
        error == ESP_OK && config_snapshot.config.enabled &&
        weather_config_is_configured(&config_snapshot.config) &&
        weather_location_selection_is_valid(
            config_snapshot.config.province_id,
            config_snapshot.config.city_id);
    weather_config_clear_sensitive(&config_snapshot,
                                   sizeof(config_snapshot));
    if (error != ESP_OK) {
        return error;
    }
    if (!configured) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_status.interactive_refresh = true;
    s_status.state = WEATHER_SERVICE_STATE_REFRESHING;
    s_status.failure_stage = WEATHER_FAILURE_STAGE_NONE;
    s_status.last_error = ESP_OK;
    increment_revision();
    xSemaphoreGive(s_mutex);
    xEventGroupSetBits(s_events, WEATHER_EVENT_MAINTENANCE_REFRESH);
    return ESP_OK;
}

esp_err_t weather_service_acknowledge_interactive_refresh(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_status.interactive_refresh) {
        s_status.interactive_refresh = false;
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t weather_service_notify_configuration_changed(void)
{
    if (!s_initialized || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(s_events, WEATHER_EVENT_CHECK);
    return ESP_OK;
}

esp_err_t weather_service_clear_cache(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_operation_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t error = weather_cache_clear();
    if (error != ESP_OK) {
        xSemaphoreGive(s_operation_mutex);
        return error;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(s_operation_mutex);
        return ESP_ERR_TIMEOUT;
    }
    memset(&s_cache, 0, sizeof(s_cache));
    s_cache_available = false;
    s_status.config_enabled = false;
    s_status.data_available = false;
    s_status.interactive_refresh = false;
    s_status.location_name[0] = '\0';
    weather_snapshot_clear(&s_status.snapshot);
    s_status.daily_fetched_at_epoch_seconds = 0;
    s_status.state = WEATHER_SERVICE_STATE_DISABLED;
    s_status.failure_stage = WEATHER_FAILURE_STAGE_NONE;
    s_status.last_error = ESP_OK;
    clear_retry_policy();
    increment_revision();
    xSemaphoreGive(s_mutex);
    xSemaphoreGive(s_operation_mutex);
    return ESP_OK;
}

const char *weather_service_state_name(weather_service_state_t state)
{
    switch (state) {
    case WEATHER_SERVICE_STATE_DISABLED:
        return "disabled";
    case WEATHER_SERVICE_STATE_NO_DATA:
        return "no data";
    case WEATHER_SERVICE_STATE_REFRESHING:
        return "refreshing";
    case WEATHER_SERVICE_STATE_READY:
        return "ready";
    case WEATHER_SERVICE_STATE_FAILED:
        return "failed";
    default:
        return "invalid";
    }
}
