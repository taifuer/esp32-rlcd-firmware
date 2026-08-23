#include "firmware_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_settings.h"
#include "clock_service.h"
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
#include "settings_portal_policy.h"

#define UPDATE_EVENT_COMPLETE BIT1
#define UPDATE_EVENT_FAILED BIT2
#define UPDATE_EVENT_CANCEL BIT3
#define UPDATE_EVENT_RESTART_REQUEST BIT4
#define UPDATE_EVENT_MUTATION_ENDED BIT5
#define UPDATE_EVENT_SESSION                                                    \
    (UPDATE_EVENT_COMPLETE | UPDATE_EVENT_FAILED | UPDATE_EVENT_CANCEL |       \
     UPDATE_EVENT_RESTART_REQUEST)
#define UPDATE_EVENT_ALL                                                       \
    (UPDATE_EVENT_SESSION | UPDATE_EVENT_MUTATION_ENDED)

#define SETTINGS_WINDOW_MS 300000U
#define SETTINGS_ACTIVE_REQUEST_GRACE_MS 20000U
#define UPDATE_RESTART_DELAY_MS 1800U
#define UPDATE_SERVER_STOP_DELAY_MS 250U
#define UPDATE_HTTP_BUFFER_SIZE 4096U
#define SETTINGS_PORTAL_FORM_CAPACITY (APP_SETTINGS_FORM_MAX_LENGTH + 1U)
#define SETTINGS_PORTAL_SMALL_FORM_CAPACITY 64U
#define SETTINGS_PORTAL_TOKEN_HEADER "X-RLCD-Token"

static const char *TAG = "firmware_update";
static const char SETTINGS_AP_BASE_NAME[] = "ESP32-RLCD-SETTINGS";
static const char SETTINGS_URL[] = "http://192.168.4.1";
static const char EXPECTED_PROJECT_NAME[] = "rlcd_firmware";

