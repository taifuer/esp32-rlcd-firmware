#include "firmware_update.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "network_credentials.h"
#include "network_time.h"

#define UPDATE_EVENT_UPLOAD_STARTED BIT0
#define UPDATE_EVENT_COMPLETE BIT1
#define UPDATE_EVENT_FAILED BIT2
#define UPDATE_EVENT_CANCEL BIT3
#define UPDATE_EVENT_ALL                                                       \
    (UPDATE_EVENT_UPLOAD_STARTED | UPDATE_EVENT_COMPLETE |                    \
     UPDATE_EVENT_FAILED | UPDATE_EVENT_CANCEL)

#define UPDATE_WINDOW_MS 300000U
#define UPDATE_RESTART_DELAY_MS 1800U
#define UPDATE_SERVER_STOP_DELAY_MS 250U
#define UPDATE_HTTP_BUFFER_SIZE 4096U

static const char *TAG = "firmware_update";
static const char UPDATE_AP_BASE_NAME[] = "ESP32-RLCD-UPDATE";
static const char UPDATE_URL[] = "http://192.168.4.1";
static const char EXPECTED_PROJECT_NAME[] = "rlcd_firmware";

static const char UPDATE_PAGE[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ESP32 RLCD 固件升级</title><style>"
    "body{font:16px system-ui,sans-serif;max-width:30rem;margin:3rem auto;padding:0 1rem;color:#171717}"
    "h1{font-size:1.6rem}input,button{box-sizing:border-box;width:100%;font:inherit;padding:.75rem;"
    "border:1px solid #888;border-radius:.45rem;margin-top:1rem}button{background:#171717;color:#fff;"
    "border:0}progress{width:100%;height:1.25rem;margin-top:1.25rem}small{color:#555}</style></head>"
    "<body><h1>ESP32 RLCD 固件升级</h1>"
    "<p>请选择当前项目发布的 <strong>OTA 固件</strong>。升级不会清除 Wi-Fi 配置。</p>"
    "<input id=\"file\" type=\"file\" accept=\".bin,application/octet-stream\">"
    "<button id=\"upload\" type=\"button\">开始升级</button>"
    "<progress id=\"progress\" max=\"100\" value=\"0\"></progress>"
    "<p id=\"status\">等待选择固件</p>"
    "<small>写入期间请保持设备供电，不要关闭页面。成功后设备会自动重启。</small>"
    "<script>const f=document.getElementById('file'),b=document.getElementById('upload'),"
    "p=document.getElementById('progress'),s=document.getElementById('status');"
    "b.onclick=()=>{if(!f.files.length){s.textContent='请先选择 OTA 固件';return;}"
    "b.disabled=true;f.disabled=true;const x=new XMLHttpRequest();x.open('POST','/update');"
    "x.setRequestHeader('Content-Type','application/octet-stream');"
    "x.upload.onprogress=e=>{if(e.lengthComputable){const n=Math.round(e.loaded*100/e.total);"
    "p.value=n;s.textContent='正在上传 '+n+'%';}};"
    "x.onload=()=>{s.textContent=x.status===200?'升级成功，设备即将重启':x.responseText||'升级失败';"
    "if(x.status!==200){b.disabled=false;f.disabled=false;}};"
    "x.onerror=()=>{s.textContent='连接中断，请查看设备屏幕';};x.send(f.files[0]);};</script>"
    "</body></html>";

static const char UPDATE_SUCCESS_PAGE[] =
    "升级成功。固件已校验，设备即将自动重启。";
static const char UPDATE_ERROR_PAGE[] =
    "升级失败。请确认选择的是本项目的 OTA 固件，然后重新开启升级模式。";

static EventGroupHandle_t s_events;
static httpd_handle_t s_http_server;
static bool s_initialized;
static bool s_upload_started;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static firmware_update_status_t s_status = {
    .state = FIRMWARE_UPDATE_STATE_IDLE,
    .last_error = ESP_OK,
};

static void reset_status_locked(firmware_update_state_t state,
                                esp_err_t error)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = state;
    s_status.last_error = error;
}

