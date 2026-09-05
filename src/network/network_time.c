#include "network_time.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_storage.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network_connection_policy.h"
#include "network_credentials.h"
#include "network_retry_policy.h"
#include "network_session_policy.h"
#include "network_station_link.h"
#include "nvs.h"

#define NETWORK_NAMESPACE "rlcd_net"
#define NETWORK_SSID_KEY "ssid"
#define NETWORK_PASSWORD_KEY "password"

#define NETWORK_EVENT_CONNECTED BIT0
#define NETWORK_EVENT_FAILED BIT1
#define NETWORK_EVENT_CREDENTIALS_SAVED BIT2
#define NETWORK_EVENT_SYNC_REQUEST BIT3
#define NETWORK_EVENT_MAINTENANCE_CHANGED BIT4
#define NETWORK_EVENT_POLICY_CHANGED BIT5
#define NETWORK_EVENT_LINK_RECOVERY BIT6
#define NETWORK_EVENT_FORCE_PROVISIONING BIT7

#define NETWORK_EVENT_TASK_WAKE                                               \
    (NETWORK_EVENT_SYNC_REQUEST | NETWORK_EVENT_MAINTENANCE_CHANGED |        \
     NETWORK_EVENT_POLICY_CHANGED | NETWORK_EVENT_LINK_RECOVERY |            \
     NETWORK_EVENT_FORCE_PROVISIONING)

#define NETWORK_STATION_MAX_RETRIES 5U
#define NETWORK_STATION_TIMEOUT_MS 40000U
#define NETWORK_STATION_STOP_RETRY_MS 5000U
#define NETWORK_SNTP_ATTEMPTS 5U
#define NETWORK_SNTP_WAIT_MS 4000U
#define NETWORK_SETUP_WINDOW_MS 300000U
#define NETWORK_RESYNC_INTERVAL_MS 86400000U
#define NETWORK_FORM_MAX_LENGTH 384U

static const char *TAG = "network_time";
static const char SETUP_URL[] = "http://192.168.4.1";

static const char SETUP_PAGE[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ESP32 RLCD 配网</title><style>"
    "body{font:16px system-ui,sans-serif;max-width:30rem;margin:3rem auto;padding:0 1rem;"
    "color:#171717}h1{font-size:1.6rem}label{display:block;margin:1rem 0 .35rem}"
    "input,button{box-sizing:border-box;width:100%;font:inherit;padding:.75rem;border:1px solid #888;"
    "border-radius:.45rem}button{margin-top:1.25rem;background:#171717;color:#fff;border:0}"
    "small{color:#555}</style></head><body><h1>ESP32 RLCD 配网</h1>"
    "<p>请输入设备需要连接的 2.4 GHz Wi-Fi。凭据只保存在设备 NVS 中。</p>"
    "<form method=\"post\" action=\"/configure\" autocomplete=\"off\">"
    "<label for=\"ssid\">Wi-Fi 名称</label>"
    "<input id=\"ssid\" name=\"ssid\" maxlength=\"32\" required autofocus>"
    "<label for=\"password\">Wi-Fi 密码</label>"
    "<input id=\"password\" name=\"password\" type=\"password\" minlength=\"8\" "
    "maxlength=\"63\" autocomplete=\"new-password\">"
    "<small>开放网络请留空；暂不支持企业认证网络。</small>"
    "<button type=\"submit\">保存并校时</button></form></body></html>";

static const char SETUP_SUCCESS_PAGE[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>配置已保存</title></head><body><h1>配置已保存</h1>"
    "<p>设备正在关闭临时热点、连接 Wi-Fi 并校准时间。现在可以关闭此页面。</p>"
    "</body></html>";

static const char SETUP_ERROR_PAGE[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>配置无效</title></head><body><h1>配置无效</h1>"
    "<p>请确认 Wi-Fi 名称不超过 32 字节；密码应留空或为 8—63 个 ASCII 字符。</p>"
    "<p><a href=\"/\">返回重新填写</a></p></body></html>";

static EventGroupHandle_t s_events;
static QueueHandle_t s_datetime_queue;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_station_netif;
static httpd_handle_t s_http_server;
static bool s_initialized;
static bool s_storage_ready;
static bool s_automatic_sync_enabled = true;
static bool s_startup_provisioning_enabled = true;
/* Suppress the normal STA_START auto-connect while a settings-portal
 * candidate is being configured in APSTA mode. */
static bool s_station_connect_deferred;
/* Prevent a deliberate esp_wifi_stop() from being mistaken for a link
 * failure that should immediately reconnect. Guarded by s_status_lock. */
static bool s_station_stop_requested;
/* A failed driver stop must be completed before a persistent STA may be
 * reused. Guarded by s_status_lock. */
static bool s_radio_normalization_required;
static volatile uint32_t s_station_retries;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static network_session_policy_t s_session_policy;
static network_station_link_t s_station_link;
static network_time_status_t s_status = {
    .state = NETWORK_TIME_STATE_UNINITIALIZED,
    .automatic_sync_enabled = true,
    .last_error = ESP_OK,
    .last_failure = NETWORK_TIME_FAILURE_NONE,
};

static bool exclusive_session_active(void);
static esp_err_t load_credentials(network_credentials_t *credentials);
static bool credentials_are_missing(void);
static esp_err_t stop_wifi(void);
static esp_err_t start_station(const network_credentials_t *credentials,
                               uint32_t timeout_ms,
                               network_time_failure_t *failure);
static esp_err_t ensure_station_connected(
    const network_credentials_t *credentials, uint32_t timeout_ms,
    network_time_failure_t *failure);

static void set_starting_unconfigured_status_locked(void)
{
    s_status.state = NETWORK_TIME_STATE_STARTING;
    s_status.configured = false;
    s_status.automatic_sync_enabled = s_automatic_sync_enabled;
    s_status.last_error = ESP_OK;
    s_status.last_failure = NETWORK_TIME_FAILURE_NONE;
    memset(s_status.setup_ssid, 0, sizeof(s_status.setup_ssid));
    memset(s_status.setup_password, 0, sizeof(s_status.setup_password));
    memset(s_status.setup_url, 0, sizeof(s_status.setup_url));
}

static bool get_automatic_sync_enabled(void)
{
    bool enabled;
    portENTER_CRITICAL(&s_status_lock);
    enabled = s_automatic_sync_enabled;
    portEXIT_CRITICAL(&s_status_lock);
    return enabled;
}

static network_connection_mode_t current_connection_mode(void)
{
    return get_automatic_sync_enabled()
               ? NETWORK_CONNECTION_MODE_NORMAL
               : NETWORK_CONNECTION_MODE_SAVING;
}

static void get_station_link_state(bool *active, bool *connected)
{
    portENTER_CRITICAL(&s_status_lock);
    if (active != NULL) {
        *active = network_station_link_is_active(&s_station_link);
    }
    if (connected != NULL) {
        *connected = network_station_link_is_connected(&s_station_link);
    }
    portEXIT_CRITICAL(&s_status_lock);
}

static bool maintenance_session_owned(void)
{
    bool owned;
    portENTER_CRITICAL(&s_status_lock);
    owned = s_session_policy.owner == NETWORK_SESSION_OWNER_MAINTENANCE;
    portEXIT_CRITICAL(&s_status_lock);
    return owned;
}

static bool station_normalization_required(void)
{
    bool required;
    portENTER_CRITICAL(&s_status_lock);
    required = s_station_connect_deferred ||
               s_radio_normalization_required;
    portEXIT_CRITICAL(&s_status_lock);
    return required;
}

static void set_status(network_time_state_t state, bool configured, esp_err_t error)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.state = state;
    s_status.configured = configured;
    s_status.automatic_sync_enabled = s_automatic_sync_enabled;
    s_status.last_error = error;
    s_status.last_failure = NETWORK_TIME_FAILURE_NONE;
    memset(s_status.setup_ssid, 0, sizeof(s_status.setup_ssid));
    memset(s_status.setup_password, 0, sizeof(s_status.setup_password));
    memset(s_status.setup_url, 0, sizeof(s_status.setup_url));
    portEXIT_CRITICAL(&s_status_lock);
}

static void set_failure_status(network_time_state_t state, bool configured,
                               esp_err_t error,
                               network_time_failure_t failure)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.state = state;
    s_status.configured = configured;
    s_status.automatic_sync_enabled = s_automatic_sync_enabled;
    s_status.last_error = error;
    s_status.last_failure = failure;
    memset(s_status.setup_ssid, 0, sizeof(s_status.setup_ssid));
    memset(s_status.setup_password, 0, sizeof(s_status.setup_password));
    memset(s_status.setup_url, 0, sizeof(s_status.setup_url));
    portEXIT_CRITICAL(&s_status_lock);
}

static void set_provisioning_status(bool configured, esp_err_t reason,
                                    const char *ssid, const char *password)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.state = NETWORK_TIME_STATE_PROVISIONING;
    s_status.configured = configured;
    s_status.automatic_sync_enabled = s_automatic_sync_enabled;
    s_status.last_error = reason;
    s_status.last_failure = NETWORK_TIME_FAILURE_NONE;
    snprintf(s_status.setup_ssid, sizeof(s_status.setup_ssid), "%s", ssid);
    snprintf(s_status.setup_password, sizeof(s_status.setup_password), "%s", password);
    snprintf(s_status.setup_url, sizeof(s_status.setup_url), "%s", SETUP_URL);
    portEXIT_CRITICAL(&s_status_lock);
}