static const char SETTINGS_PAGE[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>设备设置</title><style>"
    ":root{color-scheme:light;font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "color:#171717;background:#f5f6f7}*{box-sizing:border-box}body{margin:0}main{width:calc(100% - 2rem);max-width:34rem;"
    "margin:0 auto;padding:2rem 0 3rem}header{padding:.4rem 0 1rem}h1{font-size:1.85rem;margin:0 0 .55rem}"
    "h2{font-size:1.15rem;margin:0 0 .9rem}p{line-height:1.55;margin:.45rem 0;color:#555}section{background:#fff;"
    "border:1px solid #e2e4e7;border-radius:.8rem;padding:1.15rem;margin:1rem 0}label{display:block;font-weight:600;"
    "margin:.9rem 0 .35rem}input,select,button{width:100%;font:inherit;border-radius:.55rem;padding:.72rem .8rem;"
    "border:1px solid #a9adb2;background:#fff}input[type=range]{padding:.35rem 0;border:0}.days{display:grid;"
    "grid-template-columns:repeat(7,minmax(0,1fr));gap:.35rem}.day{display:flex;flex-direction:column;align-items:center;"
    "gap:.35rem;margin:0;padding:.55rem .2rem;border:1px solid #d7dade;border-radius:.55rem;font-weight:500}.day input{width:auto;margin:0;padding:0;accent-color:#171717}button{margin-top:.9rem;"
    "border-color:#171717;background:#171717;color:#fff;font-weight:650}button.secondary{background:#fff;color:#171717}"
    "button.danger{background:#fff;color:#a32626;border-color:#c96c6c}small,.note{font-size:.88rem;color:#676b70}"
    ".row{display:grid;grid-template-columns:1fr auto;gap:.75rem;align-items:center}.row output{min-width:3ch;text-align:right}"
    "progress{width:100%;height:1rem;margin-top:1rem}.message{min-height:1.5rem;margin-top:.75rem;color:#333}"
    "</style></head><body><main><header><h1>设备设置</h1>"
    "<p>设置只保存在设备中。临时热点关闭后，本页面将无法继续访问。</p></header>"
    "<section><h2>偏好设置</h2><form id=\"settings\">"
    "<label for=\"power\">运行模式</label><select id=\"power\" name=\"power\">"
    "<option value=\"normal\">正常</option><option value=\"saving\">低功耗</option></select>"
    "<p class=\"note\">低功耗模式减少自动联网和屏幕刷新；手动校时与升级仍可使用。</p>"
    "<label for=\"timezone\">时区</label><select id=\"timezone\" name=\"timezone\"></select>"
    "<label for=\"unit\">温度单位</label><select id=\"unit\" name=\"unit\">"
    "<option value=\"c\">摄氏度（°C）</option><option value=\"f\">华氏度（°F）</option></select>"
    "<label for=\"volume\">播放音量</label><div class=\"row\">"
    "<input id=\"volume\" name=\"volume\" type=\"range\" min=\"0\" max=\"100\" step=\"1\">"
    "<output id=\"volumeValue\">--</output></div>"
    "<p class=\"note\">此音量用于音频检测中的扬声器输出和闹钟；0% 静音。</p>"
    "<label for=\"alarm\">启用闹钟</label><select id=\"alarm\" name=\"alarm\">"
    "<option value=\"off\">关闭</option><option value=\"on\">开启</option></select>"
    "<label for=\"alarmTime\">响铃时间</label><input id=\"alarmTime\" type=\"time\" value=\"07:30\" step=\"60\" required>"
    "<input id=\"alarmHour\" name=\"alarm_hour\" type=\"hidden\" value=\"7\">"
    "<input id=\"alarmMinute\" name=\"alarm_minute\" type=\"hidden\" value=\"30\">"
    "<input id=\"alarmDays\" name=\"alarm_days\" type=\"hidden\" value=\"62\">"
    "<label>重复日期</label><div class=\"days\" role=\"group\" aria-label=\"重复日期\">"
    "<label class=\"day\"><input class=\"alarm-day\" type=\"checkbox\" data-bit=\"1\">日</label>"
    "<label class=\"day\"><input class=\"alarm-day\" type=\"checkbox\" data-bit=\"2\" checked>一</label>"
    "<label class=\"day\"><input class=\"alarm-day\" type=\"checkbox\" data-bit=\"4\" checked>二</label>"
    "<label class=\"day\"><input class=\"alarm-day\" type=\"checkbox\" data-bit=\"8\" checked>三</label>"
    "<label class=\"day\"><input class=\"alarm-day\" type=\"checkbox\" data-bit=\"16\" checked>四</label>"
    "<label class=\"day\"><input class=\"alarm-day\" type=\"checkbox\" data-bit=\"32\" checked>五</label>"
    "<label class=\"day\"><input class=\"alarm-day\" type=\"checkbox\" data-bit=\"64\">六</label></div>"
    "<p class=\"note\">设备开机时按本地 RTC 时间响铃，无需网络；关闭后仍会保留时间和重复日期。</p>"
    "<label for=\"updates\">Beta 更新</label><select id=\"updates\" name=\"updates\">"
    "<option value=\"stable\">关闭（默认）</option><option value=\"beta\">开启（开发者）</option></select>"
    "<p class=\"note\">测试固件可能不稳定，仅适合能使用本地 OTA 或 USB 恢复的开发者；安装仍需在设备上确认。关闭后不会自动降级。</p>"
    "<button type=\"submit\">保存设置</button></form>"
    "<p id=\"settingsMessage\" class=\"message\"></p></section>"
    "<section><h2>日期与时间</h2><p>无需互联网，使用当前手机时间校准设备 RTC。</p>"
    "<button id=\"setTime\" type=\"button\" class=\"secondary\">使用手机时间校准</button>"
    "<p id=\"timeMessage\" class=\"message\"></p></section>"
    "<section><h2>维护</h2><button id=\"defaults\" type=\"button\" class=\"secondary\">恢复偏好默认值</button>"
    "<button id=\"forgetWifi\" type=\"button\" class=\"danger\">清除 Wi-Fi 配置</button>"
    "<p class=\"note\">清除后设备会重启，并进入现有配网流程；其他偏好不会被删除。</p>"
    "<p id=\"maintenanceMessage\" class=\"message\"></p></section>"
    "<section><h2>本地固件升级</h2><p>请选择本项目发布的 <strong>OTA 固件</strong>。升级不会清除设置。</p>"
    "<input id=\"file\" type=\"file\" accept=\".bin,application/octet-stream\">"
    "<button id=\"upload\" type=\"button\">开始升级</button>"
    "<progress id=\"progress\" max=\"100\" value=\"0\"></progress>"
    "<p id=\"updateMessage\" class=\"message\">等待选择固件</p>"
    "<small>写入期间请保持设备供电。校验成功后设备会自动重启。</small></section>"
    "<script>let token='',initialUpdates='stable';const $=id=>document.getElementById(id);"
    "const show=(id,text)=>{$(id).textContent=text};const alarmDays=()=>document.querySelectorAll('.alarm-day');"
    "const unix=()=>String(Math.floor(Date.now()/1000));"
    "function zones(){const select=$('timezone');for(let minutes=-720;minutes<=840;minutes+=15){const option=document.createElement('option');"
    "const sign=minutes>=0?'+':'-';const absolute=Math.abs(minutes),hours=String(Math.floor(absolute/60)).padStart(2,'0'),"
    "remainder=String(absolute%60).padStart(2,'0');option.value=minutes;option.textContent='UTC'+sign+hours+':'+remainder+"
    "(minutes===480?' · 中国标准时间':'');select.appendChild(option)}}"
    "async function post(path,body){const headers={'Content-Type':'application/x-www-form-urlencoded',"
    "'X-RLCD-Token':token};const response=await fetch(path,{method:'POST',"
    "headers,body});const text=await response.text();if(!response.ok)throw new Error(text||'操作失败');return text;}"
    "async function load(){const response=await fetch('/api/state',{cache:'no-store'});if(!response.ok)throw new Error('无法读取设备设置');"
    "const state=await response.json();token=state.token;$('power').value=state.power;$('timezone').value=state.timezone;"
    "$('unit').value=state.unit;$('volume').value=state.volume;$('volumeValue').value=state.volume;$('updates').value=state.updates;"
    "$('alarm').value=state.alarm;$('alarmTime').value=String(state.alarm_hour).padStart(2,'0')+':'+String(state.alarm_minute).padStart(2,'0');"
    "alarmDays().forEach(input=>{input.checked=(state.alarm_days&Number(input.dataset.bit))!==0});initialUpdates=state.updates;}"
    "$('volume').oninput=()=>{$('volumeValue').value=$('volume').value};"
    "$('settings').onsubmit=async event=>{event.preventDefault();const match=/^(\\d{2}):(\\d{2})$/.exec($('alarmTime').value);"
    "const days=Array.from(alarmDays()).reduce((mask,input)=>input.checked?mask|Number(input.dataset.bit):mask,0);"
    "if(!match||Number(match[1])>23||Number(match[2])>59){show('settingsMessage','请选择有效的响铃时间。');return}"
    "if(days===0||(days&~127)!==0){show('settingsMessage','请至少选择一个重复日期。');return}"
    "$('alarmHour').value=String(Number(match[1]));$('alarmMinute').value=String(Number(match[2]));$('alarmDays').value=String(days);"
    "if(initialUpdates!=='beta'&&$('updates').value==='beta'&&!confirm('开启 Beta 更新？测试固件可能不稳定，请确认你能够使用本地 OTA 或 USB 恢复设备。'))return;show('settingsMessage','正在保存…');try{const body=new URLSearchParams(new FormData(event.target)).toString();"
    "show('settingsMessage',await post('/api/settings',body));}catch(error){show('settingsMessage',error.message)}};"
    "$('setTime').onclick=async()=>{show('timeMessage','正在校准…');try{show('timeMessage',await post('/api/time','unix='+unix()))}"
    "catch(error){show('timeMessage',error.message)}};"
    "$('defaults').onclick=async()=>{if(!confirm('恢复偏好默认值？Wi-Fi 配置不会被删除。'))return;"
    "show('maintenanceMessage','正在恢复…');try{show('maintenanceMessage',await post('/api/settings/defaults','confirm=DEFAULTS'))}"
    "catch(error){show('maintenanceMessage',error.message)}};"
    "$('forgetWifi').onclick=async()=>{if(!confirm('清除 Wi-Fi 配置并重启？之后需要重新配网。'))return;"
    "show('maintenanceMessage','正在清除…');try{show('maintenanceMessage',await post('/api/wifi/clear','confirm=FORGET'))}"
    "catch(error){show('maintenanceMessage',error.message)}};"
    "$('upload').onclick=()=>{const file=$('file'),button=$('upload'),progress=$('progress');if(!file.files.length){"
    "show('updateMessage','请先选择 OTA 固件');return}if(!confirm('开始写入固件？写入期间请保持供电。'))return;"
    "button.disabled=true;file.disabled=true;const request=new XMLHttpRequest();request.open('POST','/update');"
    "request.setRequestHeader('Content-Type','application/octet-stream');request.setRequestHeader('X-RLCD-Token',token);"
    "request.upload.onprogress=event=>{if(event.lengthComputable){const value=Math.round(event.loaded*100/event.total);"
    "progress.value=value;show('updateMessage','正在上传 '+value+'%')}};request.onload=()=>{show('updateMessage',request.responseText||"
    "(request.status===200?'升级成功，设备即将重启':'升级失败'));if(request.status!==200){button.disabled=false;file.disabled=false}};"
    "request.onerror=()=>show('updateMessage','连接中断，请查看设备屏幕');request.send(file.files[0])};"
    "zones();load().catch(error=>show('settingsMessage',error.message));</script></main></body></html>";

static const char UPDATE_SUCCESS_PAGE[] =
    "升级成功。固件已校验，设备即将自动重启。";
static const char UPDATE_ERROR_PAGE[] =
    "升级失败。请确认选择的是本项目的 OTA 固件，然后重新开启升级模式。";

static EventGroupHandle_t s_events;
static httpd_handle_t s_http_server;
static bool s_initialized;
static bool s_upload_started;
static bool s_mutation_active;
static bool s_restart_requested;
static bool s_session_closing;
static bool s_session_deadline_active;
static uint32_t s_session_started_tick;
static char s_session_token[SETTINGS_PORTAL_TOKEN_CAPACITY];
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static firmware_update_status_t s_status = {
    .state = FIRMWARE_UPDATE_STATE_IDLE,
    .last_error = ESP_OK,
};

static void reset_status_locked(firmware_update_state_t state,
                                esp_err_t error)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(s_session_token, 0, sizeof(s_session_token));
    s_mutation_active = false;
    s_restart_requested = false;
    s_session_closing = false;
    s_session_deadline_active = false;
    s_session_started_tick = 0U;
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

static void set_access_details(const char *ssid, const char *password)
{
    char ssid_value[FIRMWARE_UPDATE_SSID_CAPACITY] = {0};
    char password_value[FIRMWARE_UPDATE_PASSWORD_CAPACITY] = {0};
    char url_value[FIRMWARE_UPDATE_URL_CAPACITY] = {0};
    snprintf(ssid_value, sizeof(ssid_value), "%s", ssid);
    snprintf(password_value, sizeof(password_value), "%s", password);
    snprintf(url_value, sizeof(url_value), "%s", SETTINGS_URL);

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

static uint32_t settings_window_ticks(void)
{
    return (uint32_t)pdMS_TO_TICKS(SETTINGS_WINDOW_MS);
}

static bool refresh_session_deadline_locked(uint32_t now)
{
    if (s_session_deadline_active &&
        settings_portal_deadline_remaining(
            s_session_started_tick, now, settings_window_ticks()) == 0U) {
        s_session_closing = true;
        return true;
    }
    return false;
}

static bool session_deadline_expired(void)
{
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    bool expired;
    portENTER_CRITICAL(&s_status_lock);
    expired = refresh_session_deadline_locked(now);
    portEXIT_CRITICAL(&s_status_lock);
    return expired;
}

static void start_session_deadline(void)
{
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    portENTER_CRITICAL(&s_status_lock);
    s_session_started_tick = now;
    s_session_deadline_active = true;
    portEXIT_CRITICAL(&s_status_lock);
}

static TickType_t session_deadline_remaining_ticks(void)
{
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    uint32_t remaining = 0U;
    portENTER_CRITICAL(&s_status_lock);
    if (s_session_deadline_active) {
        remaining = settings_portal_deadline_remaining(
            s_session_started_tick, now, settings_window_ticks());
        if (remaining == 0U) {
            s_session_closing = true;
        }
    }
    portEXIT_CRITICAL(&s_status_lock);
    return (TickType_t)remaining;
}

static bool portal_is_ready(void);

static esp_err_t settings_get_handler(httpd_req_t *request)
{
    if (!portal_is_ready()) {
        return send_page(request, "409 Conflict",
                         "text/plain; charset=utf-8",
                         "设置会话已关闭。\n");
    }
    return send_page(request, "200 OK", "text/html; charset=utf-8",
                     SETTINGS_PAGE);
}

static esp_err_t receive_exact(httpd_req_t *request, uint8_t *buffer,
                               size_t requested, size_t *received_total)
{
    unsigned timeout_count = 0U;
    while (*received_total < requested) {
        if (session_deadline_expired()) {
            return ESP_ERR_TIMEOUT;
        }
        const int received = httpd_req_recv(
            request, (char *)&buffer[*received_total],
            requested - *received_total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            if (session_deadline_expired()) {
                return ESP_ERR_TIMEOUT;
            }
            if (timeout_count++ < 3U) {
                continue;
            }
        }
        if (received <= 0) {
            return ESP_FAIL;
        }
        *received_total += (size_t)received;
        if (session_deadline_expired()) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static bool portal_is_ready(void)
{
    bool ready;
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    portENTER_CRITICAL(&s_status_lock);
    (void)refresh_session_deadline_locked(now);
    ready = settings_portal_write_is_available(
        s_status.state == FIRMWARE_UPDATE_STATE_READY && !s_session_closing,
        s_upload_started,
        s_mutation_active, s_restart_requested);
    portEXIT_CRITICAL(&s_status_lock);
    return ready;
}

static bool request_token_is_valid(httpd_req_t *request)
{
    const size_t length = httpd_req_get_hdr_value_len(
        request, SETTINGS_PORTAL_TOKEN_HEADER);
    if (length != SETTINGS_PORTAL_TOKEN_LENGTH) {
        return false;
    }

    char provided[SETTINGS_PORTAL_TOKEN_CAPACITY] = {0};
    char expected[SETTINGS_PORTAL_TOKEN_CAPACITY] = {0};
    if (httpd_req_get_hdr_value_str(request, SETTINGS_PORTAL_TOKEN_HEADER,
                                    provided, sizeof(provided)) != ESP_OK) {
        return false;
    }
    portENTER_CRITICAL(&s_status_lock);
    memcpy(expected, s_session_token, sizeof(expected));
    portEXIT_CRITICAL(&s_status_lock);
    const bool valid = settings_portal_token_matches(expected, provided);
    memset(expected, 0, sizeof(expected));
    memset(provided, 0, sizeof(provided));
    return valid;
}

static bool authorize_post(httpd_req_t *request)
{
    if (!portal_is_ready()) {
        (void)send_page(request, "409 Conflict",
                        "text/plain; charset=utf-8",
                        "设置会话不可用，或固件写入已经开始。\n");
        return false;
    }
    if (!request_token_is_valid(request)) {
        (void)send_page(request, "403 Forbidden",
                        "text/plain; charset=utf-8",
                        "设置会话验证失败，请刷新页面后重试。\n");
        return false;
    }
    return true;
}

static bool begin_regular_mutation(void)
{
    bool accepted = false;
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    portENTER_CRITICAL(&s_status_lock);
    (void)refresh_session_deadline_locked(now);
    if (settings_portal_write_is_available(
            s_status.state == FIRMWARE_UPDATE_STATE_READY &&
                !s_session_closing,
            s_upload_started, s_mutation_active, s_restart_requested)) {
        s_mutation_active = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
    return accepted;
}

static void end_regular_mutation(void)
{
    portENTER_CRITICAL(&s_status_lock);
    s_mutation_active = false;
    portEXIT_CRITICAL(&s_status_lock);
    xEventGroupSetBits(s_events, UPDATE_EVENT_MUTATION_ENDED);
}

static esp_err_t finish_regular_request(httpd_req_t *request,
                                        const char *status,
                                        const char *message)
{
    const esp_err_t response_error = send_page(
        request, status, "text/plain; charset=utf-8", message);
    end_regular_mutation();
    return response_error;
}

static bool begin_upload(size_t total)
{
    bool accepted = false;
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    portENTER_CRITICAL(&s_status_lock);
    (void)refresh_session_deadline_locked(now);
    if (settings_portal_write_is_available(
            s_status.state == FIRMWARE_UPDATE_STATE_READY &&
                !s_session_closing,
            s_upload_started, s_mutation_active, s_restart_requested)) {
        s_upload_started = true;
        s_status.state = FIRMWARE_UPDATE_STATE_RECEIVING;
        s_status.received_bytes = 0U;
        s_status.total_bytes = total;
        s_status.percent = 0U;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
    return accepted;
}

static esp_err_t send_deadline_response(httpd_req_t *request)
{
    return send_page(request, "408 Request Timeout",
                     "text/plain; charset=utf-8",
                     "设置会话已到期，请重新在设备上开启。\n");
}

static esp_err_t send_mutation_unavailable(httpd_req_t *request)
{
    if (session_deadline_expired()) {
        return send_deadline_response(request);
    }
    return send_page(request, "409 Conflict", "text/plain; charset=utf-8",
                     "另一个设置操作正在进行。\n");
}

static esp_err_t receive_form(httpd_req_t *request, char *body,
                              size_t capacity, size_t *length)
{
    if (request->content_len <= 0 || body == NULL || length == NULL ||
        (size_t)request->content_len >= capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0U;
    const esp_err_t error = receive_exact(
        request, (uint8_t *)body, (size_t)request->content_len, &received);
    if (error != ESP_OK) {
        return error;
    }
    body[received] = '\0';
    *length = received;
    return ESP_OK;
}

static esp_err_t settings_state_get_handler(httpd_req_t *request)
{
    if (!portal_is_ready()) {
        return send_page(request, "409 Conflict",
                         "text/plain; charset=utf-8",
                         "设置会话已关闭。\n");
    }
    app_settings_t settings = {0};
    const esp_err_t error = app_settings_get(&settings);
    if (error != ESP_OK) {
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备设置暂不可用。\n");
    }

    char token[SETTINGS_PORTAL_TOKEN_CAPACITY] = {0};
    portENTER_CRITICAL(&s_status_lock);
    memcpy(token, s_session_token, sizeof(token));
    portEXIT_CRITICAL(&s_status_lock);
    char json[384];
    const int written = snprintf(
        json, sizeof(json),
        "{\"power\":\"%s\",\"timezone\":%d,\"unit\":\"%s\","
        "\"volume\":%u,\"updates\":\"%s\",\"alarm\":\"%s\","
        "\"alarm_hour\":%u,\"alarm_minute\":%u,\"alarm_days\":%u,"
        "\"token\":\"%s\"}",
        settings.power_mode == APP_POWER_MODE_SAVING ? "saving" : "normal",
        settings.utc_offset_minutes,
        settings.temperature_unit == APP_TEMPERATURE_UNIT_FAHRENHEIT ? "f"
                                                                     : "c",
        settings.audio_playback_volume,
        settings.update_channel == APP_UPDATE_CHANNEL_BETA ? "beta"
                                                           : "stable",
        settings.alarm_enabled ? "on" : "off",
        (unsigned int)settings.alarm_hour,
        (unsigned int)settings.alarm_minute,
        (unsigned int)settings.alarm_weekdays,
        token);
    memset(token, 0, sizeof(token));
    if (written <= 0 || (size_t)written >= sizeof(json)) {
        return send_page(request, "500 Internal Server Error",
                         "text/plain; charset=utf-8",
                         "无法生成设置状态。\n");
    }
    return send_page(request, "200 OK", "application/json; charset=utf-8",
                     json);
}

static esp_err_t restore_previous_settings(
    const app_settings_t *previous)
{
    esp_err_t result = app_settings_save(previous);
    const esp_err_t timezone_error = app_settings_apply_timezone(previous);
    if (result == ESP_OK) {
        result = timezone_error;
    }
    return result;
}

static bool current_unix_time_is_reliable(int64_t *unix_seconds)
{
    if (unix_seconds == NULL) {
        return false;
    }

    const time_t now = time(NULL);
    if (now == (time_t)-1) {
        return false;
    }
    const int64_t candidate = (int64_t)now;
    if ((time_t)candidate != now) {
        return false;
    }

    struct tm utc = {0};
    if (gmtime_r(&now, &utc) == NULL || utc.tm_year < 100 ||
        utc.tm_year > 199) {
        return false;
    }
    *unix_seconds = candidate;
    return true;
}

static esp_err_t save_settings(
    const app_settings_t *replacement, bool *time_needs_calibration)
{
    if (time_needs_calibration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *time_needs_calibration = false;

    /* The caller has already received and validated the bounded form, then
     * acquired the regular-mutation slot before the session deadline. From
     * this point the NVS/TZ/RTC transaction must finish atomically; the
     * session task grants it a bounded cleanup window. OTA remains subject to
     * deadline checks at every receive and flash boundary below. */

    app_settings_t previous = {0};
    esp_err_t error = app_settings_get(&previous);
    if (error != ESP_OK) {
        return error;
    }
    const bool timezone_changed =
        previous.utc_offset_minutes != replacement->utc_offset_minutes;
    error = app_settings_save(replacement);
    if (error != ESP_OK) {
        return error;
    }
    if (!timezone_changed) {
        return ESP_OK;
    }

    int64_t unix_seconds = 0;
    const bool unix_time_reliable =
        current_unix_time_is_reliable(&unix_seconds);
    error = app_settings_apply_timezone(replacement);
    if (error != ESP_OK) {
        const esp_err_t rollback_error = restore_previous_settings(&previous);
        if (rollback_error != ESP_OK) {
            ESP_LOGE(TAG,
                     "could not restore settings after timezone failure: %s",
                     esp_err_to_name(rollback_error));
        }
        return error;
    }

    if (!unix_time_reliable) {
        *time_needs_calibration = true;
        ESP_LOGW(TAG,
                 "timezone saved, but current Unix time is not reliable; "
                 "phone calibration is required");
        return ESP_OK;
    }

    error = clock_service_set_unix_time(unix_seconds);
    if (error != ESP_OK) {
        *time_needs_calibration = true;
        ESP_LOGW(TAG,
                 "timezone saved, but RTC could not preserve Unix time: %s; "
                 "phone calibration is required",
                 esp_err_to_name(error));
    }
    return ESP_OK;
}

static esp_err_t finish_restarting_request(httpd_req_t *request,
                                           const char *message)
{
    const esp_err_t response_error = send_page(
        request, "200 OK", "text/plain; charset=utf-8", message);
    portENTER_CRITICAL(&s_status_lock);
    s_mutation_active = false;
    s_restart_requested = true;
    portEXIT_CRITICAL(&s_status_lock);
    xEventGroupSetBits(s_events, UPDATE_EVENT_RESTART_REQUEST);
    return response_error;
}

static esp_err_t settings_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_PORTAL_FORM_CAPACITY] = {0};
    size_t length = 0U;
    app_settings_t settings = {0};
    const esp_err_t receive_error = receive_form(
        request, body, sizeof(body), &length);
    const bool valid = receive_error == ESP_OK &&
                       app_settings_parse_form(body, length, &settings);
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!valid) {
        return send_page(
            request, "400 Bad Request", "text/plain; charset=utf-8",
            "设置内容无效，请检查后重试。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    bool time_needs_calibration = false;
    const esp_err_t error = save_settings(
        &settings, &time_needs_calibration);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "settings save failed: %s", esp_err_to_name(error));
        return finish_regular_request(
            request, "500 Internal Server Error",
            "设置未能完整保存，原设置已尽量恢复。\n");
    }
    if (time_needs_calibration) {
        return finish_restarting_request(
            request,
            "设置已保存，但 RTC 未能随时区可靠更新。设备重启后请重新打开设置门户，使用手机时间校准。\n");
    }
    return finish_restarting_request(
        request, "设置已保存，设备即将重启。\n");
}

static esp_err_t time_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_PORTAL_SMALL_FORM_CAPACITY] = {0};
    size_t length = 0U;
    int64_t unix_seconds = 0;
    const esp_err_t receive_error = receive_form(
        request, body, sizeof(body), &length);
    const bool valid = receive_error == ESP_OK &&
                       settings_portal_parse_unix_form(
                           body, length, &unix_seconds);
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!valid) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "手机时间无效。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    const esp_err_t error = clock_service_set_unix_time(unix_seconds);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "phone time calibration failed: %s",
                 esp_err_to_name(error));
        return finish_regular_request(
            request, "400 Bad Request",
            "校准失败，请确认 RTC 可用且手机时间正确。\n");
    }
    return finish_regular_request(request, "200 OK",
                                  "RTC 已使用手机时间校准。\n");
}