static void set_state(firmware_update_state_t state, esp_err_t error)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.state = state;
    s_status.last_error = error;
    portEXIT_CRITICAL(&s_status_lock);
}

static firmware_update_state_t get_state(void)
{
    firmware_update_state_t state;
    portENTER_CRITICAL(&s_status_lock);
    state = s_status.state;
    portEXIT_CRITICAL(&s_status_lock);
    return state;
}

static void set_access_details(const char *ssid, const char *password)
{
    char ssid_value[FIRMWARE_UPDATE_SSID_CAPACITY] = {0};
    char password_value[FIRMWARE_UPDATE_PASSWORD_CAPACITY] = {0};
    char url_value[FIRMWARE_UPDATE_URL_CAPACITY] = {0};
    snprintf(ssid_value, sizeof(ssid_value), "%s", ssid);
    snprintf(password_value, sizeof(password_value), "%s", password);
    snprintf(url_value, sizeof(url_value), "%s", UPDATE_URL);

    portENTER_CRITICAL(&s_status_lock);
    memcpy(s_status.access_point_ssid, ssid_value, sizeof(ssid_value));
    memcpy(s_status.access_point_password, password_value,
           sizeof(password_value));
    memcpy(s_status.access_url, url_value, sizeof(url_value));
    s_status.state = FIRMWARE_UPDATE_STATE_READY;
    s_status.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_status_lock);
}

static void set_incoming_version(const char *version)
{
    char version_value[FIRMWARE_UPDATE_VERSION_CAPACITY] = {0};
    snprintf(version_value, sizeof(version_value), "%s", version);
    portENTER_CRITICAL(&s_status_lock);
    memcpy(s_status.incoming_version, version_value, sizeof(version_value));
    portEXIT_CRITICAL(&s_status_lock);
}

static void set_progress(size_t received, size_t total)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.state = FIRMWARE_UPDATE_STATE_RECEIVING;
    s_status.received_bytes = received;
    s_status.total_bytes = total;
    s_status.percent = firmware_update_progress_percent(received, total);
    portEXIT_CRITICAL(&s_status_lock);
}

esp_err_t firmware_update_get_status(firmware_update_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return ESP_OK;
}

static esp_err_t send_page(httpd_req_t *request, const char *status,
                           const char *type, const char *body)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, type);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(
        request, "Content-Security-Policy",
        "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'");
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t update_get_handler(httpd_req_t *request)
{
    return send_page(request, "200 OK", "text/html; charset=utf-8",
                     UPDATE_PAGE);
}