const char *network_time_state_name(network_time_state_t state)
{
    switch (state) {
    case NETWORK_TIME_STATE_UNINITIALIZED:
        return "uninitialized";
    case NETWORK_TIME_STATE_STARTING:
        return "starting";
    case NETWORK_TIME_STATE_PROVISIONING:
        return "provisioning";
    case NETWORK_TIME_STATE_CONNECTING:
        return "connecting";
    case NETWORK_TIME_STATE_SYNCHRONIZING:
        return "synchronizing";
    case NETWORK_TIME_STATE_SYNCHRONIZED:
        return "synchronized";
    case NETWORK_TIME_STATE_RETRY_WAIT:
        return "retry_wait";
    case NETWORK_TIME_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

const char *network_time_failure_name(network_time_failure_t failure)
{
    switch (failure) {
    case NETWORK_TIME_FAILURE_NONE:
        return "none";
    case NETWORK_TIME_FAILURE_WIFI:
        return "wifi";
    case NETWORK_TIME_FAILURE_NTP:
        return "ntp";
    case NETWORK_TIME_FAILURE_SERVICE:
        return "service";
    default:
        return "unknown";
    }
}

esp_err_t network_time_get_status(network_time_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    status->station_connected =
        network_station_link_is_connected(&s_station_link);
    portEXIT_CRITICAL(&s_status_lock);
    return ESP_OK;
}

esp_err_t network_time_request_sync(void)
{
    if (!s_initialized || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    network_time_status_t status = {0};
    (void)network_time_get_status(&status);
    if (!status.configured || exclusive_session_active() ||
        (status.state != NETWORK_TIME_STATE_SYNCHRONIZED &&
         status.state != NETWORK_TIME_STATE_RETRY_WAIT)) {
        return ESP_ERR_INVALID_STATE;
    }

    set_status(NETWORK_TIME_STATE_STARTING, true, ESP_OK);
    xEventGroupSetBits(s_events, NETWORK_EVENT_SYNC_REQUEST);
    ESP_LOGI(TAG, "manual time synchronization requested");
    return ESP_OK;
}

esp_err_t network_time_set_automatic_sync_enabled(bool enabled)
{
    if (!s_initialized || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    bool changed = false;
    bool task_reserved = false;
    network_connection_action_t action = NETWORK_CONNECTION_ACTION_NONE;
    portENTER_CRITICAL(&s_status_lock);
    changed = s_automatic_sync_enabled != enabled;
    s_automatic_sync_enabled = enabled;
    s_status.automatic_sync_enabled = enabled;
    if (changed) {
        network_connection_activity_t activity =
            NETWORK_CONNECTION_ACTIVITY_IDLE;
        if (s_session_policy.owner == NETWORK_SESSION_OWNER_ONLINE) {
            activity = NETWORK_CONNECTION_ACTIVITY_ONLINE;
        } else if (s_session_policy.owner ==
                   NETWORK_SESSION_OWNER_MAINTENANCE) {
            activity = NETWORK_CONNECTION_ACTIVITY_MAINTENANCE;
        } else if (s_session_policy.task_active) {
            activity = NETWORK_CONNECTION_ACTIVITY_SYNC;
        }
        action = network_connection_policy_on_mode_change(
            enabled ? NETWORK_CONNECTION_MODE_NORMAL
                    : NETWORK_CONNECTION_MODE_SAVING,
            activity);
        if ((action == NETWORK_CONNECTION_ACTION_CONNECT ||
             action == NETWORK_CONNECTION_ACTION_STOP) &&
            s_status.configured &&
            s_session_policy.owner == NETWORK_SESSION_OWNER_NONE) {
            task_reserved =
                network_session_policy_request_task(&s_session_policy);
            if (task_reserved && action == NETWORK_CONNECTION_ACTION_CONNECT) {
                s_status.state = NETWORK_TIME_STATE_STARTING;
                s_status.last_error = ESP_OK;
                s_status.last_failure = NETWORK_TIME_FAILURE_NONE;
            }
        }
    }
    portEXIT_CRITICAL(&s_status_lock);

    if (changed) {
        xEventGroupSetBits(s_events, NETWORK_EVENT_POLICY_CHANGED);
        ESP_LOGI(TAG,
                 "background network mode %s (%s%s)",
                 enabled ? "normal" : "saving",
                 action == NETWORK_CONNECTION_ACTION_DEFER ? "deferred"
                                                            : "scheduled",
                 task_reserved ? ", task reserved" : "");
    }
    return ESP_OK;
}

esp_err_t network_time_begin_maintenance(void)
{
    if (!s_initialized || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    if (s_http_server == NULL &&
        (s_status.state == NETWORK_TIME_STATE_SYNCHRONIZED ||
         s_status.state == NETWORK_TIME_STATE_RETRY_WAIT)) {
        accepted = network_session_policy_try_acquire(
            &s_session_policy, NETWORK_SESSION_OWNER_MAINTENANCE);
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupSetBits(s_events, NETWORK_EVENT_MAINTENANCE_CHANGED);
    const esp_err_t stop_error = stop_wifi();
    if (stop_error != ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        (void)network_session_policy_release(
            &s_session_policy, NETWORK_SESSION_OWNER_MAINTENANCE);
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupSetBits(s_events, NETWORK_EVENT_MAINTENANCE_CHANGED |
                                        NETWORK_EVENT_POLICY_CHANGED);
        ESP_LOGW(TAG, "could not pause station for maintenance: %s",
                 esp_err_to_name(stop_error));
        return stop_error;
    }
    ESP_LOGI(TAG, "network maintenance window acquired");
    return ESP_OK;
}

void network_time_end_maintenance(void)
{
    if (!s_initialized || s_events == NULL ||
        !maintenance_session_owned()) {
        return;
    }

    /* Settings and update portals manipulate the shared ESP-IDF radio while
     * maintenance owns it. Normalize the driver before returning ownership;
     * a failed stop is handed to the network task for a bounded retry. */
    const esp_err_t stop_error = stop_wifi();
    const network_connection_action_t action =
        network_connection_policy_on_operation_end(
            current_connection_mode(),
            NETWORK_CONNECTION_OPERATION_MAINTENANCE);
    bool released = false;
    bool reconnect_scheduled = false;
    portENTER_CRITICAL(&s_status_lock);
    if ((action == NETWORK_CONNECTION_ACTION_CONNECT ||
         stop_error != ESP_OK) &&
        s_status.configured) {
        reconnect_scheduled = network_session_policy_release_to_task(
            &s_session_policy, NETWORK_SESSION_OWNER_MAINTENANCE);
        released = reconnect_scheduled;
        if (reconnect_scheduled) {
            s_status.state = NETWORK_TIME_STATE_STARTING;
            s_status.last_error = ESP_OK;
            s_status.last_failure = NETWORK_TIME_FAILURE_NONE;
        }
    }
    if (!released) {
        released = network_session_policy_release(
            &s_session_policy, NETWORK_SESSION_OWNER_MAINTENANCE);
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (released) {
        xEventGroupSetBits(
            s_events, NETWORK_EVENT_MAINTENANCE_CHANGED |
                          (reconnect_scheduled
                               ? NETWORK_EVENT_POLICY_CHANGED
                               : 0U));
        ESP_LOGI(TAG, "network maintenance window released%s",
                 reconnect_scheduled ? "; station restore scheduled" : "");
    }
    if (stop_error != ESP_OK) {
        ESP_LOGW(TAG, "could not normalize Wi-Fi after maintenance: %s",
                 esp_err_to_name(stop_error));
    }
}

esp_err_t network_time_end_maintenance_and_request_provisioning(void)
{
    if (!s_initialized || !s_storage_ready || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!maintenance_session_owned()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!credentials_are_missing()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t stop_error = stop_wifi();
    if (stop_error != ESP_OK) {
        return stop_error;
    }

    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    if (!network_station_link_is_active(&s_station_link) &&
        s_http_server == NULL) {
        accepted = network_session_policy_release_to_task(
            &s_session_policy, NETWORK_SESSION_OWNER_MAINTENANCE);
        if (accepted) {
            set_starting_unconfigured_status_locked();
        }
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupSetBits(s_events, NETWORK_EVENT_FORCE_PROVISIONING |
                                    NETWORK_EVENT_MAINTENANCE_CHANGED);
    ESP_LOGI(TAG, "network maintenance handed off to provisioning");
    return ESP_OK;
}

esp_err_t network_time_end_maintenance_and_request_sync(void)
{
    if (!s_initialized || !s_storage_ready || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!maintenance_session_owned()) {
        return ESP_ERR_INVALID_STATE;
    }

    network_time_saved_network_t network = {0};
    const esp_err_t credentials_error =
        network_time_get_saved_network(&network);
    const bool configured = credentials_error == ESP_OK && network.configured;
    memset(&network, 0, sizeof(network));
    if (!configured) {
        return credentials_error == ESP_OK ? ESP_ERR_INVALID_STATE
                                           : credentials_error;
    }
    const esp_err_t stop_error = stop_wifi();
    if (stop_error != ESP_OK) {
        return stop_error;
    }

    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    /* The shared radio has been stopped above. Do not alter status unless the
     * ownership transfer succeeds. */
    if (s_http_server == NULL &&
        s_session_policy.owner == NETWORK_SESSION_OWNER_MAINTENANCE &&
        !s_session_policy.task_active) {
        accepted = network_session_policy_release_to_task(
            &s_session_policy, NETWORK_SESSION_OWNER_MAINTENANCE);
        if (accepted) {
            s_status.state = NETWORK_TIME_STATE_STARTING;
            s_status.configured = true;
            s_status.automatic_sync_enabled = s_automatic_sync_enabled;
            s_status.last_error = ESP_OK;
            s_status.last_failure = NETWORK_TIME_FAILURE_NONE;
            memset(s_status.setup_ssid, 0, sizeof(s_status.setup_ssid));
            memset(s_status.setup_password, 0,
                   sizeof(s_status.setup_password));
            memset(s_status.setup_url, 0, sizeof(s_status.setup_url));
        }
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupSetBits(s_events, NETWORK_EVENT_SYNC_REQUEST |
                                    NETWORK_EVENT_MAINTENANCE_CHANGED);
    ESP_LOGI(TAG, "network maintenance handed off to saved Wi-Fi sync");
    return ESP_OK;
}

static bool exclusive_session_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_status_lock);
    active = network_session_policy_is_exclusive(&s_session_policy);
    portEXIT_CRITICAL(&s_status_lock);
    return active;
}

static bool network_task_activity_begin(void)
{
    bool accepted;
    portENTER_CRITICAL(&s_status_lock);
    accepted = network_session_policy_try_begin_task(&s_session_policy);
    portEXIT_CRITICAL(&s_status_lock);
    return accepted;
}

static void network_task_activity_end(void)
{
    portENTER_CRITICAL(&s_status_lock);
    network_session_policy_end_task(&s_session_policy);
    portEXIT_CRITICAL(&s_status_lock);
}

static bool online_session_acquire(void)
{
    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    if (s_http_server == NULL && s_status.configured &&
        (s_status.state == NETWORK_TIME_STATE_SYNCHRONIZED ||
         s_status.state == NETWORK_TIME_STATE_RETRY_WAIT)) {
        accepted = network_session_policy_try_acquire(
            &s_session_policy, NETWORK_SESSION_OWNER_ONLINE);
    }
    portEXIT_CRITICAL(&s_status_lock);
    return accepted;
}

static bool maintenance_session_transfer_to_online(void)
{
    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    if (!network_station_link_is_active(&s_station_link) &&
        s_http_server == NULL && s_status.configured &&
        (s_status.state == NETWORK_TIME_STATE_SYNCHRONIZED ||
         s_status.state == NETWORK_TIME_STATE_RETRY_WAIT)) {
        accepted = network_session_policy_transfer(
            &s_session_policy, NETWORK_SESSION_OWNER_MAINTENANCE,
            NETWORK_SESSION_OWNER_ONLINE);
    }
    portEXIT_CRITICAL(&s_status_lock);
    return accepted;
}

static bool online_session_release(void)
{
    bool released;
    portENTER_CRITICAL(&s_status_lock);
    released = network_session_policy_release(
        &s_session_policy, NETWORK_SESSION_OWNER_ONLINE);
    portEXIT_CRITICAL(&s_status_lock);
    if (released) {
        xEventGroupSetBits(s_events, NETWORK_EVENT_MAINTENANCE_CHANGED);
    }
    return released;
}

static esp_err_t load_credentials(network_credentials_t *credentials)
{
    if (!s_storage_ready || credentials == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(credentials, 0, sizeof(*credentials));
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(NETWORK_NAMESPACE, NVS_READONLY, &handle);
    if (error != ESP_OK) {
        return error;
    }

    size_t ssid_size = sizeof(credentials->ssid);
    error = nvs_get_str(handle, NETWORK_SSID_KEY, credentials->ssid, &ssid_size);
    if (error == ESP_OK) {
        size_t password_size = sizeof(credentials->password);
        error = nvs_get_str(handle, NETWORK_PASSWORD_KEY, credentials->password,
                            &password_size);
    }
    nvs_close(handle);

    if (error == ESP_OK && !network_credentials_are_valid(credentials)) {
        memset(credentials, 0, sizeof(*credentials));
        return ESP_ERR_INVALID_STATE;
    }
    return error;
}

static bool credentials_are_missing(void)
{
    network_credentials_t credentials = {0};
    const esp_err_t error = load_credentials(&credentials);
    memset(&credentials, 0, sizeof(credentials));
    return error == ESP_ERR_NVS_NOT_FOUND;
}

static esp_err_t save_credentials(const network_credentials_t *credentials)
{
    if (!s_storage_ready || !network_credentials_are_valid(credentials)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(NETWORK_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_str(handle, NETWORK_SSID_KEY, credentials->ssid);
    }
    if (error == ESP_OK) {
        error = nvs_set_str(handle, NETWORK_PASSWORD_KEY, credentials->password);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return error;
}

esp_err_t network_time_get_saved_network(
    network_time_saved_network_t *network)
{
    if (network == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(network, 0, sizeof(*network));

    network_credentials_t credentials = {0};
    const esp_err_t error = load_credentials(&credentials);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (error == ESP_OK) {
        network->configured = true;
        memcpy(network->ssid, credentials.ssid, sizeof(network->ssid));
    }
    memset(&credentials, 0, sizeof(credentials));
    return error;
}

static esp_err_t erase_credentials(void)
{
    if (!s_storage_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(NETWORK_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_erase_all(handle);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return error;
}

esp_err_t network_time_clear_credentials(void)
{
    const esp_err_t error = erase_credentials();
    if (error == ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        if (s_status.state == NETWORK_TIME_STATE_PROVISIONING) {
            s_status.configured = false;
            s_status.automatic_sync_enabled = s_automatic_sync_enabled;
        } else {
            set_starting_unconfigured_status_locked();
        }
        portEXIT_CRITICAL(&s_status_lock);
    }
    return error;
}

esp_err_t network_time_request_provisioning(void)
{
    if (!s_initialized || !s_storage_ready || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!credentials_are_missing()) {
        return ESP_ERR_INVALID_STATE;
    }

    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    accepted = network_session_policy_request_task(&s_session_policy);
    if (accepted && s_status.state != NETWORK_TIME_STATE_PROVISIONING) {
        set_starting_unconfigured_status_locked();
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupSetBits(s_events, NETWORK_EVENT_FORCE_PROVISIONING);
    ESP_LOGI(TAG, "network provisioning requested");
    return ESP_OK;
}

esp_err_t network_time_forget_and_request_provisioning(void)
{
    esp_err_t error = network_time_begin_maintenance();
    if (error != ESP_OK) {
        return error;
    }

    error = network_time_clear_credentials();
    if (error == ESP_OK) {
        error = network_time_end_maintenance_and_request_provisioning();
    }
    if (error != ESP_OK) {
        network_time_end_maintenance();
        if (credentials_are_missing()) {
            const esp_err_t request_error =
                network_time_request_provisioning();
            if (request_error == ESP_OK) {
                error = ESP_OK;
            }
        }
    }
    return error;
}

static esp_err_t send_html(httpd_req_t *request, const char *status, const char *html)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
                       "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'");
    return httpd_resp_send(request, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t setup_get_handler(httpd_req_t *request)
{
    return send_html(request, "200 OK", SETUP_PAGE);
}

static esp_err_t configure_post_handler(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > NETWORK_FORM_MAX_LENGTH) {
        return send_html(request, "400 Bad Request", SETUP_ERROR_PAGE);
    }

    char body[NETWORK_FORM_MAX_LENGTH + 1U];
    size_t received_total = 0U;
    unsigned timeout_count = 0U;
    while (received_total < (size_t)request->content_len) {
        const int received = httpd_req_recv(
            request, &body[received_total], (size_t)request->content_len - received_total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && timeout_count++ < 2U) {
            continue;
        }
        if (received <= 0) {
            return ESP_FAIL;
        }
        received_total += (size_t)received;
    }
    body[received_total] = '\0';

    network_credentials_t credentials = {0};
    const network_credentials_result_t result =
        network_credentials_parse_form(body, received_total, &credentials);
    memset(body, 0, sizeof(body));
    if (result != NETWORK_CREDENTIALS_OK) {
        ESP_LOGW(TAG, "rejected network configuration: %s",
                 network_credentials_result_name(result));
        memset(&credentials, 0, sizeof(credentials));
        return send_html(request, "400 Bad Request", SETUP_ERROR_PAGE);
    }

    const size_t ssid_length = strlen(credentials.ssid);
    const esp_err_t save_error = save_credentials(&credentials);
    memset(&credentials, 0, sizeof(credentials));
    if (save_error != ESP_OK) {
        ESP_LOGE(TAG, "failed to save network configuration: %s",
                 esp_err_to_name(save_error));
        return send_html(request, "500 Internal Server Error", SETUP_ERROR_PAGE);
    }

    ESP_LOGI(TAG, "network configuration saved (SSID length %u)", (unsigned)ssid_length);
    const esp_err_t response_error = send_html(request, "200 OK", SETUP_SUCCESS_PAGE);
    xEventGroupSetBits(s_events, NETWORK_EVENT_CREDENTIALS_SAVED);
    return response_error;
}

static esp_err_t not_found_handler(httpd_req_t *request, httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_send(request, "Open the setup page", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144U;
    config.max_uri_handlers = 4U;
    config.lru_purge_enable = true;

    esp_err_t error = httpd_start(&s_http_server, &config);
    if (error != ESP_OK) {
        return error;
    }

    const httpd_uri_t setup_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = setup_get_handler,
    };
    const httpd_uri_t configure_uri = {
        .uri = "/configure",
        .method = HTTP_POST,
        .handler = configure_post_handler,
    };
    error = httpd_register_uri_handler(s_http_server, &setup_uri);
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &configure_uri);
    }
    if (error == ESP_OK) {
        error = httpd_register_err_handler(s_http_server, HTTPD_404_NOT_FOUND,
                                           not_found_handler);
    }
    if (error != ESP_OK) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
    }
    return error;
}

static void stop_web_server(void)
{
    if (s_http_server != NULL) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
    }
}

static void configure_captive_portal_hint(void)
{
    esp_err_t error = esp_netif_dhcps_stop(s_ap_netif);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "could not pause DHCP server for captive portal hint: %s",
                 esp_err_to_name(error));
        return;
    }

    error = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                                   ESP_NETIF_CAPTIVEPORTAL_URI,
                                   (void *)SETUP_URL, strlen(SETUP_URL));
    const esp_err_t start_error = esp_netif_dhcps_start(s_ap_netif);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "could not set captive portal URI: %s", esp_err_to_name(error));
    }
    if (start_error != ESP_OK) {
        ESP_LOGW(TAG, "could not restart DHCP server: %s", esp_err_to_name(start_error));
    }
}

static esp_err_t stop_wifi(void)
{
    portENTER_CRITICAL(&s_status_lock);
    s_station_stop_requested = true;
    portEXIT_CRITICAL(&s_status_lock);

    esp_err_t error = esp_wifi_stop();
    const bool stopped = error == ESP_OK || error == ESP_ERR_WIFI_NOT_STARTED;
    portENTER_CRITICAL(&s_status_lock);
    s_station_stop_requested = false;
    if (stopped) {
        network_station_link_stop(&s_station_link);
        s_station_connect_deferred = false;
        s_radio_normalization_required = false;
    } else {
        s_radio_normalization_required = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (stopped && s_events != NULL) {
        xEventGroupClearBits(s_events, NETWORK_EVENT_CONNECTED |
                                          NETWORK_EVENT_FAILED |
                                          NETWORK_EVENT_LINK_RECOVERY);
    }
    return stopped ? ESP_OK : error;
}

static void generate_setup_password(char *password, size_t password_size)
{
    const uint64_t entropy = ((uint64_t)esp_random() << 32U) | esp_random();
    (void)network_setup_password_from_entropy(entropy, password, password_size);
}

static bool append_device_id_to_setup_ssid(void)
{
#ifdef CONFIG_RLCD_SETUP_AP_APPEND_DEVICE_ID
    return true;
#else
    return false;
#endif
}

static esp_err_t run_provisioning(TickType_t timeout, bool configured,
                                  esp_err_t reason)
{
    uint8_t mac[6] = {0};
    esp_err_t error = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (error != ESP_OK) {
        return error;
    }

    char setup_ssid[NETWORK_SSID_MAX_LENGTH + 1U] = {0};
    char setup_password[NETWORK_PASSWORD_MAX_LENGTH + 1U] = {0};
    char bootstrap_password[NETWORK_SETUP_PASSWORD_LENGTH + 1U] = {0};
    if (!network_setup_ssid_build(CONFIG_RLCD_SETUP_AP_SSID,
                                  append_device_id_to_setup_ssid(), mac,
                                  setup_ssid, sizeof(setup_ssid))) {
        ESP_LOGE(TAG, "invalid setup access point name in project configuration");
        return ESP_ERR_INVALID_ARG;
    }
    if (!network_setup_password_is_valid(CONFIG_RLCD_SETUP_AP_PASSWORD)) {
        ESP_LOGE(TAG, "invalid setup access point password in project configuration");
        return ESP_ERR_INVALID_ARG;
    }

    const bool use_random_password = CONFIG_RLCD_SETUP_AP_PASSWORD[0] == '\0';
    if (use_random_password) {
        generate_setup_password(bootstrap_password, sizeof(bootstrap_password));
    } else {
        const size_t configured_password_length =
            strlen(CONFIG_RLCD_SETUP_AP_PASSWORD);
        memcpy(setup_password, CONFIG_RLCD_SETUP_AP_PASSWORD,
               configured_password_length + 1U);
    }

    wifi_config_t wifi_config = {0};
    const size_t ssid_length = strlen(setup_ssid);
    const char *initial_password =
        use_random_password ? bootstrap_password : setup_password;
    const size_t password_length = strlen(initial_password);
    memcpy(wifi_config.ap.ssid, setup_ssid, ssid_length);
    memcpy(wifi_config.ap.password, initial_password, password_length);
    wifi_config.ap.ssid_len = (uint8_t)ssid_length;
    wifi_config.ap.channel = 1U;
    wifi_config.ap.max_connection = 2U;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.capable = true;
    wifi_config.ap.pmf_cfg.required = false;

    xEventGroupClearBits(s_events, NETWORK_EVENT_CREDENTIALS_SAVED);
    error = esp_wifi_set_mode(WIFI_MODE_AP);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    }
    if (error == ESP_OK) {
        error = esp_wifi_start();
    }
    if (error != ESP_OK) {
        (void)stop_wifi();
        memset(&wifi_config, 0, sizeof(wifi_config));
        memset(bootstrap_password, 0, sizeof(bootstrap_password));
        memset(setup_password, 0, sizeof(setup_password));
        return error;
    }

    if (use_random_password) {
        /* esp_random() is true-random while the Wi-Fi RF subsystem is active. */
        generate_setup_password(setup_password, sizeof(setup_password));
        error = stop_wifi();
        if (error == ESP_OK) {
            memset(wifi_config.ap.password, 0, sizeof(wifi_config.ap.password));
            memcpy(wifi_config.ap.password, setup_password, strlen(setup_password));
            error = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        }
        if (error == ESP_OK) {
            error = esp_wifi_start();
        }
    }
    memset(&wifi_config, 0, sizeof(wifi_config));
    memset(bootstrap_password, 0, sizeof(bootstrap_password));
    if (error != ESP_OK) {
        (void)stop_wifi();
        memset(setup_password, 0, sizeof(setup_password));
        return error;
    }

    configure_captive_portal_hint();
    error = start_web_server();
    if (error != ESP_OK) {
        (void)stop_wifi();
        memset(setup_password, 0, sizeof(setup_password));
        return error;
    }

    set_provisioning_status(configured, reason, setup_ssid, setup_password);
    ESP_LOGI(TAG, "setup access point ready: %s at %s (%s password)",
             setup_ssid, SETUP_URL, use_random_password ? "random" : "configured");

    const EventBits_t bits = xEventGroupWaitBits(
        s_events, NETWORK_EVENT_CREDENTIALS_SAVED, pdTRUE, pdFALSE, timeout);
    stop_web_server();
    const esp_err_t stop_error = stop_wifi();
    memset(setup_password, 0, sizeof(setup_password));
    if (stop_error != ESP_OK) {
        return stop_error;
    }
    return (bits & NETWORK_EVENT_CREDENTIALS_SAVED) != 0U ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        bool station_active;
        bool connect_deferred;
        bool stop_requested;
        portENTER_CRITICAL(&s_status_lock);
        station_active = network_station_link_is_active(&s_station_link);
        connect_deferred = s_station_connect_deferred;
        stop_requested = s_station_stop_requested;
        portEXIT_CRITICAL(&s_status_lock);
        if (!station_active || connect_deferred || stop_requested) {
            return;
        }
        const esp_err_t error = esp_wifi_connect();
        if (error != ESP_OK) {
            xEventGroupSetBits(s_events, NETWORK_EVENT_FAILED |
                                             NETWORK_EVENT_LINK_RECOVERY);
        }
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        bool station_active;
        bool stop_requested;
        portENTER_CRITICAL(&s_status_lock);
        station_active = network_station_link_lost_ipv4(&s_station_link);
        stop_requested = s_station_stop_requested;
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupClearBits(s_events, NETWORK_EVENT_CONNECTED);
        if (!station_active || stop_requested) {
            return;
        }
        const wifi_event_sta_disconnected_t *event = event_data;
        if (s_station_retries < NETWORK_STATION_MAX_RETRIES) {
            ++s_station_retries;
            ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u), retry %u/%u",
                     event != NULL ? event->reason : 0U, (unsigned)s_station_retries,
                     NETWORK_STATION_MAX_RETRIES);
            if (esp_wifi_connect() != ESP_OK) {
                xEventGroupSetBits(s_events, NETWORK_EVENT_FAILED |
                                                 NETWORK_EVENT_LINK_RECOVERY);
            }
        } else {
            xEventGroupSetBits(s_events, NETWORK_EVENT_FAILED |
                                             NETWORK_EVENT_LINK_RECOVERY);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        portENTER_CRITICAL(&s_status_lock);
        network_station_link_stop(&s_station_link);
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupClearBits(s_events, NETWORK_EVENT_CONNECTED);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        bool accepted;
        portENTER_CRITICAL(&s_status_lock);
        accepted = network_station_link_got_ipv4(&s_station_link);
        portEXIT_CRITICAL(&s_status_lock);
        if (!accepted) {
            return;
        }
        const ip_event_got_ip_t *event = event_data;
        s_station_retries = 0U;
        if (event != NULL) {
            ESP_LOGI(TAG, "Wi-Fi connected with address " IPSTR,
                     IP2STR(&event->ip_info.ip));
        }
        xEventGroupClearBits(s_events, NETWORK_EVENT_FAILED |
                                          NETWORK_EVENT_LINK_RECOVERY);
        xEventGroupSetBits(s_events, NETWORK_EVENT_CONNECTED);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        bool station_active;
        portENTER_CRITICAL(&s_status_lock);
        station_active = network_station_link_lost_ipv4(&s_station_link);
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupClearBits(s_events, NETWORK_EVENT_CONNECTED);
        if (station_active) {
            /* A lease can be renewed without a full STA disconnect. Wake the
             * owner and allow its normal connection timeout to provide a
             * short recovery window before declaring Wi-Fi failed. */
            xEventGroupSetBits(s_events, NETWORK_EVENT_LINK_RECOVERY);
        }
    }
}

static esp_err_t synchronize_system_time(network_time_datetime_t *datetime)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org"));
    esp_err_t error = esp_netif_sntp_init(&config);
    if (error != ESP_OK) {
        return error;
    }

    for (uint32_t attempt = 0U; attempt < NETWORK_SNTP_ATTEMPTS; ++attempt) {
        error = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(NETWORK_SNTP_WAIT_MS));
        if (error == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "waiting for SNTP response (%u/%u)", (unsigned)(attempt + 1U),
                 NETWORK_SNTP_ATTEMPTS);
    }
    esp_netif_sntp_deinit();
    if (error != ESP_OK) {
        return error;
    }

    const time_t now = time(NULL);
    struct tm local_time = {0};
    if (now == (time_t)-1 || localtime_r(&now, &local_time) == NULL ||
        local_time.tm_year < 100 || local_time.tm_year > 199) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *datetime = (network_time_datetime_t){
        .year = (uint16_t)(local_time.tm_year + 1900),
        .month = (uint8_t)(local_time.tm_mon + 1),
        .day = (uint8_t)local_time.tm_mday,
        .weekday = (uint8_t)local_time.tm_wday,
        .hour = (uint8_t)local_time.tm_hour,
        .minute = (uint8_t)local_time.tm_min,
        .second = (uint8_t)local_time.tm_sec,
    };
    return ESP_OK;
}

static esp_err_t start_station(const network_credentials_t *credentials,
                               uint32_t timeout_ms,
                               network_time_failure_t *failure)
{
    if (credentials == NULL || timeout_ms == 0U ||
        !network_credentials_are_valid(credentials)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (failure != NULL) {
        *failure = NETWORK_TIME_FAILURE_SERVICE;
    }

    wifi_config_t wifi_config = {0};
    const size_t ssid_length = strlen(credentials->ssid);
    const size_t password_length = strlen(credentials->password);
    memcpy(wifi_config.sta.ssid, credentials->ssid, ssid_length);
    memcpy(wifi_config.sta.password, credentials->password, password_length);
    wifi_config.sta.threshold.authmode =
        password_length == 0U ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    s_station_retries = 0U;
    portENTER_CRITICAL(&s_status_lock);
    s_station_connect_deferred = false;
    network_station_link_begin(&s_station_link);
    portEXIT_CRITICAL(&s_status_lock);
    xEventGroupClearBits(s_events, NETWORK_EVENT_CONNECTED |
                                      NETWORK_EVENT_FAILED |
                                      NETWORK_EVENT_LINK_RECOVERY);

    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (error == ESP_OK) {
        error = esp_wifi_start();
    }
    if (error != ESP_OK) {
        (void)stop_wifi();
        memset(&wifi_config, 0, sizeof(wifi_config));
        return error;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_events, NETWORK_EVENT_CONNECTED | NETWORK_EVENT_FAILED,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if ((bits & NETWORK_EVENT_CONNECTED) == 0U) {
        error = ESP_ERR_TIMEOUT;
        if (failure != NULL) {
            *failure = NETWORK_TIME_FAILURE_WIFI;
        }
    }

    if (error != ESP_OK) {
        (void)stop_wifi();
    } else if (failure != NULL) {
        *failure = NETWORK_TIME_FAILURE_NONE;
    }
    memset(&wifi_config, 0, sizeof(wifi_config));
    return error;
}

static esp_err_t ensure_station_connected(
    const network_credentials_t *credentials, uint32_t timeout_ms,
    network_time_failure_t *failure)
{
    if (credentials == NULL || timeout_ms == 0U ||
        !network_credentials_are_valid(credentials)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (failure != NULL) {
        *failure = NETWORK_TIME_FAILURE_SERVICE;
    }

    bool active = false;
    bool connected = false;
    if (station_normalization_required()) {
        const esp_err_t stop_error = stop_wifi();
        if (stop_error != ESP_OK) {
            return stop_error;
        }
    }
    get_station_link_state(&active, &connected);
    if (connected) {
        if (failure != NULL) {
            *failure = NETWORK_TIME_FAILURE_NONE;
        }
        return ESP_OK;
    }
    if (!active) {
        return start_station(credentials, timeout_ms, failure);
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_events, NETWORK_EVENT_CONNECTED | NETWORK_EVENT_FAILED,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    get_station_link_state(NULL, &connected);
    if ((bits & NETWORK_EVENT_CONNECTED) != 0U && connected) {
        if (failure != NULL) {
            *failure = NETWORK_TIME_FAILURE_NONE;
        }
        return ESP_OK;
    }

    if (failure != NULL) {
        *failure = NETWORK_TIME_FAILURE_WIFI;
    }
    (void)stop_wifi();
    return ESP_ERR_TIMEOUT;
}

static esp_err_t restore_settings_access_point(void)
{
    portENTER_CRITICAL(&s_status_lock);
    network_station_link_stop(&s_station_link);
    s_station_connect_deferred = false;
    portEXIT_CRITICAL(&s_status_lock);
    const esp_err_t disconnect_error = esp_wifi_disconnect();
    xEventGroupClearBits(s_events,
                         NETWORK_EVENT_CONNECTED | NETWORK_EVENT_FAILED |
                             NETWORK_EVENT_LINK_RECOVERY);
    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_AP);
    if (error == ESP_OK) {
        if (disconnect_error != ESP_OK &&
            disconnect_error != ESP_ERR_WIFI_NOT_CONNECT) {
            ESP_LOGW(TAG, "candidate STA disconnect reported: %s",
                     esp_err_to_name(disconnect_error));
        }
        return ESP_OK;
    }

    /* A failed dynamic mode change must not leave a page that claims it can
     * accept another attempt. Rebuild the already-configured settings AP. */
    ESP_LOGW(TAG, "dynamic settings AP restore failed, rebuilding: %s",
             esp_err_to_name(error));
    const esp_err_t stop_error = esp_wifi_stop();
    if (stop_error != ESP_OK && stop_error != ESP_ERR_WIFI_NOT_STARTED) {
        return stop_error;
    }
    error = esp_wifi_set_mode(WIFI_MODE_AP);
    if (error == ESP_OK) {
        error = esp_wifi_start();
    }
    return error;
}

esp_err_t network_time_validate_and_save_credentials(
    const network_credentials_t *credentials, uint32_t timeout_ms,
    bool *portal_available)
{
    if (portal_available == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *portal_available = true;
    if (!s_initialized || !s_storage_ready || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms == 0U ||
        !network_credentials_are_valid(credentials)) {
        return ESP_ERR_INVALID_ARG;
    }

    network_credentials_t previous = {0};
    const esp_err_t previous_error = load_credentials(&previous);
    const bool previous_available = previous_error == ESP_OK;
    if (!previous_available && previous_error != ESP_ERR_NVS_NOT_FOUND) {
        memset(&previous, 0, sizeof(previous));
        return previous_error;
    }

    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    if (s_session_policy.owner == NETWORK_SESSION_OWNER_MAINTENANCE &&
        !network_station_link_is_active(&s_station_link) &&
        s_http_server == NULL) {
        s_station_connect_deferred = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) {
        memset(&previous, 0, sizeof(previous));
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_config = {0};
    const size_t ssid_length = strlen(credentials->ssid);
    const size_t password_length = strlen(credentials->password);
    memcpy(wifi_config.sta.ssid, credentials->ssid, ssid_length);
    memcpy(wifi_config.sta.password, credentials->password, password_length);
    wifi_config.sta.threshold.authmode =
        password_length == 0U ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    xEventGroupClearBits(s_events,
                         NETWORK_EVENT_CONNECTED | NETWORK_EVENT_FAILED |
                             NETWORK_EVENT_LINK_RECOVERY);
    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (error == ESP_OK) {
        s_station_retries = 0U;
        portENTER_CRITICAL(&s_status_lock);
        network_station_link_begin(&s_station_link);
        portEXIT_CRITICAL(&s_status_lock);
        error = esp_wifi_connect();
    }
    memset(&wifi_config, 0, sizeof(wifi_config));

    if (error == ESP_OK) {
        const EventBits_t bits = xEventGroupWaitBits(
            s_events, NETWORK_EVENT_CONNECTED | NETWORK_EVENT_FAILED,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
        if ((bits & NETWORK_EVENT_CONNECTED) == 0U) {
            error = ESP_ERR_TIMEOUT;
        }
    }

    if (error == ESP_OK) {
        error = save_credentials(credentials);
        if (error != ESP_OK) {
            const esp_err_t rollback_error =
                previous_available
                    ? save_credentials(&previous)
                    : erase_credentials();
            if (rollback_error != ESP_OK) {
                ESP_LOGE(TAG,
                         "could not restore credentials after save failure: %s",
                         esp_err_to_name(rollback_error));
                error = rollback_error;
            }
        }
    }
    memset(&previous, 0, sizeof(previous));
    if (error != ESP_OK) {
        const esp_err_t restore_error = restore_settings_access_point();
        if (restore_error != ESP_OK) {
            *portal_available = false;
            ESP_LOGE(TAG, "could not restore settings access point: %s",
                     esp_err_to_name(restore_error));
            error = restore_error;
        }
        ESP_LOGW(TAG,
                 "candidate Wi-Fi was not saved (SSID length %u): %s",
                 (unsigned)ssid_length, esp_err_to_name(error));
        return error;
    }

    /* Keep APSTA alive just long enough for the settings server to return its
     * success response. The portal task stops Wi-Fi before ownership is
     * handed back to the normal network task. */
    ESP_LOGI(TAG, "candidate Wi-Fi validated and saved (SSID length %u)",
             (unsigned)ssid_length);
    return ESP_OK;
}

static esp_err_t connect_and_synchronize(
    const network_credentials_t *credentials,
    network_time_failure_t *failure)
{
    set_status(NETWORK_TIME_STATE_CONNECTING, true, ESP_OK);
    esp_err_t error = ensure_station_connected(
        credentials, NETWORK_STATION_TIMEOUT_MS, failure);
    if (error == ESP_OK) {
        set_status(NETWORK_TIME_STATE_SYNCHRONIZING, true, ESP_OK);
        network_time_datetime_t datetime = {0};
        error = synchronize_system_time(&datetime);
        if (error != ESP_OK && failure != NULL) {
            *failure = NETWORK_TIME_FAILURE_NTP;
        }
        if (error == ESP_OK) {
            if (failure != NULL) {
                *failure = NETWORK_TIME_FAILURE_NONE;
            }
            xQueueOverwrite(s_datetime_queue, &datetime);
            ESP_LOGI(TAG, "SNTP local time %04u-%02u-%02u %02u:%02u:%02u",
                     datetime.year, datetime.month, datetime.day, datetime.hour,
                     datetime.minute, datetime.second);
        }
    }

    const network_connection_action_t action =
        network_connection_policy_on_operation_end(
            current_connection_mode(), NETWORK_CONNECTION_OPERATION_SYNC);
    if (action == NETWORK_CONNECTION_ACTION_STOP) {
        const esp_err_t stop_error = stop_wifi();
        if (error == ESP_OK && stop_error != ESP_OK) {
            error = stop_error;
            if (failure != NULL) {
                *failure = NETWORK_TIME_FAILURE_SERVICE;
            }
        }
    }
    return error;
}

static esp_err_t connect_acquired_online_session(uint32_t timeout_ms)
{
    network_credentials_t credentials = {0};
    esp_err_t error = load_credentials(&credentials);
    if (error == ESP_OK) {
        network_time_failure_t failure = NETWORK_TIME_FAILURE_NONE;
        error = ensure_station_connected(&credentials, timeout_ms, &failure);
    }
    memset(&credentials, 0, sizeof(credentials));

    if (error != ESP_OK) {
        const esp_err_t stop_error = stop_wifi();
        (void)online_session_release();
        if (get_automatic_sync_enabled()) {
            xEventGroupSetBits(s_events, NETWORK_EVENT_LINK_RECOVERY);
        } else if (stop_error != ESP_OK) {
            xEventGroupSetBits(s_events, NETWORK_EVENT_POLICY_CHANGED);
        }
        ESP_LOGW(TAG, "online network session could not start: %s",
                 esp_err_to_name(error));
        return error;
    }

    ESP_LOGI(TAG, "online network session acquired");
    return ESP_OK;
}

esp_err_t network_time_begin_online_session(uint32_t timeout_ms)
{
    if (timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !s_storage_ready || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!online_session_acquire()) {
        return ESP_ERR_INVALID_STATE;
    }
    return connect_acquired_online_session(timeout_ms);
}

esp_err_t network_time_begin_online_session_from_maintenance(
    uint32_t timeout_ms)
{
    if (timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !s_storage_ready || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!maintenance_session_owned()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t stop_error = stop_wifi();
    if (stop_error != ESP_OK) {
        return stop_error;
    }
    if (!maintenance_session_transfer_to_online()) {
        return ESP_ERR_INVALID_STATE;
    }
    return connect_acquired_online_session(timeout_ms);
}

esp_err_t network_time_end_online_session(void)
{
    if (!s_initialized || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    network_session_owner_t owner;
    portENTER_CRITICAL(&s_status_lock);
    owner = s_session_policy.owner;
    portEXIT_CRITICAL(&s_status_lock);
    if (owner == NETWORK_SESSION_OWNER_NONE) {
        return ESP_OK;
    }
    if (owner != NETWORK_SESSION_OWNER_ONLINE) {
        return ESP_ERR_INVALID_STATE;
    }

    const network_connection_action_t action =
        network_connection_policy_on_operation_end(
            current_connection_mode(), NETWORK_CONNECTION_OPERATION_ONLINE);
    esp_err_t stop_error = ESP_OK;
    if (action == NETWORK_CONNECTION_ACTION_STOP) {
        stop_error = stop_wifi();
    }
    bool connected = false;
    get_station_link_state(NULL, &connected);
    const bool released = online_session_release();
    if (!released) {
        return ESP_ERR_INVALID_STATE;
    }
    if (action == NETWORK_CONNECTION_ACTION_KEEP && !connected) {
        xEventGroupSetBits(s_events, NETWORK_EVENT_LINK_RECOVERY);
    }
    if (stop_error != ESP_OK) {
        xEventGroupSetBits(s_events, NETWORK_EVENT_POLICY_CHANGED);
        ESP_LOGW(TAG, "online network session Wi-Fi shutdown failed: %s",
                 esp_err_to_name(stop_error));
        return stop_error;
    }

    ESP_LOGI(TAG, "online network session released; station %s",
             action == NETWORK_CONNECTION_ACTION_KEEP ? "kept" : "stopped");
    return ESP_OK;
}

static EventBits_t wait_for_network_task_event(TickType_t timeout)
{
    while (true) {
        const EventBits_t bits = xEventGroupWaitBits(
            s_events, NETWORK_EVENT_TASK_WAKE,
            pdTRUE, pdFALSE, timeout);
        const EventBits_t actionable =
            bits & (NETWORK_EVENT_SYNC_REQUEST |
                    NETWORK_EVENT_POLICY_CHANGED |
                    NETWORK_EVENT_LINK_RECOVERY |
                    NETWORK_EVENT_FORCE_PROVISIONING);
        if (actionable != 0U) {
            return actionable;
        }
        if ((bits & NETWORK_EVENT_MAINTENANCE_CHANGED) == 0U) {
            return 0U;
        }
        while (exclusive_session_active()) {
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
    }
}

static void wait_for_maintenance_end(void)
{
    while (exclusive_session_active()) {
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
    xEventGroupClearBits(s_events, NETWORK_EVENT_MAINTENANCE_CHANGED);
}

static void network_task(void *argument)
{
    (void)argument;
    network_credentials_t credentials = {0};
    bool setup_window_completed = false;
    uint32_t consecutive_failures = 0U;
    EventBits_t wake_bits = 0U;

    while (true) {
        wait_for_maintenance_end();
        if (!network_task_activity_begin()) {
            continue;
        }
        wake_bits |= wait_for_network_task_event(0U);
        const bool force_provisioning =
            (wake_bits & NETWORK_EVENT_FORCE_PROVISIONING) != 0U;
        wake_bits &= ~NETWORK_EVENT_FORCE_PROVISIONING;
        esp_err_t error = load_credentials(&credentials);
        if (error != ESP_OK) {
            if (!network_connection_policy_should_provision(
                    s_startup_provisioning_enabled,
                    setup_window_completed, force_provisioning)) {
                set_status(NETWORK_TIME_STATE_RETRY_WAIT, false,
                           error == ESP_ERR_NVS_NOT_FOUND
                               ? (s_startup_provisioning_enabled
                                      ? ESP_ERR_TIMEOUT : ESP_OK)
                               : error);
                network_task_activity_end();
                wake_bits = wait_for_network_task_event(portMAX_DELAY);
                continue;
            }
            if (error != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW(TAG, "stored network configuration is unusable: %s",
                         esp_err_to_name(error));
                (void)network_time_clear_credentials();
            }
            (void)stop_wifi();
            error = run_provisioning(
                pdMS_TO_TICKS(NETWORK_SETUP_WINDOW_MS), false, ESP_OK);
            if (error == ESP_ERR_TIMEOUT) {
                setup_window_completed = true;
                set_status(NETWORK_TIME_STATE_RETRY_WAIT, false, error);
                network_task_activity_end();
                ESP_LOGI(TAG,
                         "setup window closed; local features remain available offline");
                wake_bits = wait_for_network_task_event(portMAX_DELAY);
                continue;
            }
            if (error != ESP_OK) {
                set_status(NETWORK_TIME_STATE_ERROR, false, error);
                network_task_activity_end();
                ESP_LOGE(TAG, "network setup failed: %s", esp_err_to_name(error));
                vTaskDelay(pdMS_TO_TICKS(60000U));
            } else {
                network_task_activity_end();
            }
            setup_window_completed = false;
            consecutive_failures = 0U;
            wake_bits = error == ESP_OK ? NETWORK_EVENT_SYNC_REQUEST : 0U;
            continue;
        }

        setup_window_completed = false;
        wake_bits |= wait_for_network_task_event(0U);
        const bool user_requested_sync =
            (wake_bits & NETWORK_EVENT_SYNC_REQUEST) != 0U;
        wake_bits = 0U;
        const bool automatic_sync = get_automatic_sync_enabled();
        if (!automatic_sync && !user_requested_sync) {
            const esp_err_t stop_error = stop_wifi();
            set_status(NETWORK_TIME_STATE_RETRY_WAIT, true, ESP_OK);
            network_task_activity_end();
            if (stop_error != ESP_OK) {
                ESP_LOGW(TAG,
                         "could not stop station after entering saving mode: %s",
                         esp_err_to_name(stop_error));
            }
            wake_bits = wait_for_network_task_event(
                stop_error == ESP_OK
                    ? portMAX_DELAY
                    : pdMS_TO_TICKS(NETWORK_STATION_STOP_RETRY_MS));
            continue;
        }
        network_time_failure_t failure = NETWORK_TIME_FAILURE_NONE;
        error = connect_and_synchronize(&credentials, &failure);
        memset(&credentials, 0, sizeof(credentials));
        if (error == ESP_OK) {
            consecutive_failures = 0U;
            set_status(NETWORK_TIME_STATE_SYNCHRONIZED, true, ESP_OK);
            network_task_activity_end();
            wake_bits = wait_for_network_task_event(
                get_automatic_sync_enabled()
                    ? pdMS_TO_TICKS(NETWORK_RESYNC_INTERVAL_MS)
                    : portMAX_DELAY);
            continue;
        }

        const esp_err_t synchronization_error = error;
        ESP_LOGW(TAG, "%s time synchronization failed: %s",
                 user_requested_sync ? "manual" : "automatic",
                 esp_err_to_name(synchronization_error));
        set_failure_status(NETWORK_TIME_STATE_RETRY_WAIT, true,
                           synchronization_error, failure);
        if (consecutive_failures < UINT32_MAX) {
            ++consecutive_failures;
        }
        const uint32_t retry_delay_ms =
            network_retry_delay_ms(consecutive_failures);
        const bool retry_automatically = get_automatic_sync_enabled();
        if (retry_automatically) {
            ESP_LOGW(TAG, "%s unavailable; retrying in %u seconds",
                     network_time_failure_name(failure),
                     (unsigned)(retry_delay_ms / 1000U));
        } else {
            ESP_LOGW(TAG,
                     "%s unavailable; automatic retry disabled by power mode",
                     network_time_failure_name(failure));
        }
        if (user_requested_sync) {
            ESP_LOGW(TAG, "manual time synchronization did not complete");
        }
        network_task_activity_end();
        bool station_active = false;
        get_station_link_state(&station_active, NULL);
        wake_bits = wait_for_network_task_event(
            retry_automatically
                ? pdMS_TO_TICKS(retry_delay_ms)
                : (station_active
                       ? pdMS_TO_TICKS(NETWORK_STATION_STOP_RETRY_MS)
                       : portMAX_DELAY));
    }
}

esp_err_t network_time_init(bool automatic_sync_enabled,
                            bool startup_provisioning_enabled)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_automatic_sync_enabled = automatic_sync_enabled;
    s_startup_provisioning_enabled = startup_provisioning_enabled;
    set_status(NETWORK_TIME_STATE_STARTING, false, ESP_OK);

    esp_err_t error = app_storage_init();
    if (error != ESP_OK) {
        set_status(NETWORK_TIME_STATE_ERROR, false, error);
        return error;
    }
    s_storage_ready = true;
    if (getenv("TZ") == NULL && setenv("TZ", "CST-8", 1) != 0) {
        set_status(NETWORK_TIME_STATE_ERROR, false, ESP_FAIL);
        return ESP_FAIL;
    }
    tzset();

    s_events = xEventGroupCreate();
    s_datetime_queue = xQueueCreate(1U, sizeof(network_time_datetime_t));
    if (s_events == NULL || s_datetime_queue == NULL) {
        set_status(NETWORK_TIME_STATE_ERROR, false, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    error = esp_netif_init();
    if (error != ESP_OK) {
        set_status(NETWORK_TIME_STATE_ERROR, false, error);
        return error;
    }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        set_status(NETWORK_TIME_STATE_ERROR, false, error);
        return error;
    }

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_station_netif = esp_netif_create_default_wifi_sta();
    if (s_ap_netif == NULL || s_station_netif == NULL) {
        set_status(NETWORK_TIME_STATE_ERROR, false, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    (void)esp_netif_set_hostname(s_station_netif, "esp32-rlcd");

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&wifi_init);
    if (error == ESP_OK) {
        error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }
    if (error == ESP_OK) {
        /* NORMAL keeps the STA associated while the radio sleeps between
         * DTIM beacons. Make the ESP-IDF default explicit so later SDK
         * changes cannot silently turn a persistent link into full-power
         * operation. */
        error = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }
    if (error == ESP_OK) {
        error = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    }
    if (error == ESP_OK) {
        error = esp_event_handler_instance_register(
            IP_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    }
    if (error != ESP_OK) {
        set_status(NETWORK_TIME_STATE_ERROR, false, error);
        return error;
    }

    s_initialized = true;
    if (xTaskCreate(network_task, "network_time", 8192U, NULL, 5U, NULL) != pdPASS) {
        s_initialized = false;
        set_status(NETWORK_TIME_STATE_ERROR, false, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "network service ready; mode=%s; station power save=min-modem; credentials are never written to logs",
             get_automatic_sync_enabled() ? "normal" : "saving");
    return ESP_OK;
}

bool network_time_take_datetime(network_time_datetime_t *datetime)
{
    if (datetime == NULL || s_datetime_queue == NULL) {
        return false;
    }
    return xQueueReceive(s_datetime_queue, datetime, 0U) == pdTRUE;
}