static esp_err_t defaults_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_PORTAL_SMALL_FORM_CAPACITY] = {0};
    size_t length = 0U;
    const esp_err_t receive_error = receive_form(
        request, body, sizeof(body), &length);
    const bool valid = receive_error == ESP_OK &&
                       settings_portal_confirmation_matches(
                           body, length, "DEFAULTS");
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!valid) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "恢复默认设置需要重新确认。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    app_settings_t defaults = {0};
    app_settings_defaults(&defaults);
    bool time_needs_calibration = false;
    const esp_err_t error = save_settings(
        &defaults, &time_needs_calibration);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "settings reset failed: %s", esp_err_to_name(error));
        return finish_regular_request(
            request, "500 Internal Server Error",
            "默认设置未能完整恢复，原设置已尽量保留。\n");
    }
    if (time_needs_calibration) {
        return finish_restarting_request(
            request,
            "偏好已恢复默认值，但 RTC 未能随默认时区可靠更新。设备重启后请重新打开设置门户，使用手机时间校准。\n");
    }
    return finish_restarting_request(
        request, "偏好已恢复默认值，设备即将重启。\n");
}

static esp_err_t wifi_clear_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_PORTAL_SMALL_FORM_CAPACITY] = {0};
    size_t length = 0U;
    const esp_err_t receive_error = receive_form(
        request, body, sizeof(body), &length);
    const bool confirmed = receive_error == ESP_OK &&
                           settings_portal_confirmation_matches(
                               body, length, "FORGET");
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!confirmed) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "清除 Wi-Fi 需要重新确认。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    const esp_err_t error = network_time_clear_credentials();
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi credential reset failed: %s",
                 esp_err_to_name(error));
        return finish_regular_request(request, "500 Internal Server Error",
                                      "Wi-Fi 配置未能清除。\n");
    }
    return finish_restarting_request(
        request, "Wi-Fi 配置已清除，设备即将重启并进入配网。\n");
}