static esp_err_t receive_exact(httpd_req_t *request, uint8_t *buffer,
                               size_t requested, size_t *received_total)
{
    unsigned timeout_count = 0U;
    while (*received_total < requested) {
        const int received = httpd_req_recv(
            request, (char *)&buffer[*received_total],
            requested - *received_total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && timeout_count++ < 3U) {
            continue;
        }
        if (received <= 0) {
            return ESP_FAIL;
        }
        *received_total += (size_t)received;
    }
    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *request)
{
    const size_t total = request->content_len > 0
                             ? (size_t)request->content_len
                             : 0U;
    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    if (!s_upload_started &&
        s_status.state == FIRMWARE_UPDATE_STATE_READY) {
        s_upload_started = true;
        s_status.state = FIRMWARE_UPDATE_STATE_RECEIVING;
        s_status.received_bytes = 0U;
        s_status.total_bytes = total;
        s_status.percent = 0U;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) {
        return send_page(request, "409 Conflict", "text/plain; charset=utf-8",
                         "升级会话不可用或已有上传正在进行。\n");
    }
    xEventGroupSetBits(s_events, UPDATE_EVENT_UPLOAD_STARTED);

    const esp_partition_t *partition =
        esp_ota_get_next_update_partition(NULL);
    const size_t header_size = sizeof(esp_image_header_t) +
                               sizeof(esp_image_segment_header_t) +
                               sizeof(esp_app_desc_t);
    esp_err_t error = ESP_OK;
    esp_ota_handle_t update_handle = 0;
    bool update_handle_open = false;
    uint8_t *buffer = NULL;

    if (partition == NULL || total < header_size || total > partition->size) {
        error = ESP_ERR_INVALID_SIZE;
        goto failed;
    }

    buffer = malloc(UPDATE_HTTP_BUFFER_SIZE);
    if (buffer == NULL) {
        error = ESP_ERR_NO_MEM;
        goto failed;
    }

    size_t buffered = 0U;
    error = receive_exact(request, buffer, header_size, &buffered);
    if (error != ESP_OK) {
        goto failed;
    }

    const esp_image_header_t *image_header =
        (const esp_image_header_t *)buffer;
    esp_app_desc_t incoming = {0};
    memcpy(&incoming,
           &buffer[sizeof(esp_image_header_t) +
                   sizeof(esp_image_segment_header_t)],
           sizeof(incoming));
    if (image_header->magic != ESP_IMAGE_HEADER_MAGIC ||
        incoming.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        strncmp(incoming.project_name, EXPECTED_PROJECT_NAME,
                sizeof(incoming.project_name)) != 0 ||
        incoming.version[0] == '\0') {
        error = ESP_ERR_OTA_VALIDATE_FAILED;
        goto failed;
    }
    incoming.version[sizeof(incoming.version) - 1U] = '\0';
    set_incoming_version(incoming.version);
    ESP_LOGI(TAG, "receiving firmware v%s into OTA slot at 0x%lx",
             incoming.version, (unsigned long)partition->address);

    error = esp_ota_begin(partition, total, &update_handle);
    if (error != ESP_OK) {
        goto failed;
    }
    update_handle_open = true;
    error = esp_ota_write(update_handle, buffer, buffered);
    if (error != ESP_OK) {
        goto failed;
    }
    size_t received_total = buffered;
    set_progress(received_total, total);

    while (received_total < total) {
        const size_t remaining = total - received_total;
        const size_t chunk_size =
            remaining < UPDATE_HTTP_BUFFER_SIZE ? remaining
                                                : UPDATE_HTTP_BUFFER_SIZE;
        size_t chunk_received = 0U;
        error = receive_exact(request, buffer, chunk_size, &chunk_received);
        if (error != ESP_OK) {
            goto failed;
        }
        error = esp_ota_write(update_handle, buffer, chunk_received);
        if (error != ESP_OK) {
            goto failed;
        }
        received_total += chunk_received;
        set_progress(received_total, total);
    }

    set_state(FIRMWARE_UPDATE_STATE_VERIFYING, ESP_OK);
    error = esp_ota_end(update_handle);
    update_handle_open = false;
    if (error != ESP_OK) {
        goto failed;
    }
    error = esp_ota_set_boot_partition(partition);
    if (error != ESP_OK) {
        goto failed;
    }

    free(buffer);
    set_state(FIRMWARE_UPDATE_STATE_SUCCESS, ESP_OK);
    const esp_err_t response_error = send_page(
        request, "200 OK", "text/plain; charset=utf-8",
        UPDATE_SUCCESS_PAGE);
    xEventGroupSetBits(s_events, UPDATE_EVENT_COMPLETE);
    return response_error;

failed:
    if (update_handle_open) {
        (void)esp_ota_abort(update_handle);
    }
    free(buffer);
    ESP_LOGE(TAG, "firmware upload failed: %s", esp_err_to_name(error));
    set_state(FIRMWARE_UPDATE_STATE_FAILED, error);
    const esp_err_t failure_response_error = send_page(
        request, "400 Bad Request", "text/plain; charset=utf-8",
        UPDATE_ERROR_PAGE);
    xEventGroupSetBits(s_events, UPDATE_EVENT_FAILED);
    return failure_response_error;
}

