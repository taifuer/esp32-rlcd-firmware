#include "market_service.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "market_client.h"
#include "market_config.h"
#include "network_time.h"

enum {
    MARKET_TASK_STACK_BYTES = 8192,
    MARKET_TASK_PRIORITY = 3,
    MARKET_NETWORK_TIMEOUT_MS = 10000,
    MARKET_CHECK_INTERVAL_MS = 1000,
};

static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static bool s_initialized;
static bool s_requested;
static bool s_running;
static uint32_t s_generation;
static uint32_t s_visibility_generation;
static market_service_status_t s_status;
static market_row_t s_cache[MARKET_PRESET_COUNT];
static bool s_attempted;
static int64_t s_last_attempt_us;

typedef struct {
    uint32_t configuration;
    uint32_t visibility;
    bool manual;
} request_context_t;

static void revised(void)
{
    if (++s_status.revision == 0) ++s_status.revision;
}

static void publish_rows(void)
{
    memset(s_status.rows, 0, sizeof(s_status.rows));
    for (size_t i = 0; i < s_status.config.count; ++i) {
        const uint8_t id = s_status.config.ids[i];
        s_status.rows[i] = s_cache[id];
        s_status.rows[i].id = id;
    }
}

static market_service_state_t idle_state(void)
{
    if (!s_status.config.enabled) return MARKET_SERVICE_STATE_DISABLED;
    for (size_t i = 0; i < s_status.config.count; ++i) {
        if (s_status.rows[i].valid) return MARKET_SERVICE_STATE_READY;
    }
    return MARKET_SERVICE_STATE_NO_DATA;
}

static esp_err_t update_configuration(void)
{
    market_config_t config;
    uint32_t generation;
    esp_err_t error = market_config_get_snapshot(&config, &generation);
    if (error != ESP_OK) return error;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    /* A portal save can publish a newer snapshot while this worker waits
     * for the status lock. Never roll the service back to the older one. */
    const uint32_t generation_delta = generation - s_generation;
    if (generation_delta >= UINT32_C(0x80000000)) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }
    if (generation != s_generation || !market_config_equal(&config, &s_status.config)) {
        s_status.config = config;
        s_generation = generation;
        s_requested = false;
        s_attempted = false;
        publish_rows();
        s_status.state = idle_state();
        s_status.last_error = ESP_OK;
        revised();
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

static bool request_cancelled(void *argument)
{
    const request_context_t *request = argument;
    market_config_t latest;
    uint32_t generation;
    if (market_config_get_snapshot(&latest, &generation) != ESP_OK ||
        generation != request->configuration) return true;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return true;
    const bool cancelled = !s_status.visible || !s_status.config.enabled ||
        s_visibility_generation != request->visibility ||
        s_generation != request->configuration ||
        (!request->manual && !s_status.automatic_refresh_enabled);
    xSemaphoreGive(s_mutex);
    return cancelled;
}

static void refresh_if_due(void)
{
    if (update_configuration() != ESP_OK) return;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return;
    const int64_t now_us = esp_timer_get_time();
    const uint64_t elapsed_ms = now_us >= s_last_attempt_us
        ? (uint64_t)(now_us - s_last_attempt_us) / 1000 : 0;
    if (!market_refresh_due(s_status.config.enabled, s_status.visible,
            s_status.automatic_refresh_enabled, s_requested, s_attempted,
            elapsed_ms)) {
        xSemaphoreGive(s_mutex);
        return;
    }
    const market_config_t config = s_status.config;
    request_context_t request = {
        .configuration = s_generation,
        .visibility = s_visibility_generation,
        .manual = s_requested,
    };
    s_requested = false;
    s_running = true;
    s_attempted = true;
    s_last_attempt_us = now_us;
    s_status.state = MARKET_SERVICE_STATE_REFRESHING;
    s_status.last_error = ESP_OK;
    revised();
    xSemaphoreGive(s_mutex);

    market_row_t rows[MARKET_MAX_ROWS] = {0};
    for (size_t i = 0; i < config.count; ++i) rows[i].id = config.ids[i];
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!request_cancelled(&request)) {
        error = network_time_begin_online_session(MARKET_NETWORK_TIMEOUT_MS);
        if (error == ESP_OK) {
            error = market_client_fetch(&config, rows, request_cancelled, &request);
            /* Only the successful acquirer may release this session. */
            const esp_err_t release_error = network_time_end_online_session();
            if (error == ESP_OK) error = release_error;
        }
    }
    const bool cancelled = request_cancelled(&request);
    const int64_t now_epoch = (int64_t)time(NULL);
    for (size_t i = 0; i < config.count; ++i) {
        if (rows[i].valid && !market_quote_time_plausible(&rows[i], now_epoch)) rows[i].valid = false;
    }
    (void)update_configuration();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return;
    s_running = false;
    if (cancelled || s_generation != request.configuration) {
        s_status.state = idle_state();
        s_status.last_error = ESP_OK;
    } else {
        const size_t accepted = market_merge_rows(s_cache, rows, config.count);
        publish_rows();
        if (accepted != config.count && error == ESP_OK) error = ESP_ERR_INVALID_RESPONSE;
        s_status.state = error == ESP_OK ? MARKET_SERVICE_STATE_READY : MARKET_SERVICE_STATE_FAILED;
        s_status.last_error = error;
    }
    revised();
    xSemaphoreGive(s_mutex);
    if (!cancelled && error != ESP_OK) ESP_LOGW("market", "refresh failed: %s", esp_err_to_name(error));
}

static void market_task(void *argument)
{
    (void)argument;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MARKET_CHECK_INTERVAL_MS));
        refresh_if_due();
    }
}

esp_err_t market_service_init(void)
{
    if (s_initialized) return ESP_OK;
    if (s_mutex == NULL) s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) return ESP_ERR_NO_MEM;
    esp_err_t error = market_config_init();
    if (error == ESP_OK) error = update_configuration();
    if (error != ESP_OK) return error;
    if (xTaskCreate(market_task, "market", MARKET_TASK_STACK_BYTES,
                    NULL, MARKET_TASK_PRIORITY, &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t market_service_set_activity(bool visible,
                                      bool automatic_refresh_enabled)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    const bool changed = s_status.visible != visible ||
                         s_status.automatic_refresh_enabled != automatic_refresh_enabled;
    if (s_status.visible != visible) ++s_visibility_generation;
    s_status.visible = visible;
    s_status.automatic_refresh_enabled = automatic_refresh_enabled;
    if (!visible) {
        s_requested = false;
        if (!s_running && s_status.state == MARKET_SERVICE_STATE_REFRESHING) {
            s_status.state = idle_state();
            s_status.last_error = ESP_OK;
        }
    }
    if (changed) revised();
    xSemaphoreGive(s_mutex);
    if (changed) xTaskNotifyGive(s_task);
    return ESP_OK;
}

esp_err_t market_service_request_refresh(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (!s_status.config.enabled || !s_status.visible || s_requested || s_running) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_requested = true;
    s_status.state = MARKET_SERVICE_STATE_REFRESHING;
    s_status.last_error = ESP_OK;
    revised();
    xSemaphoreGive(s_mutex);
    xTaskNotifyGive(s_task);
    return ESP_OK;
}

esp_err_t market_service_notify_configuration_changed(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    const esp_err_t error = update_configuration();
    if (error == ESP_OK) xTaskNotifyGive(s_task);
    return error;
}

esp_err_t market_service_get_status(market_service_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    *status = s_status;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