static esp_err_t update_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    const size_t total = request->content_len > 0
                             ? (size_t)request->content_len
                             : 0U;
    if (!begin_upload(total)) {
        return send_page(request, "409 Conflict", "text/plain; charset=utf-8",
                         "升级会话不可用或已有上传正在进行。\n");
    }
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

    if (session_deadline_expired()) {
        error = ESP_ERR_TIMEOUT;
        goto failed;
    }
    error = esp_ota_begin(partition, total, &update_handle);
    if (error == ESP_OK) {
        update_handle_open = true;
    }
    if (session_deadline_expired()) {
        error = ESP_ERR_TIMEOUT;
        goto failed;
    }
    if (error != ESP_OK) {
        goto failed;
    }
    error = esp_ota_write(update_handle, buffer, buffered);
    if (session_deadline_expired()) {
        error = ESP_ERR_TIMEOUT;
        goto failed;
    }
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
        if (session_deadline_expired()) {
            error = ESP_ERR_TIMEOUT;
            goto failed;
        }
        error = esp_ota_write(update_handle, buffer, chunk_received);
        if (session_deadline_expired()) {
            error = ESP_ERR_TIMEOUT;
            goto failed;
        }
        if (error != ESP_OK) {
            goto failed;
        }
        received_total += chunk_received;
        set_progress(received_total, total);
    }

    set_state(FIRMWARE_UPDATE_STATE_VERIFYING, ESP_OK);
    if (session_deadline_expired()) {
        error = ESP_ERR_TIMEOUT;
        goto failed;
    }
    error = esp_ota_end(update_handle);
    update_handle_open = false;
    if (error != ESP_OK) {
        goto failed;
    }
    if (session_deadline_expired()) {
        error = ESP_ERR_TIMEOUT;
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
    const bool deadline_timeout =
        error == ESP_ERR_TIMEOUT && session_deadline_expired();
    if (deadline_timeout) {
        ESP_LOGW(TAG, "firmware upload stopped at the settings deadline");
        set_state(FIRMWARE_UPDATE_STATE_FAILED, error);
    } else {
        ESP_LOGE(TAG, "firmware upload failed: %s", esp_err_to_name(error));
        set_state(FIRMWARE_UPDATE_STATE_FAILED, error);
    }
    const esp_err_t failure_response_error =
        deadline_timeout
            ? send_deadline_response(request)
            : send_page(request, "400 Bad Request",
                        "text/plain; charset=utf-8", UPDATE_ERROR_PAGE);
    xEventGroupSetBits(s_events, UPDATE_EVENT_FAILED);
    return failure_response_error;
}