static esp_err_t redirect_handler(httpd_req_t *request,
                                  httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_send(request, "Open the update page",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192U;
    config.max_uri_handlers = 4U;
    config.max_open_sockets = 2U;
    config.recv_wait_timeout = 15U;
    config.lru_purge_enable = true;

    esp_err_t error = httpd_start(&s_http_server, &config);
    if (error != ESP_OK) {
        return error;
    }
    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = update_get_handler,
    };
    const httpd_uri_t update_uri = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = update_post_handler,
    };
    error = httpd_register_uri_handler(s_http_server, &root_uri);
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &update_uri);
    }
    if (error == ESP_OK) {
        error = httpd_register_err_handler(s_http_server,
                                           HTTPD_404_NOT_FOUND,
                                           redirect_handler);
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

static void configure_captive_portal(void)
{
    esp_netif_t *ap_netif =
        esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == NULL) {
        ESP_LOGW(TAG, "SoftAP network interface is unavailable");
        return;
    }
    esp_err_t error = esp_netif_dhcps_stop(ap_netif);
    if (error == ESP_OK) {
        error = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                                       ESP_NETIF_CAPTIVEPORTAL_URI,
                                       (void *)UPDATE_URL,
                                       strlen(UPDATE_URL));
    }
    const esp_err_t start_error = esp_netif_dhcps_start(ap_netif);
    if (error != ESP_OK || start_error != ESP_OK) {
        ESP_LOGW(TAG, "could not configure captive portal hint: %s / %s",
                 esp_err_to_name(error), esp_err_to_name(start_error));
    }
}

static void generate_password(char *password, size_t capacity)
{
    const uint64_t entropy = ((uint64_t)esp_random() << 32U) | esp_random();
    (void)network_setup_password_from_entropy(entropy, password, capacity);
}

static esp_err_t stop_update_ap(void)
{
    const esp_err_t error = esp_wifi_stop();
    return error == ESP_ERR_WIFI_NOT_STARTED ? ESP_OK : error;
}

static esp_err_t start_update_ap(void)
{
    uint8_t mac[6] = {0};
    esp_err_t error = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (error != ESP_OK) {
        return error;
    }

    char ssid[FIRMWARE_UPDATE_SSID_CAPACITY] = {0};
    char password[FIRMWARE_UPDATE_PASSWORD_CAPACITY] = {0};
    if (!network_setup_ssid_build(UPDATE_AP_BASE_NAME, true, mac, ssid,
                                  sizeof(ssid))) {
        return ESP_ERR_INVALID_ARG;
    }
    generate_password(password, sizeof(password));

    wifi_config_t config = {0};
    memcpy(config.ap.ssid, ssid, strlen(ssid));
    memcpy(config.ap.password, password, strlen(password));
    config.ap.ssid_len = (uint8_t)strlen(ssid);
    config.ap.channel = 1U;
    config.ap.max_connection = 1U;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.capable = true;

    error = esp_wifi_set_mode(WIFI_MODE_AP);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_AP, &config);
    }
    if (error == ESP_OK) {
        error = esp_wifi_start();
    }
    if (error != ESP_OK) {
        memset(password, 0, sizeof(password));
        memset(&config, 0, sizeof(config));
        (void)stop_update_ap();
        return error;
    }

    /* Once the radio is active, regenerate the password from the hardware RNG. */
    generate_password(password, sizeof(password));
    error = stop_update_ap();
    if (error == ESP_OK) {
        memset(config.ap.password, 0, sizeof(config.ap.password));
        memcpy(config.ap.password, password, strlen(password));
        error = esp_wifi_set_config(WIFI_IF_AP, &config);
    }
    if (error == ESP_OK) {
        error = esp_wifi_start();
    }
    memset(&config, 0, sizeof(config));
    if (error != ESP_OK) {
        memset(password, 0, sizeof(password));
        (void)stop_update_ap();
        return error;
    }

    configure_captive_portal();
    set_access_details(ssid, password);
    memset(password, 0, sizeof(password));
    ESP_LOGI(TAG, "temporary update access point ready: %s at %s",
             ssid, UPDATE_URL);
    return ESP_OK;
}