static esp_err_t redirect_handler(httpd_req_t *request,
                                  httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_send(request, "Open the settings page",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192U;
    config.max_uri_handlers = 10U;
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
        .handler = settings_get_handler,
    };
    const httpd_uri_t state_uri = {
        .uri = "/api/state",
        .method = HTTP_GET,
        .handler = settings_state_get_handler,
    };
    const httpd_uri_t settings_uri = {
        .uri = "/api/settings",
        .method = HTTP_POST,
        .handler = settings_post_handler,
    };
    const httpd_uri_t time_uri = {
        .uri = "/api/time",
        .method = HTTP_POST,
        .handler = time_post_handler,
    };
    const httpd_uri_t defaults_uri = {
        .uri = "/api/settings/defaults",
        .method = HTTP_POST,
        .handler = defaults_post_handler,
    };
    const httpd_uri_t wifi_clear_uri = {
        .uri = "/api/wifi/clear",
        .method = HTTP_POST,
        .handler = wifi_clear_post_handler,
    };
    const httpd_uri_t update_uri = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = update_post_handler,
    };
    error = httpd_register_uri_handler(s_http_server, &root_uri);
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &state_uri);
    }
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &settings_uri);
    }
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &time_uri);
    }
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &defaults_uri);
    }
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &wifi_clear_uri);
    }
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
    portENTER_CRITICAL(&s_status_lock);
    memset(s_session_token, 0, sizeof(s_session_token));
    portEXIT_CRITICAL(&s_status_lock);
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
                                       (void *)SETTINGS_URL,
                                       strlen(SETTINGS_URL));
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

static void generate_session_token(void)
{
    uint8_t entropy[SETTINGS_PORTAL_TOKEN_BYTES] = {0};
    char token[SETTINGS_PORTAL_TOKEN_CAPACITY] = {0};
    esp_fill_random(entropy, sizeof(entropy));
    if (settings_portal_token_encode(entropy, token, sizeof(token))) {
        portENTER_CRITICAL(&s_status_lock);
        memcpy(s_session_token, token, sizeof(token));
        portEXIT_CRITICAL(&s_status_lock);
    }
    memset(entropy, 0, sizeof(entropy));
    memset(token, 0, sizeof(token));
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
    if (!network_setup_ssid_build(SETTINGS_AP_BASE_NAME, true, mac, ssid,
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
    generate_session_token();
    set_access_details(ssid, password);
    memset(password, 0, sizeof(password));
    ESP_LOGI(TAG, "temporary settings access point ready: %s at %s",
             ssid, SETTINGS_URL);
    return ESP_OK;
}

static EventBits_t finish_active_request_after_timeout(void)
{
    settings_portal_timeout_action_t action;
    EventBits_t terminal_bits = 0U;

    /* Close admission first. Once this flag is visible, no new mutation can
     * begin. Only then clear the reusable completion bit and re-read all
     * state: a mutation which ended before/during the clear is already
     * inactive, while one still active can only publish a fresh completion
     * after the clear. This avoids both stale-bit consumption and lost
     * completion wakeups. */
    portENTER_CRITICAL(&s_status_lock);
    s_session_closing = true;
    portEXIT_CRITICAL(&s_status_lock);
    xEventGroupClearBits(s_events, UPDATE_EVENT_MUTATION_ENDED);

    portENTER_CRITICAL(&s_status_lock);
    if (s_restart_requested) {
        terminal_bits = UPDATE_EVENT_RESTART_REQUEST;
    } else if (s_status.state == FIRMWARE_UPDATE_STATE_SUCCESS) {
        terminal_bits = UPDATE_EVENT_COMPLETE;
    } else if (s_status.state == FIRMWARE_UPDATE_STATE_FAILED ||
               s_status.state == FIRMWARE_UPDATE_STATE_EXPIRED) {
        terminal_bits = UPDATE_EVENT_FAILED;
    } else if (s_status.state == FIRMWARE_UPDATE_STATE_CANCELLED) {
        terminal_bits = UPDATE_EVENT_CANCEL;
    }
    action = settings_portal_timeout_action(
        s_upload_started, s_mutation_active, s_restart_requested);
    if (action == SETTINGS_PORTAL_TIMEOUT_EXPIRE &&
        terminal_bits == 0U &&
        s_status.state == FIRMWARE_UPDATE_STATE_READY) {
        s_status.state = FIRMWARE_UPDATE_STATE_EXPIRED;
        s_status.last_error = ESP_ERR_TIMEOUT;
    }
    portEXIT_CRITICAL(&s_status_lock);

    if (terminal_bits != 0U) {
        return terminal_bits;
    }
    if (action == SETTINGS_PORTAL_TIMEOUT_RESTART) {
        return UPDATE_EVENT_RESTART_REQUEST;
    }

    EventBits_t wait_for = 0U;
    if (action == SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_UPLOAD) {
        wait_for = UPDATE_EVENT_COMPLETE | UPDATE_EVENT_FAILED;
    } else if (action == SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_MUTATION) {
        wait_for = UPDATE_EVENT_MUTATION_ENDED |
                   UPDATE_EVENT_RESTART_REQUEST;
    }
    if (wait_for == 0U) {
        return 0U;
    }
    const EventBits_t bits = xEventGroupWaitBits(
        s_events, wait_for, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(SETTINGS_ACTIVE_REQUEST_GRACE_MS));
    return bits & UPDATE_EVENT_SESSION;
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
        start_session_deadline();
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
        s_events, UPDATE_EVENT_SESSION, pdTRUE, pdFALSE,
        session_deadline_remaining_ticks());
    bits &= UPDATE_EVENT_SESSION;
    if (bits == 0U) {
        bits = finish_active_request_after_timeout();
    }
    if ((bits & UPDATE_EVENT_COMPLETE) != 0U) {
        ESP_LOGI(TAG, "firmware image verified; restarting into new OTA slot");
        vTaskDelay(pdMS_TO_TICKS(UPDATE_RESTART_DELAY_MS));
        esp_restart();
    }
    if ((bits & UPDATE_EVENT_RESTART_REQUEST) != 0U) {
        ESP_LOGI(TAG, "settings operation complete; restarting");
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
    ESP_LOGI(TAG, "local settings and dual-slot OTA service ready");
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
    bool accepted = false;
    portENTER_CRITICAL(&s_status_lock);
    if (!s_session_closing && !s_restart_requested && !s_mutation_active &&
        (s_status.state == FIRMWARE_UPDATE_STATE_STARTING ||
         s_status.state == FIRMWARE_UPDATE_STATE_READY)) {
        s_status.state = FIRMWARE_UPDATE_STATE_CANCELLED;
        s_status.last_error = ESP_OK;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }
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