static void update_task(void *argument)
{
    (void)argument;
    esp_err_t error = network_time_begin_maintenance();
    if (error != ESP_OK) {
        set_state(FIRMWARE_UPDATE_STATE_FAILED, error);
        vTaskDelete(NULL);
        return;
    }

    error = start_update_ap();
    if (error == ESP_OK) {
        error = start_web_server();
    }
    if (error != ESP_OK) {
        stop_web_server();
        (void)stop_update_ap();
        network_time_end_maintenance();
        set_state(FIRMWARE_UPDATE_STATE_FAILED, error);
        vTaskDelete(NULL);
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_events, UPDATE_EVENT_ALL, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(UPDATE_WINDOW_MS));
    if ((bits & UPDATE_EVENT_UPLOAD_STARTED) != 0U &&
        (bits & (UPDATE_EVENT_COMPLETE | UPDATE_EVENT_FAILED |
                 UPDATE_EVENT_CANCEL)) == 0U) {
        bits |= xEventGroupWaitBits(
            s_events, UPDATE_EVENT_COMPLETE | UPDATE_EVENT_FAILED,
            pdTRUE, pdFALSE, portMAX_DELAY);
    }
    if ((bits & UPDATE_EVENT_COMPLETE) != 0U) {
        ESP_LOGI(TAG, "firmware image verified; restarting into new OTA slot");
        vTaskDelay(pdMS_TO_TICKS(UPDATE_RESTART_DELAY_MS));
        esp_restart();
    }

    if ((bits & UPDATE_EVENT_FAILED) != 0U) {
        vTaskDelay(pdMS_TO_TICKS(UPDATE_SERVER_STOP_DELAY_MS));
    } else if ((bits & UPDATE_EVENT_CANCEL) != 0U) {
        set_state(FIRMWARE_UPDATE_STATE_CANCELLED, ESP_OK);
    } else {
        set_state(FIRMWARE_UPDATE_STATE_EXPIRED, ESP_ERR_TIMEOUT);
    }
    stop_web_server();
    (void)stop_update_ap();
    network_time_end_maintenance();
    vTaskDelete(NULL);
}

esp_err_t firmware_update_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (running == NULL || next == NULL || running == next) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "dual-slot OTA service ready");
    return ESP_OK;
}

esp_err_t firmware_update_confirm_running_image(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t error = esp_ota_get_state_partition(running, &state);
    if (error == ESP_ERR_NOT_SUPPORTED ||
        (error == ESP_OK && state != ESP_OTA_IMG_PENDING_VERIFY)) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    ESP_LOGI(TAG, "confirming the new OTA image after startup diagnostics");
    return esp_ota_mark_app_valid_cancel_rollback();
}

esp_err_t firmware_update_start(void)
{
    if (!s_initialized || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    if (s_status.state == FIRMWARE_UPDATE_STATE_IDLE) {
        reset_status_locked(FIRMWARE_UPDATE_STATE_STARTING, ESP_OK);
        s_upload_started = false;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupClearBits(s_events, UPDATE_EVENT_ALL);
    if (xTaskCreate(update_task, "firmware_update", 8192U, NULL, 6U, NULL) !=
        pdPASS) {
        set_state(FIRMWARE_UPDATE_STATE_FAILED, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t firmware_update_cancel(void)
{
    const firmware_update_state_t state = get_state();
    if (state != FIRMWARE_UPDATE_STATE_STARTING &&
        state != FIRMWARE_UPDATE_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    set_state(FIRMWARE_UPDATE_STATE_CANCELLED, ESP_OK);
    xEventGroupSetBits(s_events, UPDATE_EVENT_CANCEL);
    return ESP_OK;
}

esp_err_t firmware_update_dismiss_result(void)
{
    bool dismissed = false;
    portENTER_CRITICAL(&s_status_lock);
    if (firmware_update_state_is_dismissible(s_status.state)) {
        reset_status_locked(FIRMWARE_UPDATE_STATE_IDLE, ESP_OK);
        s_upload_started = false;
        dismissed = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
    return dismissed ? ESP_OK : ESP_ERR_INVALID_STATE;
}
