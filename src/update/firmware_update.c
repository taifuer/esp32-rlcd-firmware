#include "firmware_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_settings.h"
#include "audio_music.h"
#include "boot_recovery.h"
#include "clock_service.h"
#include "conversation_config.h"
#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
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
#include "gallery_download.h"
#include "network_credentials.h"
#include "network_time.h"
#include "sd_image.h"
#include "settings_portal_policy.h"
#include "weather_config.h"
#include "weather_location_catalog.h"
#include "weather_service.h"

#define UPDATE_EVENT_COMPLETE BIT1
#define UPDATE_EVENT_FAILED BIT2
#define UPDATE_EVENT_CANCEL BIT3
#define UPDATE_EVENT_REPROVISION BIT4
#define UPDATE_EVENT_MUTATION_ENDED BIT5
#define UPDATE_EVENT_GALLERY_INSTALL BIT6
#define UPDATE_EVENT_WIFI_CHANGED BIT7
#define UPDATE_EVENT_WEATHER_REFRESH BIT8
#define UPDATE_EVENT_SESSION                                                    \
    (UPDATE_EVENT_COMPLETE | UPDATE_EVENT_FAILED | UPDATE_EVENT_CANCEL |       \
     UPDATE_EVENT_REPROVISION | UPDATE_EVENT_GALLERY_INSTALL |                 \
     UPDATE_EVENT_WIFI_CHANGED | UPDATE_EVENT_WEATHER_REFRESH)
#define UPDATE_EVENT_ALL                                                       \
    (UPDATE_EVENT_SESSION | UPDATE_EVENT_MUTATION_ENDED)

#define SETTINGS_WINDOW_MS 300000U
#define SETTINGS_ACTIVE_REQUEST_GRACE_MS 35000U
#define UPDATE_RESTART_DELAY_MS 1800U
#define UPDATE_SERVER_STOP_DELAY_MS 250U
#define SETTINGS_WIFI_SUCCESS_DISPLAY_MS 1200U
#define UPDATE_HTTP_BUFFER_SIZE 4096U
#define SETTINGS_IMAGE_UPLOAD_BYTES \
    (MONO_IMAGE_BITMAP_BYTES + sizeof("P4\n400 300\n") - 1U)
#define SETTINGS_PORTAL_FORM_CAPACITY (APP_SETTINGS_FORM_MAX_LENGTH + 1U)
#define SETTINGS_PORTAL_SMALL_FORM_CAPACITY 64U
#define SETTINGS_WIFI_FORM_CAPACITY 385U
#define SETTINGS_CONVERSATION_FORM_CAPACITY \
    (CONVERSATION_CONFIG_FORM_MAX_LENGTH + 1U)
#define SETTINGS_WEATHER_FORM_CAPACITY \
    (WEATHER_CONFIG_FORM_MAX_LENGTH + 1U)
#define SETTINGS_WEATHER_REGIONS_JSON_CAPACITY 8192U
#define SETTINGS_WIFI_VALIDATION_TIMEOUT_MS 15000U
#define SETTINGS_IMAGE_FORM_CAPACITY 96U
#define SETTINGS_IMAGE_LIST_JSON_CAPACITY                                  \
    (128U + SD_IMAGE_MAX_IMAGES * (SD_IMAGE_FILENAME_CAPACITY + 3U))
#define SETTINGS_PORTAL_TOKEN_HEADER "X-RLCD-Token"
#define SETTINGS_JSON_ESCAPE_CAPACITY(maximum_length) \
    ((maximum_length) * 6U + 1U)
#define SETTINGS_STATE_JSON_CAPACITY 6144U
#define SETTINGS_CONVERSATION_SERVICE_ALIYUN_REALTIME "aliyun_realtime"
#define SETTINGS_CONVERSATION_MODEL_QWEN3_OMNI                              \
    "qwen3-omni-flash-realtime"
#define SETTINGS_CONVERSATION_MODEL_QWEN_AUDIO_3                           \
    "qwen-audio-3.0-realtime-flash"

static const char *TAG = "firmware_update";
static const char SETTINGS_AP_BASE_NAME[] = "ESP32-RLCD-SETTINGS";
static const char SETTINGS_URL[] = "http://192.168.4.1";
static const char EXPECTED_PROJECT_NAME[] = "rlcd_firmware";

static const char RECOVERY_SETTINGS_PAGE[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>固件恢复</title><style>"
    ":root{color-scheme:light;font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#171717;background:#f5f6f7}"
    "*{box-sizing:border-box}body{margin:0}main{width:calc(100% - 2rem);max-width:34rem;margin:0 auto;padding:2rem 0 3rem}"
    "header{padding:.4rem 0 1rem}h1{font-size:1.85rem;margin:0 0 .55rem}h2{font-size:1.15rem;margin:0 0 .9rem}"
    "p{line-height:1.55;margin:.45rem 0;color:#555}section{background:#fff;border:1px solid #e2e4e7;border-radius:.8rem;padding:1.15rem;margin:1rem 0}"
    "label{display:block;font-weight:600;margin:.9rem 0 .35rem}input,button{width:100%;font:inherit;border-radius:.55rem;padding:.72rem .8rem;border:1px solid #a9adb2;background:#fff;min-height:44px}"
    "button{margin-top:.9rem;border-color:#171717;background:#171717;color:#fff;font-weight:650}button.secondary,button.danger{background:#fff;color:#171717}"
    "button.danger{color:#a32626;border-color:#c96c6c}button:disabled,input:disabled{opacity:.48}.note{font-size:.88rem;color:#676b70}.message{min-height:1.5rem;margin-top:.75rem;color:#333}"
    ".summary{display:flex;justify-content:space-between;align-items:baseline;gap:1rem;padding:.8rem .9rem;border-radius:.55rem;background:#f2f3f4}.summary span{color:#676b70}.summary strong{overflow-wrap:anywhere;text-align:right}"
    ".form{margin-top:.9rem}.form[hidden]{display:none}.check{display:flex;align-items:center;gap:.55rem}.check input{width:auto;margin:0;padding:0;min-height:0}.row{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.65rem}.row button{margin-top:.7rem}"
    "progress{width:100%;height:1rem;margin-top:1rem}@media(max-width:26rem){.row{grid-template-columns:1fr}.summary{align-items:flex-start;flex-direction:column;gap:.25rem}.summary strong{text-align:left}}"
    "footer{text-align:center;margin-top:1.5rem;font-size:.85rem;color:#676b70}footer a{color:inherit;text-decoration:none}footer a:hover{text-decoration:underline}"
    "</style></head><body><main><header><h1>固件恢复</h1>"
    "<p>恢复模式仅启动联网和升级所需功能。修复完成后，关闭电源再正常开机。</p></header>"
    "<section><h2>Wi-Fi</h2><div class=\"summary\"><span id=\"wifiLabel\">已保存网络</span><strong id=\"wifiName\">正在读取…</strong></div>"
    "<button id=\"wifiEdit\" type=\"button\" class=\"secondary\">更换 Wi-Fi</button>"
    "<form id=\"wifiForm\" class=\"form\" autocomplete=\"off\" hidden><label for=\"wifiSsid\">Wi-Fi 名称</label><input id=\"wifiSsid\" maxlength=\"32\" required>"
    "<label for=\"wifiPassword\">Wi-Fi 密码</label><input id=\"wifiPassword\" type=\"password\" maxlength=\"63\" autocomplete=\"new-password\">"
    "<label class=\"check\"><input id=\"showWifiPassword\" type=\"checkbox\">显示密码</label><label class=\"check\"><input id=\"openWifi\" type=\"checkbox\">这是开放网络</label>"
    "<p class=\"note\">仅支持 2.4 GHz。新网络验证成功后才会替换原配置。</p><div class=\"row\"><button type=\"submit\">连接并保存</button><button id=\"wifiCancel\" type=\"button\" class=\"secondary\">取消</button></div></form>"
    "<p id=\"wifiMessage\" class=\"message\" role=\"status\"></p><button id=\"forgetWifi\" type=\"button\" class=\"danger\">移除已保存的 Wi-Fi</button></section>"
    "<section><h2>本地固件升级</h2><p>请选择本项目发布的 <strong>OTA 固件</strong>。写入期间请保持供电。</p>"
    "<input id=\"file\" type=\"file\" accept=\".bin,application/octet-stream\"><button id=\"upload\" type=\"button\">开始升级</button>"
    "<progress id=\"progress\" max=\"100\" value=\"0\"></progress><p id=\"updateMessage\" class=\"message\">等待选择固件</p></section>"
    "<footer>© <span id=\"portalYear\">2026</span> <a href=\"https://mcu.taifua.com/\" target=\"_blank\" rel=\"noopener noreferrer\">ESP32 固件</a></footer>"
    "<script>document.getElementById('portalYear').textContent=String(new Date().getFullYear());let token='',wifiConfigured=false,wifiBusy=false;const $=id=>document.getElementById(id);const show=(id,text)=>{$(id).textContent=text};"
    "function controls(){const open=$('openWifi').checked;$('wifiEdit').disabled=wifiBusy;$('wifiSsid').disabled=wifiBusy;$('wifiPassword').disabled=wifiBusy||open;$('showWifiPassword').disabled=wifiBusy||open;$('openWifi').disabled=wifiBusy;$('forgetWifi').disabled=wifiBusy||!wifiConfigured}"
    "function edit(value){$('wifiForm').hidden=!value;if(value){$('wifiPassword').value='';$('wifiSsid').focus()}}"
    "async function post(path,body){const response=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-RLCD-Token':token},body});const text=await response.text();if(!response.ok)throw new Error(text||'操作失败');return text}"
    "async function load(){const response=await fetch('/api/state',{cache:'no-store'});if(!response.ok)throw new Error('无法读取恢复状态');const state=await response.json();token=state.token;wifiConfigured=state.wifi_configured===true;$('wifiLabel').textContent=wifiConfigured?'已保存网络':'网络状态';$('wifiName').textContent=wifiConfigured?state.wifi_ssid:(state.wifi_readable?'尚未配置':'无法读取，可更换网络或升级');$('wifiSsid').value=wifiConfigured?state.wifi_ssid:'';controls()}"
    "$('wifiEdit').onclick=()=>edit(true);$('wifiCancel').onclick=()=>{edit(false);show('wifiMessage','')};$('showWifiPassword').onchange=()=>{$('wifiPassword').type=$('showWifiPassword').checked?'text':'password'};"
    "$('openWifi').onchange=()=>{if($('openWifi').checked){$('wifiPassword').value='';$('showWifiPassword').checked=false;$('wifiPassword').type='password'}controls()};"
    "$('wifiForm').onsubmit=async event=>{event.preventDefault();if(wifiBusy)return;const ssid=$('wifiSsid').value,password=$('wifiPassword').value,open=$('openWifi').checked;if(!ssid||new TextEncoder().encode(ssid).length>32){show('wifiMessage','Wi-Fi 名称应为 1—32 字节。');return}if(!open&&!/^[\\x20-\\x7e]{8,63}$/.test(password)){show('wifiMessage','请输入 8—63 位英文字符、数字或符号。');return}wifiBusy=true;controls();show('wifiMessage','正在验证新网络…');try{show('wifiMessage',await post('/api/wifi/change','ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(open?'':password)))}catch(error){show('wifiMessage',error.name==='TypeError'?'设置热点已关闭，请查看设备屏幕。':error.message);wifiBusy=false;controls()}};"
    "$('forgetWifi').onclick=async()=>{if(!wifiConfigured||!confirm('移除已保存的 Wi-Fi？'))return;wifiBusy=true;controls();show('wifiMessage','正在移除…');try{show('wifiMessage',await post('/api/wifi/clear','confirm=FORGET'))}catch(error){show('wifiMessage',error.name==='TypeError'?'设置热点已关闭，请重新配网。':error.message);wifiBusy=false;controls()}};"
    "$('upload').onclick=()=>{const file=$('file'),button=$('upload'),progress=$('progress');if(!file.files.length){show('updateMessage','请先选择 OTA 固件');return}if(!confirm('开始写入固件？'))return;button.disabled=true;file.disabled=true;const request=new XMLHttpRequest();request.open('POST','/update');request.setRequestHeader('Content-Type','application/octet-stream');request.setRequestHeader('X-RLCD-Token',token);request.upload.onprogress=event=>{if(event.lengthComputable){const value=Math.round(event.loaded*100/event.total);progress.value=value;show('updateMessage','正在上传 '+value+'%')}};request.onload=()=>{show('updateMessage',request.responseText||(request.status===200?'升级成功，设备即将重启':'升级失败'));if(request.status!==200){button.disabled=false;file.disabled=false}};request.onerror=()=>show('updateMessage','连接中断，请查看设备屏幕');request.send(file.files[0])};"
    "load().catch(error=>show('wifiMessage',error.message));</script></main></body></html>";

static const char SETTINGS_PAGE[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>设备设置</title><style>"
    ":root{color-scheme:light;font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "color:#171717;background:#f5f6f7}*{box-sizing:border-box}body{margin:0}main{width:calc(100% - 2rem);max-width:34rem;"
    "margin:0 auto;padding:2rem 0 3rem}header{padding:.4rem 0 1rem}h1{font-size:1.85rem;margin:0 0 .55rem}"
    "h2{font-size:1.15rem;margin:0 0 .9rem}h3{font-size:1rem;margin:0 0 .65rem}p{line-height:1.55;margin:.45rem 0;color:#555}section{background:#fff;"
    "border:1px solid #e2e4e7;border-radius:.8rem;padding:1.15rem;margin:1rem 0}label{display:block;font-weight:600;"
    "margin:.9rem 0 .35rem}input,select,button{width:100%;font:inherit;border-radius:.55rem;padding:.72rem .8rem;"
    "border:1px solid #a9adb2;background:#fff;min-height:44px}input[type=range]{padding:.35rem 0;border:0;min-height:0}.days{display:grid;"
    "grid-template-columns:repeat(7,minmax(0,1fr));gap:.35rem}.day{display:flex;flex-direction:column;align-items:center;"
    "gap:.35rem;margin:0;padding:.55rem .2rem;border:1px solid #d7dade;border-radius:.55rem;font-weight:500}.day input{width:auto;margin:0;padding:0;min-height:0;accent-color:#171717}button{margin-top:.9rem;"
    "border-color:#171717;background:#171717;color:#fff;font-weight:650}button.secondary{background:#fff;color:#171717}"
    "button.danger{background:#fff;color:#a32626;border-color:#c96c6c}small,.note{font-size:.88rem;color:#676b70}"
    ".row{display:grid;grid-template-columns:1fr auto;gap:.75rem;align-items:center}.row output{min-width:3ch;text-align:right}"
    "progress{width:100%;height:1rem;margin-top:1rem}.message{min-height:1.5rem;margin-top:.75rem;color:#333}"
    "button:disabled,input:disabled,select:disabled{cursor:not-allowed;opacity:.48}.sd-state{padding:.75rem .85rem;border-radius:.55rem;background:#f2f3f4;color:#333}"
    ".sd-state[data-state=ready]{background:#eef6f0;color:#235b31}.sd-state[data-state=full]{background:#fff5df;color:#6e4c0c}"
    ".image-preview{margin-top:1rem}.canvas-shell{position:relative;overflow:hidden;"
    "border:1px solid #cfd2d6;border-radius:.55rem;background:#000;aspect-ratio:4/3}.canvas-shell canvas{display:block;width:100%;height:100%;image-rendering:pixelated}"
    ".reserved{position:absolute;left:0;right:0;bottom:0;height:16.6667%;display:grid;place-items:center;border-top:1px dashed #8a8a8a;"
    "color:#ddd;background:rgba(0,0,0,.72);font-size:.72rem;letter-spacing:.02em;pointer-events:none}.advanced{margin-top:.9rem;"
    "border:1px solid #e2e4e7;border-radius:.55rem;padding:.7rem .8rem}.advanced summary{cursor:pointer;font-weight:600}.advanced label{font-weight:500}"
    ".check{display:flex;align-items:center;gap:.55rem}.check input{width:auto;margin:0;padding:0;min-height:0;accent-color:#171717}.divider{border:0;border-top:1px solid #e5e7e9;margin:1.15rem 0}"
    ".stored-manager{margin-top:1rem}.image-meta{display:flex;align-items:baseline;justify-content:space-between;gap:.8rem;margin:.65rem 0 0}"
    ".image-meta strong{white-space:nowrap}.image-meta small{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;text-align:right}"
    ".button-row{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.65rem}.button-row button{margin-top:.7rem}"
    ".wifi-summary{display:flex;justify-content:space-between;align-items:baseline;gap:1rem;padding:.8rem .9rem;"
    "border-radius:.55rem;background:#f2f3f4}.wifi-summary span{color:#676b70}.wifi-summary strong{min-width:0;overflow-wrap:anywhere;text-align:right}"
    ".wifi-form{margin-top:.9rem;padding-top:.1rem}.wifi-form[hidden]{display:none}.compact{margin-top:.7rem}"
    ".source-link{color:#255da8;text-decoration:none}.source-link:hover{text-decoration:underline}"
    ".section-toggle{cursor:pointer;font-size:1.15rem;font-weight:650;line-height:1.5}.section-toggle span{float:right;margin-left:.75rem;font-size:.88rem;font-weight:400;color:#676b70}"
    "details[open]>.section-toggle{margin-bottom:.9rem}.section-note{margin-top:.75rem}footer{text-align:center;margin-top:1.5rem;font-size:.85rem;color:#676b70}"
    "footer a{color:inherit;text-decoration:none}footer a:hover{text-decoration:underline}"
    "@media(max-width:26rem){.button-row{grid-template-columns:1fr}.wifi-summary{align-items:flex-start;flex-direction:column;gap:.25rem}.wifi-summary strong{text-align:left}}"
    "</style></head><body><main><header><h1>设备设置</h1>"
    "<p>普通设置保存后立即生效，无需重启。临时热点最多开放 5 分钟。</p></header>"
    "<section><h2>Wi-Fi</h2><div class=\"wifi-summary\"><span id=\"wifiLabel\">已保存网络</span>"
    "<strong id=\"wifiName\">正在读取…</strong></div>"
    "<p id=\"wifiStatus\" class=\"note\">设置期间设备通过临时热点提供本页面。</p>"
    "<button id=\"wifiEdit\" type=\"button\" class=\"secondary\">更换 Wi-Fi</button>"
    "<form id=\"wifiForm\" class=\"wifi-form\" autocomplete=\"off\" hidden>"
    "<label for=\"wifiSsid\">Wi-Fi 名称</label><input id=\"wifiSsid\" maxlength=\"32\" required>"
    "<label for=\"wifiPassword\">Wi-Fi 密码</label><input id=\"wifiPassword\" type=\"password\" maxlength=\"63\" autocomplete=\"new-password\">"
    "<label class=\"check compact\"><input id=\"showWifiPassword\" type=\"checkbox\">显示密码</label>"
    "<label class=\"check compact\"><input id=\"openWifi\" type=\"checkbox\">这是开放网络</label>"
    "<p class=\"note\">仅支持 2.4 GHz。密码不会显示或预填；受保护网络请重新输入 8—63 位英文字符、数字或符号。</p>"
    "<div class=\"button-row\"><button id=\"wifiSave\" type=\"submit\">连接并保存</button>"
    "<button id=\"wifiCancel\" type=\"button\" class=\"secondary\">取消</button></div></form>"
    "<p id=\"wifiMessage\" class=\"message\" role=\"status\" aria-live=\"polite\"></p>"
    "<details class=\"advanced\"><summary>移除已保存的 Wi-Fi</summary>"
    "<p class=\"note\">移除后将关闭设置热点并进入首次配网；时间、偏好和图片不会被删除，设备不会重启。</p>"
    "<button id=\"forgetWifi\" type=\"button\" class=\"danger\">移除 Wi-Fi</button></details></section>"
    "<section><h2>常用设置</h2><form id=\"settings\">"
    "<label for=\"timezone\">时区</label><select id=\"timezone\" name=\"timezone\"></select>"
    "<label for=\"unit\">温度单位</label><select id=\"unit\" name=\"unit\">"
    "<option value=\"c\">摄氏度（°C）</option><option value=\"f\">华氏度（°F）</option></select>"
    "<label for=\"volume\">播放音量</label><div class=\"row\">"
    "<input id=\"volume\" name=\"volume\" type=\"range\" min=\"0\" max=\"100\" step=\"1\">"
    "<output id=\"volumeValue\">--</output></div>"
    "<p class=\"note\">用于音乐、对话和闹钟等扬声器输出；0% 静音。</p>"
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
    "<details class=\"advanced\"><summary>高级设置</summary>"
    "<label for=\"updates\">Beta 更新</label><select id=\"updates\" name=\"updates\">"
    "<option value=\"stable\">关闭（默认）</option><option value=\"beta\">开启（开发者）</option></select>"
    "<p class=\"note\">测试固件可能不稳定，仅适合能使用本地 OTA 或 USB 恢复的开发者；安装仍需在设备上确认。关闭后不会自动降级。</p>"
    "<button id=\"defaults\" type=\"button\" class=\"secondary\">恢复偏好默认值</button>"
    "<p id=\"maintenanceMessage\" class=\"message\" role=\"status\" aria-live=\"polite\"></p></details>"
    "<button type=\"submit\">保存设置</button></form>"
    "<p id=\"settingsMessage\" class=\"message\" role=\"status\" aria-live=\"polite\"></p></section>"
    "<section><details><summary class=\"section-toggle\">天气<span id=\"weatherStatus\">正在读取…</span></summary>"
    "<form id=\"weatherForm\" autocomplete=\"off\">"
    "<label for=\"weatherEnabled\">天气页面</label><select id=\"weatherEnabled\" name=\"enabled\">"
    "<option value=\"off\">关闭（默认）</option><option value=\"on\">开启</option></select>"
    "<label for=\"weatherApiHost\">QWeather API Host</label>"
    "<input id=\"weatherApiHost\" name=\"api_host\" maxlength=\"127\" placeholder=\"abcxyz.qweatherapi.com\" "
    "autocomplete=\"off\" autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\">"
    "<p class=\"note\">填写 QWeather 控制台分配的专属 Host，不含 https://、路径或端口。</p>"
    "<label for=\"weatherKey\">API Key</label><input id=\"weatherKey\" name=\"api_key\" type=\"text\" "
    "maxlength=\"256\" autocomplete=\"off\" autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\">"
    "<p class=\"note\">请直接粘贴；保存后立即清空且不会再次显示或预填。已有配置时留空将保留当前 API Key。</p>"
    "<label for=\"weatherProvince\">省份</label><select id=\"weatherProvince\" name=\"province\"></select>"
    "<label for=\"weatherCity\">城市</label><select id=\"weatherCity\" name=\"city\"></select>"
    "<p class=\"note\">先选省份，再选择所在城市。</p>"
    "<p class=\"note\">设备使用你的凭据直连 QWeather，不经过项目服务器；请求可能计入你的账户用量。"
    "数据服务：<a class=\"source-link\" href=\"https://www.qweather.com/\" target=\"_blank\" rel=\"noopener noreferrer\">和风天气 QWeather</a>。</p>"
    "<p class=\"note\">当前固件未启用 Flash/NVS 加密；建议使用本设备专用、最小权限且可随时撤销的 Key。</p>"
    "<button id=\"weatherSave\" type=\"submit\">保存并获取天气</button></form>"
    "<p id=\"weatherMessage\" class=\"message\" role=\"status\" aria-live=\"polite\"></p>"
    "<details class=\"advanced\"><summary>清除天气配置</summary>"
    "<p class=\"note\">清除 API Host、API Key、位置和本地天气缓存；其他设置不会改变，设备无需重启。</p>"
    "<button id=\"weatherClear\" type=\"button\" class=\"danger\">确认清除天气配置</button></details></details></section>"
    "<section><details><summary class=\"section-toggle\">AI 对话 Beta<span id=\"conversationStatus\">正在读取…</span></summary>"
    "<form id=\"conversationForm\" autocomplete=\"off\">"
    "<input type=\"hidden\" name=\"service\" value=\""
    SETTINGS_CONVERSATION_SERVICE_ALIYUN_REALTIME "\">"
    "<label for=\"conversationEnabled\">AI 对话</label><select id=\"conversationEnabled\" name=\"enabled\">"
    "<option value=\"off\">关闭（默认）</option><option value=\"on\">开启</option></select>"
    "<label for=\"conversationKey\">API Key</label><input id=\"conversationKey\" name=\"api_key\" type=\"text\" "
    "maxlength=\"256\" autocomplete=\"off\" autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\">"
    "<p class=\"note\">请直接粘贴；内容仅在本次编辑时可见，保存后立即清空且不会再次显示或预填。已有配置时留空将保留当前 API Key。</p>"
    "<p class=\"note\">关闭 AI 对话不会删除 API Key；未配置、断网或省电时仍使用离线语音指令。</p>"
    "<details class=\"advanced\"><summary>高级设置</summary>"
    "<label for=\"conversationModel\">对话模型</label><select id=\"conversationModel\" name=\"model\">"
    "<option value=\"" SETTINGS_CONVERSATION_MODEL_QWEN3_OMNI
    "\">Qwen3 Omni Flash Realtime（默认）</option>"
    "<option value=\"" SETTINGS_CONVERSATION_MODEL_QWEN_AUDIO_3
    "\">Qwen Audio 3.0 Realtime Flash</option></select>"
    "<p class=\"note\">仅可选择固件已验证的阿里云百炼 Realtime 模型。</p>"
    "<label for=\"conversationApiHost\">API Host（可选）</label>"
    "<input id=\"conversationApiHost\" name=\"api_host\" maxlength=\"127\" "
    "placeholder=\"llm-xxx.cn-beijing.maas.aliyuncs.com\" autocomplete=\"off\">"
    "<p class=\"note\">留空使用北京共享服务；也可填写受支持的新加坡共享或 Workspace 专属 API Host。不要包含 wss://、路径或端口。</p></details>"
    "<p class=\"note\">使用 AI 对话时，麦克风音频会发送至阿里云百炼，识别文本与回复由服务端生成，并可能产生费用。</p>"
    "<p class=\"note\">当前固件未启用 Flash/NVS 加密；具备设备物理访问能力的人可能读取已保存的 API Key。建议使用本设备专用、最小权限且可随时撤销的 Key。</p>"
    "<button id=\"conversationSave\" type=\"submit\">保存 AI 对话配置</button></form>"
    "<p id=\"conversationMessage\" class=\"message\" role=\"status\" aria-live=\"polite\"></p>"
    "<details class=\"advanced\"><summary>清除 AI 对话配置</summary>"
    "<p class=\"note\">清除已保存的 API Key，关闭 AI 对话并恢复默认模型与共享 API Host；其他设备设置不会改变，也无需重启。</p>"
    "<button id=\"conversationClear\" type=\"button\" class=\"danger\">确认清除 AI 对话配置</button></details></details></section>"
    "<section><h2>日期与时间</h2><p>无需互联网，使用当前手机时间校准设备 RTC。</p>"
    "<button id=\"setTime\" type=\"button\" class=\"secondary\">使用手机时间校准</button>"
    "<p id=\"timeMessage\" class=\"message\" role=\"status\" aria-live=\"polite\"></p></section>"
    "<section><h2>microSD 图片</h2><p id=\"sdState\" class=\"sd-state\" data-state=\"loading\" aria-live=\"polite\">正在检测 microSD…</p>"
    "<div id=\"storedManager\" class=\"stored-manager\" hidden><div class=\"canvas-shell\">"
    "<canvas id=\"storedCanvas\" width=\"400\" height=\"300\" aria-label=\"microSD 当前图片预览\"></canvas>"
    "<div class=\"reserved\">设备保留操作区 · 50 px</div></div>"
    "<div class=\"image-meta\"><strong id=\"storedPosition\">-- / --</strong><small id=\"storedFilename\"></small></div>"
    "<div class=\"button-row\"><button id=\"storedPrevious\" type=\"button\" class=\"secondary\">上一张</button>"
    "<button id=\"storedNext\" type=\"button\" class=\"secondary\">下一张</button></div>"
    "<div class=\"button-row\"><button id=\"storedSelect\" type=\"button\">设为当前</button>"
    "<button id=\"storedDelete\" type=\"button\" class=\"danger\">删除</button></div></div>"
    "<p id=\"storedMessage\" class=\"message\"></p><hr class=\"divider\">"
    "<label for=\"imageFile\">从手机选择图片</label><input id=\"imageFile\" type=\"file\" accept=\"image/jpeg,image/png,.jpg,.jpeg,.png\" disabled>"
    "<p class=\"note\">JPEG/PNG 只在当前浏览器中转换，不会上传原图。支持最大 32 MiB、4000 万像素。</p>"
    "<div id=\"imagePreview\" class=\"image-preview\" hidden><div class=\"canvas-shell\">"
    "<canvas id=\"imageCanvas\" width=\"400\" height=\"300\" aria-label=\"图片在设备上的黑白预览\"></canvas>"
    "<div class=\"reserved\">设备保留操作区 · 50 px</div></div>"
    "<details class=\"advanced\"><summary>高级转换设置</summary><label for=\"threshold\">黑白阈值</label>"
    "<div class=\"row\"><input id=\"threshold\" type=\"range\" min=\"1\" max=\"254\" value=\"128\" step=\"1\">"
    "<output id=\"thresholdValue\">128</output></div><label class=\"check\"><input id=\"dither\" type=\"checkbox\">"
    "Floyd–Steinberg 抖动</label></details></div>"
    "<button id=\"imageUpload\" type=\"button\" disabled>写入 microSD</button>"
    "<progress id=\"imageProgress\" max=\"100\" value=\"0\" hidden></progress>"
    "<p id=\"imageMessage\" class=\"message\">请先选择图片</p><hr class=\"divider\">"
    "<h3>演示图集</h3><p>将公共演示图下载到 microSD。需要设备已保存可用的 Wi-Fi。</p>"
    "<button id=\"starterImages\" type=\"button\" class=\"secondary\" disabled>安装演示图集</button>"
    "<p id=\"starterMessage\" class=\"message\"></p>"
    "<small>若当前页面无法选择图片，请在手机系统浏览器中打开 192.168.4.1。</small></section>"
    "<section><details><summary class=\"section-toggle\">microSD 音乐</summary>"
    "<p>关机取卡后，用读卡器把 MP3 或 16 位 PCM WAV 复制到 <code>rlcd/music/</code>，再插回设备开机。</p>"
    "<p class=\"note\">最多显示 32 首，单个文件不超过 256 MiB。不支持热插拔；无卡不影响其他功能。</p>"
    "<p class=\"note\">音乐页短按 KEY 播放或暂停，按住 2 秒下一首；按住 BOOT 2 秒调整音量，短按返回时钟并继续播放。</p>"
    "</details></section>"
    "<section><details><summary class=\"section-toggle\">本地固件升级</summary><p>请选择本项目发布的 <strong>OTA 固件</strong>。升级不会清除设置。</p>"
    "<input id=\"file\" type=\"file\" accept=\".bin,application/octet-stream\">"
    "<button id=\"upload\" type=\"button\">开始升级</button>"
    "<progress id=\"progress\" max=\"100\" value=\"0\"></progress>"
    "<p id=\"updateMessage\" class=\"message\">等待选择固件</p>"
    "<small>写入期间请保持设备供电。校验成功后设备会自动重启。</small></details></section>"
    "<footer>© <span id=\"portalYear\">2026</span> <a href=\"https://mcu.taifua.com/\" target=\"_blank\" rel=\"noopener noreferrer\">ESP32 固件</a></footer>"
    "<script>document.getElementById('portalYear').textContent=String(new Date().getFullYear());let token='',initialUpdates='stable',settingsBusy=false,wifiConfigured=false,savedWifi='',wifiBusy=false,weatherAvailable=false,weatherConfigured=false,weatherEnabled=false,initialWeatherEnabled=false,weatherBusy=false,weatherRegionRequest=0,conversationAvailable=false,conversationConfigured=false,conversationEnabled=false,conversationBusy=false,sdReady=false,imageBusy=false,imageGray=null,imagePbm=null,imageFrame=0,"
    "storedImages=[],storedIndex=0,storedSelected='',storedBusy=false,storedRequest=0;"
    "const $=id=>document.getElementById(id);const IMAGE_WIDTH=400,IMAGE_HEIGHT=300,CONTENT_HEIGHT=250;"
    "const SOURCE_MAX_BYTES=32*1024*1024,SOURCE_MAX_PIXELS=40000000;"
    "const show=(id,text)=>{$(id).textContent=text};const alarmDays=()=>document.querySelectorAll('.alarm-day');"
    "const unix=()=>String(Math.floor(Date.now()/1000));"
    "function wifiControls(){const open=$('openWifi').checked;$('wifiForm').setAttribute('aria-busy',wifiBusy?'true':'false');"
    "$('wifiEdit').disabled=wifiBusy;$('wifiSsid').disabled=wifiBusy;$('wifiPassword').disabled=wifiBusy||open;"
    "$('showWifiPassword').disabled=wifiBusy||open;$('openWifi').disabled=wifiBusy;$('wifiSave').disabled=wifiBusy;"
    "$('wifiCancel').disabled=wifiBusy;$('forgetWifi').disabled=wifiBusy||!wifiConfigured}"
    "function setWifiState(configured,ssid){wifiConfigured=configured===true;savedWifi=wifiConfigured&&typeof ssid==='string'?ssid:'';"
    "$('wifiLabel').textContent=wifiConfigured?'已保存网络':'网络状态';$('wifiName').textContent=wifiConfigured?savedWifi:'尚未配置';"
    "$('wifiStatus').textContent=wifiConfigured?'设置期间家庭 Wi-Fi 暂停；退出设置后设备会按需连接。':'尚未保存家庭 Wi-Fi，可直接在这里完成配置。';"
    "$('wifiEdit').textContent=wifiConfigured?'更换 Wi-Fi':'配置 Wi-Fi';if($('wifiForm').hidden)$('wifiSsid').value=savedWifi;wifiControls()}"
    "function wifiEditing(value){$('wifiForm').hidden=!value;if(value){$('wifiSsid').value=savedWifi;$('wifiPassword').value='';"
    "$('openWifi').checked=false;$('showWifiPassword').checked=false;$('wifiPassword').type='password';$('wifiSsid').focus()}wifiControls()}"
    "function weatherControls(){const blocked=weatherBusy||!weatherAvailable,enabled=$('weatherEnabled').value==='on',hasProvince=$('weatherProvince').value!=='';"
    "$('weatherForm').setAttribute('aria-busy',weatherBusy?'true':'false');$('weatherEnabled').disabled=blocked;$('weatherApiHost').disabled=blocked;"
    "$('weatherKey').disabled=blocked;$('weatherProvince').disabled=blocked;$('weatherCity').disabled=blocked||!hasProvince;"
    "$('weatherSave').disabled=blocked;$('weatherSave').textContent=enabled?'保存并获取天气':'保存天气设置';$('weatherClear').disabled=blocked;"
    "$('weatherApiHost').required=enabled;$('weatherProvince').required=enabled;$('weatherCity').required=enabled}"
    "function weatherOptions(select,items,placeholder){while(select.firstChild)select.removeChild(select.firstChild);const empty=document.createElement('option');"
    "empty.value='';empty.textContent=placeholder;select.appendChild(empty);items.forEach(item=>{const option=document.createElement('option');"
    "option.value=String(item.id);option.textContent=item.name;select.appendChild(option)})}"
    "async function weatherRegions(province){const suffix=province?'?province='+encodeURIComponent(province):'',response=await fetch('/api/weather/regions'+suffix,{cache:'no-store'});"
    "if(!response.ok)throw new Error(await response.text()||'无法读取省市列表');const payload=await response.json();if(!payload||!Array.isArray(payload.items)||"
    "payload.items.some(item=>!item||!Number.isInteger(item.id)||item.id<=0||typeof item.name!=='string'))throw new Error('设备返回的省市列表无效');return payload.items}"
    "async function setWeatherState(state){const weather=state&&typeof state==='object'?state:null;weatherAvailable=weather!==null&&weather.available===true&&"
    "typeof weather.enabled==='boolean'&&typeof weather.configured==='boolean'&&typeof weather.key_saved==='boolean'&&typeof weather.api_host==='string'&&"
    "Number.isInteger(weather.province_id)&&Number.isInteger(weather.city_id);weatherConfigured=weatherAvailable&&weather.configured;"
    "weatherEnabled=weatherAvailable&&weather.enabled;initialWeatherEnabled=weatherEnabled;$('weatherStatus').textContent=!weatherAvailable?'暂不可用':"
    "weatherEnabled?(weatherConfigured?'已开启':'需要完善配置'):(weather&&weather.key_saved?'已关闭':'未配置');$('weatherEnabled').value=weatherEnabled?'on':'off';"
    "$('weatherApiHost').value=weatherAvailable?weather.api_host:'';$('weatherKey').value='';$('weatherKey').placeholder=weatherAvailable&&weather.key_saved?'已保存，留空保留':'粘贴 API Key';"
    "const requestId=++weatherRegionRequest;const provinces=await weatherRegions('');if(requestId!==weatherRegionRequest)return;"
    "weatherOptions($('weatherProvince'),provinces,'请选择省份');const province=weatherAvailable&&weather.province_id>0?String(weather.province_id):'';"
    "$('weatherProvince').value=province;if($('weatherProvince').selectedIndex<0)$('weatherProvince').value='';const selectedProvince=$('weatherProvince').value;"
    "const cities=selectedProvince?await weatherRegions(selectedProvince):[];if(requestId!==weatherRegionRequest)return;weatherOptions($('weatherCity'),cities,selectedProvince?'请选择城市':'请先选择省份');"
    "$('weatherCity').value=weatherAvailable&&weather.city_id>0?String(weather.city_id):'';if($('weatherCity').selectedIndex<0)$('weatherCity').value='';"
    "show('weatherMessage',weatherAvailable?'':'天气设置暂不可用；其他设备设置仍可正常使用。');weatherControls()}"
    "function conversationControls(){const blocked=conversationBusy||!conversationAvailable;$('conversationForm').setAttribute('aria-busy',conversationBusy?'true':'false');"
    "$('conversationEnabled').disabled=blocked;$('conversationModel').disabled=blocked;$('conversationApiHost').disabled=blocked;$('conversationKey').disabled=blocked;"
    "$('conversationSave').disabled=blocked;$('conversationClear').disabled=blocked}"
    "function setConversationState(state){const cloud=state&&typeof state==='object'?state:null;conversationAvailable=cloud!==null&&cloud.available===true&&cloud.service==='" SETTINGS_CONVERSATION_SERVICE_ALIYUN_REALTIME "'&&typeof cloud.configured==='boolean'&&typeof cloud.enabled==='boolean'&&typeof cloud.model==='string'&&typeof cloud.api_host==='string'&&typeof cloud.shared_endpoint==='boolean';"
    "conversationConfigured=conversationAvailable&&cloud.configured===true;conversationEnabled=conversationConfigured&&cloud.enabled===true;$('conversationStatus').textContent=!conversationAvailable?'暂不可用':!conversationConfigured?'未配置':conversationEnabled?'已开启':'已关闭';"
    "$('conversationEnabled').value=conversationEnabled?'on':'off';"
    "const modelSelect=$('conversationModel');modelSelect.value=conversationAvailable?cloud.model:'';if(modelSelect.selectedIndex<0)modelSelect.value='" SETTINGS_CONVERSATION_MODEL_QWEN3_OMNI "';$('conversationApiHost').value=conversationAvailable&&!(cloud.shared_endpoint&&cloud.api_host==='" CONVERSATION_DEFAULT_API_HOST "')?cloud.api_host:'';$('conversationKey').value='';"
    "show('conversationMessage',conversationAvailable?'':'AI 对话设置暂不可用；其他设备设置仍可正常使用。');conversationControls()}"
    "function storedControls(){const available=storedImages.length>0&&!imageBusy&&!storedBusy;"
    "$('storedPrevious').disabled=!available||storedImages.length<2;$('storedNext').disabled=!available||storedImages.length<2;"
    "$('storedSelect').disabled=!available||storedImages[storedIndex]===storedSelected;$('storedDelete').disabled=!available}"
    "function imageControls(){const blocked=!sdReady||imageBusy;$('imageFile').disabled=blocked;$('starterImages').disabled=blocked;"
    "$('imageUpload').disabled=blocked||!imagePbm;$('threshold').disabled=imageBusy||!imageGray;$('dither').disabled=imageBusy||!imageGray;storedControls()}"
    "function imageBusyState(value){imageBusy=value;imageControls()}"
    "function setSdState(value,count){const state=String(value||'unknown').toLowerCase(),total=Number.isFinite(Number(count))?Math.max(0,Math.floor(Number(count))):0;"
    "const output=$('sdState');if(state==='ready'||state==='available'||state==='ok'){sdReady=true;output.dataset.state='ready';"
    "output.textContent=total>0?'microSD 可用，现有 '+total+' 张图片。':'microSD 可用，尚无图片。'}"
    "else if(state==='missing'||state==='no_card'||state==='not_present'){sdReady=false;output.dataset.state='missing';"
    "output.textContent='未检测到可用的 microSD。未安装时可忽略；如需使用，请关机插卡后再开机。'}"
    "else if(state==='full'){sdReady=false;output.dataset.state='full';"
    "output.textContent='microSD 已有 '+total+' 张有效图片，达到固件上限。请先删除不需要的图片。'}"
    "else if(state==='overflow'){sdReady=false;output.dataset.state='full';"
    "output.textContent='microSD 中的图片超过固件上限，当前可管理 '+total+' 张。删除后请重启设备重新扫描。'}"
    "else if(state==='unavailable'||state==='error'||state==='unreadable'){sdReady=false;output.dataset.state='error';"
    "output.textContent='microSD 暂不可用。请确认使用 FAT32，并在关机状态下重新插卡。'}"
    "else{sdReady=false;output.dataset.state='unknown';output.textContent='当前无法读取 microSD 状态，图片导入暂不可用。'}"
    "if(!imageGray)show('imageMessage',sdReady?'请先选择图片':'microSD 可用后即可导入图片。');imageControls()}"
    "function drawStoredPbm(data){const header=[80,52,10,52,48,48,32,51,48,48,10];if(data.length!==header.length+15000||"
    "header.some((value,index)=>data[index]!==value))throw new Error('设备返回的图片预览无效');const canvas=$('storedCanvas'),"
    "context=canvas.getContext('2d'),preview=context.createImageData(IMAGE_WIDTH,IMAGE_HEIGHT),offset=header.length;"
    "for(let index=0;index<IMAGE_WIDTH*IMAGE_HEIGHT;index++){const black=(data[offset+(index>>3)]&(1<<(7-(index&7))))!==0,value=black?0:255,pixel=index*4;"
    "preview.data[pixel]=value;preview.data[pixel+1]=value;preview.data[pixel+2]=value;preview.data[pixel+3]=255}context.putImageData(preview,0,0)}"
    "async function renderStored(){if(storedImages.length===0){$('storedManager').hidden=true;storedControls();return}"
    "storedIndex=Math.max(0,Math.min(storedIndex,storedImages.length-1));const name=storedImages[storedIndex],requestId=++storedRequest;"
    "$('storedManager').hidden=false;$('storedPosition').textContent=(storedIndex+1)+' / '+storedImages.length;"
    "$('storedFilename').textContent=name;const canvas=$('storedCanvas'),context=canvas.getContext('2d');context.fillStyle='#000';"
    "context.fillRect(0,0,canvas.width,canvas.height);storedBusy=true;storedControls();show('storedMessage','正在载入预览…');try{"
    "const response=await fetch('/api/images/preview?name='+encodeURIComponent(name),{cache:'no-store'});if(!response.ok)throw new Error(await response.text()||'无法读取图片预览');"
    "const data=new Uint8Array(await response.arrayBuffer());if(requestId!==storedRequest)return;drawStoredPbm(data);"
    "show('storedMessage',name===storedSelected?'当前正在显示这张图片。':'')}catch(error){if(requestId===storedRequest)show('storedMessage',error.message)}"
    "finally{if(requestId===storedRequest){storedBusy=false;storedControls()}}}"
    "async function loadStoredImages(preferred){const previous=preferred||storedImages[storedIndex]||'';const response=await fetch('/api/images',{cache:'no-store'});"
    "if(!response.ok)throw new Error(await response.text()||'无法读取图片列表');const state=await response.json();"
    "if(!Array.isArray(state.images)||state.images.some(name=>typeof name!=='string'))throw new Error('设备返回的图片列表无效');"
    "storedImages=state.images;storedSelected=typeof state.selected==='string'?state.selected:'';const position=storedImages.indexOf(previous);"
    "storedIndex=position>=0?position:Math.max(0,storedImages.indexOf(storedSelected));await renderStored()}"
    "async function browseStored(step){if(storedBusy||imageBusy||storedImages.length<2)return;storedIndex=(storedIndex+step+storedImages.length)%storedImages.length;await renderStored()}"
    "function fallbackImage(file){return new Promise((resolve,reject)=>{const url=URL.createObjectURL(file),source=new Image();"
    "source.onload=()=>resolve({source,width:source.naturalWidth,height:source.naturalHeight,close:()=>URL.revokeObjectURL(url)});"
    "source.onerror=()=>{URL.revokeObjectURL(url);reject(new Error('无法解码这张图片'))};source.src=url})}"
    "async function decodeImage(file){const name=file.name.toLowerCase(),type=file.type.toLowerCase();"
    "if(file.size===0)throw new Error('图片文件为空');if(file.size>SOURCE_MAX_BYTES)throw new Error('图片不能超过 32 MiB');"
    "if(type!=='image/jpeg'&&type!=='image/png'&&!name.endsWith('.jpg')&&!name.endsWith('.jpeg')&&!name.endsWith('.png'))"
    "throw new Error('请选择 JPEG 或 PNG 图片');let decoded;if(window.createImageBitmap){try{const source=await createImageBitmap(file,{imageOrientation:'from-image'});"
    "decoded={source,width:source.width,height:source.height,close:()=>source.close()}}catch(error){decoded=await fallbackImage(file)}}"
    "else decoded=await fallbackImage(file);if(decoded.width<1||decoded.height<1||decoded.width>SOURCE_MAX_PIXELS/decoded.height){decoded.close();"
    "throw new Error('图片像素不能超过 4000 万')}return decoded}"
    "function renderImage(){if(!imageGray)return;const threshold=Number($('threshold').value),useDither=$('dither').checked;"
    "const values=useDither?new Float32Array(imageGray):imageGray,result=new Uint8Array(IMAGE_WIDTH*IMAGE_HEIGHT);"
    "for(let y=0;y<CONTENT_HEIGHT;y++){for(let x=0;x<IMAGE_WIDTH;x++){const index=y*IMAGE_WIDTH+x,oldValue=values[index];"
    "const next=oldValue>=threshold?255:0;result[index]=next;if(useDither){const error=oldValue-next;"
    "if(x+1<IMAGE_WIDTH)values[index+1]+=error*7/16;if(y+1<CONTENT_HEIGHT){if(x>0)values[index+IMAGE_WIDTH-1]+=error*3/16;"
    "values[index+IMAGE_WIDTH]+=error*5/16;if(x+1<IMAGE_WIDTH)values[index+IMAGE_WIDTH+1]+=error/16}}}}"
    "const canvas=$('imageCanvas'),context=canvas.getContext('2d'),preview=context.createImageData(IMAGE_WIDTH,IMAGE_HEIGHT),packed=new Uint8Array(15000);"
    "for(let index=0;index<result.length;index++){const value=result[index],offset=index*4;preview.data[offset]=value;preview.data[offset+1]=value;"
    "preview.data[offset+2]=value;preview.data[offset+3]=255;if(value===0)packed[index>>3]|=1<<(7-(index&7))}context.putImageData(preview,0,0);"
    "const header=new Uint8Array([80,52,10,52,48,48,32,51,48,48,10]);imagePbm=new Blob([header,packed],{type:'application/octet-stream'});"
    "$('thresholdValue').value=String(threshold);show('imageMessage','黑白预览已生成，可以写入 microSD。');imageControls()}"
    "function scheduleImageRender(){cancelAnimationFrame(imageFrame);imageFrame=requestAnimationFrame(renderImage)}"
    "async function prepareImage(file){const decoded=await decodeImage(file),canvas=$('imageCanvas'),context=canvas.getContext('2d',{willReadFrequently:true});"
    "context.fillStyle='#000';context.fillRect(0,0,IMAGE_WIDTH,IMAGE_HEIGHT);const scale=Math.min(IMAGE_WIDTH/decoded.width,CONTENT_HEIGHT/decoded.height),"
    "width=Math.max(1,Math.round(decoded.width*scale)),height=Math.max(1,Math.round(decoded.height*scale));"
    "context.drawImage(decoded.source,Math.floor((IMAGE_WIDTH-width)/2),Math.floor((CONTENT_HEIGHT-height)/2),width,height);decoded.close();"
    "const pixels=context.getImageData(0,0,IMAGE_WIDTH,IMAGE_HEIGHT).data;imageGray=new Uint8Array(IMAGE_WIDTH*IMAGE_HEIGHT);"
    "for(let index=0;index<imageGray.length;index++){const offset=index*4;imageGray[index]=Math.round(pixels[offset]*.2126+pixels[offset+1]*.7152+pixels[offset+2]*.0722)}"
    "$('imagePreview').hidden=false;renderImage()}"
    "function zones(){const select=$('timezone');for(let minutes=-720;minutes<=840;minutes+=15){const option=document.createElement('option');"
    "const sign=minutes>=0?'+':'-';const absolute=Math.abs(minutes),hours=String(Math.floor(absolute/60)).padStart(2,'0'),"
    "remainder=String(absolute%60).padStart(2,'0');option.value=minutes;option.textContent='UTC'+sign+hours+':'+remainder+"
    "(minutes===480?' · 中国标准时间':'');select.appendChild(option)}}"
    "async function post(path,body){const headers={'Content-Type':'application/x-www-form-urlencoded',"
    "'X-RLCD-Token':token};const response=await fetch(path,{method:'POST',"
    "headers,body});const text=await response.text();if(!response.ok)throw new Error(text||'操作失败');return text;}"
    "function settingsControls(busy){settingsBusy=busy;$('settings').setAttribute('aria-busy',busy?'true':'false');$('settings').querySelectorAll('input,select,button').forEach(input=>{input.disabled=busy})}"
    "async function load(section='all',preferred){const response=await fetch('/api/state',{cache:'no-store'});if(!response.ok)throw new Error('无法读取设备设置');"
    "const state=await response.json();token=state.token;setWifiState(state.wifi_configured,state.wifi_ssid);if(section==='all'||section==='settings'){$('timezone').value=state.timezone;"
    "$('unit').value=state.unit;$('volume').value=state.volume;$('volumeValue').value=state.volume;$('updates').value=state.updates;"
    "$('alarm').value=state.alarm;$('alarmTime').value=String(state.alarm_hour).padStart(2,'0')+':'+String(state.alarm_minute).padStart(2,'0');"
    "alarmDays().forEach(input=>{input.checked=(state.alarm_days&Number(input.dataset.bit))!==0});initialUpdates=state.updates;}"
    "if(section==='all'||section==='conversation')setConversationState(state.conversation);if(section==='all'||section==='weather'){try{await setWeatherState(state.weather)}catch(error){weatherAvailable=false;weatherBusy=false;weatherControls();show('weatherMessage',error.message)}}"
    "if(section==='all'||section==='images'){setSdState(state.sd_state,state.image_count);try{await loadStoredImages(preferred)}catch(error){storedImages=[];$('storedManager').hidden=true;show('storedMessage',error.message)}}}"
    "$('volume').oninput=()=>{$('volumeValue').value=$('volume').value};"
    "$('settings').onsubmit=async event=>{event.preventDefault();if(settingsBusy)return;const match=/^(\\d{2}):(\\d{2})$/.exec($('alarmTime').value);"
    "const days=Array.from(alarmDays()).reduce((mask,input)=>input.checked?mask|Number(input.dataset.bit):mask,0);"
    "if(!match||Number(match[1])>23||Number(match[2])>59){show('settingsMessage','请选择有效的响铃时间。');return}"
    "if(days===0||(days&~127)!==0){show('settingsMessage','请至少选择一个重复日期。');return}"
    "$('alarmHour').value=String(Number(match[1]));$('alarmMinute').value=String(Number(match[2]));$('alarmDays').value=String(days);"
    "if(initialUpdates!=='beta'&&$('updates').value==='beta'&&!confirm('开启 Beta 更新？测试固件可能不稳定，请确认你能够使用本地 OTA 或 USB 恢复设备。'))return;show('settingsMessage','正在保存…');try{const body=new URLSearchParams(new FormData(event.target)).toString();"
    "settingsControls(true);const message=await post('/api/settings',body);await load('settings');show('settingsMessage',message)}catch(error){show('settingsMessage',error.message)}finally{settingsControls(false)}};"
    "$('setTime').onclick=async()=>{show('timeMessage','正在校准…');try{show('timeMessage',await post('/api/time','unix='+unix()))}"
    "catch(error){show('timeMessage',error.message)}};"
    "$('wifiEdit').onclick=()=>wifiEditing(true);$('wifiCancel').onclick=()=>{wifiEditing(false);show('wifiMessage','')};"
    "$('showWifiPassword').onchange=()=>{$('wifiPassword').type=$('showWifiPassword').checked?'text':'password'};"
    "$('openWifi').onchange=()=>{if($('openWifi').checked){$('wifiPassword').value='';$('showWifiPassword').checked=false;$('wifiPassword').type='password'}wifiControls()};"
    "$('wifiForm').onsubmit=async event=>{event.preventDefault();if(wifiBusy)return;const ssid=$('wifiSsid').value,password=$('wifiPassword').value,open=$('openWifi').checked;"
    "if(!ssid||new TextEncoder().encode(ssid).length>32){show('wifiMessage','Wi-Fi 名称应为 1—32 字节。');return}"
    "if(!open&&!/^[\\x20-\\x7e]{8,63}$/.test(password)){show('wifiMessage','请输入 8—63 位英文字符、数字或符号，开放网络请勾选对应选项。');return}"
    "wifiBusy=true;wifiControls();show('wifiMessage','正在验证新网络，设置热点可能短暂重新连接…');try{const body='ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(open?'':password);"
    "show('wifiMessage',await post('/api/wifi/change',body))}catch(error){show('wifiMessage',error.name==='TypeError'?'设置热点刚刚断开。请查看设备是否已连接新网络；若热点仍在，可刷新页面确认原配置仍保留。':error.message);wifiBusy=false;wifiControls()}};"
    "$('weatherEnabled').onchange=()=>weatherControls();$('weatherProvince').onchange=async()=>{if(weatherBusy||!weatherAvailable)return;"
    "const province=$('weatherProvince').value;weatherBusy=true;weatherControls();weatherOptions($('weatherCity'),[],province?'正在读取城市…':'请先选择省份');"
    "const requestId=++weatherRegionRequest;try{const cities=province?await weatherRegions(province):[];if(requestId!==weatherRegionRequest)return;"
    "weatherOptions($('weatherCity'),cities,province?'请选择城市':'请先选择省份');show('weatherMessage','')}catch(error){if(requestId===weatherRegionRequest){"
    "weatherOptions($('weatherCity'),[],'无法读取城市');show('weatherMessage',error.message)}}finally{if(requestId===weatherRegionRequest){weatherBusy=false;weatherControls()}}};"
    "$('weatherForm').onsubmit=async event=>{event.preventDefault();if(weatherBusy||!weatherAvailable)return;const enabled=$('weatherEnabled').value==='on',"
    "province=$('weatherProvince').value,city=$('weatherCity').value;"
    "if(enabled&&(!$('weatherApiHost').value||!province||!city)){show('weatherMessage','请填写 API Host，并依次选择省份和城市。');return}"
    "if(enabled&&!initialWeatherEnabled&&!confirm('开启天气后，设备会使用你的凭据直连 QWeather，相关请求可能计入你的账户用量。是否继续？'))return;"
    "const fields=new FormData(event.target);fields.set('district','');if(!enabled&&(!province||!city)){fields.set('province','0');fields.set('city','0')}"
    "const body=new URLSearchParams(fields).toString();weatherBusy=true;weatherControls();show('weatherMessage',enabled?'正在保存，随后将关闭热点并获取天气…':'正在保存…');"
    "let closing=false;try{const message=await post('/api/weather/config',body);$('weatherKey').value='';if(enabled){closing=true;show('weatherMessage',message);return}"
    "await load('weather');show('weatherMessage',message)}catch(error){if(enabled&&error.name==='TypeError'){closing=true;show('weatherMessage','设置热点已关闭，请查看设备屏幕上的天气获取结果。')}"
    "else show('weatherMessage',error.message)}finally{$('weatherKey').value='';if(!closing){weatherBusy=false;weatherControls()}}};"
    "$('weatherClear').onclick=async()=>{if(weatherBusy||!weatherAvailable)return;if(!confirm('确认清除天气 API Key、位置和本地缓存？此操作无法撤销。'))return;"
    "weatherBusy=true;weatherControls();show('weatherMessage','正在清除…');try{const message=await post('/api/weather/clear','confirm=CLEAR_WEATHER');await load('weather');show('weatherMessage',message)}"
    "catch(error){show('weatherMessage',error.message)}finally{weatherBusy=false;weatherControls()}};"
    "$('conversationForm').onsubmit=async event=>{event.preventDefault();if(conversationBusy||!conversationAvailable)return;"
    "const body=new URLSearchParams(new FormData(event.target)).toString();conversationBusy=true;conversationControls();"
    "show('conversationMessage','正在保存…');try{const message=await post('/api/conversation/config',body);await load('conversation');show('conversationMessage',message)}"
    "catch(error){show('conversationMessage',error.message)}finally{$('conversationKey').value='';conversationBusy=false;conversationControls()}};"
    "$('conversationClear').onclick=async()=>{if(conversationBusy||!conversationAvailable)return;"
    "if(!confirm('确认清除已保存的 API Key，关闭 AI 对话并恢复默认模型与 API Host？此操作无法撤销。'))return;conversationBusy=true;conversationControls();"
    "show('conversationMessage','正在清除…');try{const message=await post('/api/conversation/clear','confirm=CLEAR_CONVERSATION');"
    "await load('conversation');show('conversationMessage',message)}catch(error){show('conversationMessage',error.message)}finally{conversationBusy=false;conversationControls()}};"
    "$('defaults').onclick=async()=>{if(settingsBusy||!confirm('恢复偏好默认值？Wi-Fi 配置不会被删除。'))return;settingsControls(true);"
    "show('maintenanceMessage','正在恢复…');try{const message=await post('/api/settings/defaults','confirm=DEFAULTS');await load('settings');show('maintenanceMessage',message)}"
    "catch(error){show('maintenanceMessage',error.message)}finally{settingsControls(false)}};"
    "$('forgetWifi').onclick=async()=>{if(!confirm('移除已保存的 Wi-Fi？设置热点随后会关闭并进入首次配网。'))return;"
    "wifiBusy=true;wifiControls();show('wifiMessage','正在移除…');try{show('wifiMessage',await post('/api/wifi/clear','confirm=FORGET'))}"
    "catch(error){wifiBusy=false;wifiControls();show('wifiMessage',error.message)}};"
    "$('storedPrevious').onclick=()=>browseStored(-1);$('storedNext').onclick=()=>browseStored(1);"
    "$('storedSelect').onclick=async()=>{const name=storedImages[storedIndex];if(!name||imageBusy||storedBusy)return;imageBusyState(true);"
    "show('storedMessage','正在设置…');try{const message=await post('/api/images/select','name='+encodeURIComponent(name));"
    "storedSelected=name;show('storedMessage',message);storedControls()}catch(error){show('storedMessage',error.message)}finally{imageBusyState(false)}};"
    "$('storedDelete').onclick=async()=>{const name=storedImages[storedIndex];if(!name||imageBusy||storedBusy)return;"
    "if(!confirm('删除“'+name+'”？此操作无法撤销。'))return;const next=storedImages.length>1?storedImages[(storedIndex+1)%storedImages.length]:'';"
    "imageBusyState(true);show('storedMessage','正在删除…');try{const message=await post('/api/images/delete','name='+encodeURIComponent(name)+'&confirm=DELETE');"
    "await load('images',next);show('storedMessage',message)}catch(error){show('storedMessage',error.message)}finally{imageBusyState(false)}};"
    "$('threshold').oninput=()=>{ $('thresholdValue').value=$('threshold').value;scheduleImageRender()};"
    "$('dither').onchange=scheduleImageRender;"
    "$('imageFile').onchange=async event=>{const file=event.target.files[0];imagePbm=null;imageGray=null;$('imagePreview').hidden=true;"
    "$('imageProgress').hidden=true;$('imageProgress').value=0;imageControls();if(!file){show('imageMessage','请先选择图片');return}"
    "imageBusyState(true);show('imageMessage','正在本地转换图片…');try{await prepareImage(file)}catch(error){event.target.value='';"
    "show('imageMessage',error.message)}finally{imageBusyState(false)}};"
    "$('imageUpload').onclick=()=>{if(!sdReady){show('imageMessage','microSD 当前不可用');return}if(!imagePbm){show('imageMessage','请先选择并预览图片');return}"
    "if(!confirm('将当前黑白预览写入 microSD？写入期间请保持供电并勿拔卡。'))return;const request=new XMLHttpRequest(),"
    "progress=$('imageProgress');imageBusyState(true);progress.hidden=false;progress.value=0;show('imageMessage','正在写入…');"
    "request.open('POST','/api/images/upload');request.setRequestHeader('Content-Type','application/octet-stream');"
    "request.setRequestHeader('X-RLCD-Token',token);request.upload.onprogress=event=>{if(event.lengthComputable){const value=Math.round(event.loaded*100/event.total);"
    "progress.value=value;show('imageMessage','正在上传 '+value+'%')}};request.onload=()=>{imageBusyState(false);show('imageMessage',request.responseText||"
    "(request.status>=200&&request.status<300?'图片已写入 microSD':'写入失败'));if(request.status>=200&&request.status<300){progress.value=100;"
    "load('images').catch(()=>{})}};"
    "request.onerror=()=>{imageBusyState(false);show('imageMessage','连接中断，请确认设备仍在设置模式')};request.send(imagePbm)};"
    "$('starterImages').onclick=async()=>{if(!sdReady){show('starterMessage','microSD 当前不可用');return}"
    "if(!confirm('安装公共演示图集？设备将关闭设置热点并连接已保存的 Wi-Fi。请随后查看设备屏幕。'))return;"
    "imageBusyState(true);show('starterMessage','正在提交下载请求…');try{const result=await post('/api/images/starter','confirm=STARTER');"
    "show('starterMessage',result||'请求已提交，请查看设备屏幕')}catch(error){show('starterMessage',error.name==='TypeError'?"
    "'连接已断开，请查看设备屏幕上的下载结果。':error.message)}finally{imageBusyState(false)}};"
    "$('upload').onclick=()=>{const file=$('file'),button=$('upload'),progress=$('progress');if(!file.files.length){"
    "show('updateMessage','请先选择 OTA 固件');return}if(!confirm('开始写入固件？写入期间请保持供电。'))return;"
    "button.disabled=true;file.disabled=true;const request=new XMLHttpRequest();request.open('POST','/update');"
    "request.setRequestHeader('Content-Type','application/octet-stream');request.setRequestHeader('X-RLCD-Token',token);"
    "request.upload.onprogress=event=>{if(event.lengthComputable){const value=Math.round(event.loaded*100/event.total);"
    "progress.value=value;show('updateMessage','正在上传 '+value+'%')}};request.onload=()=>{show('updateMessage',request.responseText||"
    "(request.status===200?'升级成功，设备即将重启':'升级失败'));if(request.status!==200){button.disabled=false;file.disabled=false}};"
    "request.onerror=()=>show('updateMessage','连接中断，请查看设备屏幕');request.send(file.files[0])};"
    "zones();settingsControls(true);weatherControls();conversationControls();load().catch(error=>{show('settingsMessage',error.message);setSdState('unknown',0)}).finally(()=>settingsControls(false));</script></main></body></html>";

static const char UPDATE_SUCCESS_PAGE[] =
    "升级成功。固件已校验，设备即将自动重启。";
static const char UPDATE_ERROR_PAGE[] =
    "升级失败。请确认选择的是本项目的 OTA 固件，然后重新开启升级模式。";

static EventGroupHandle_t s_events;
static httpd_handle_t s_http_server;
static bool s_initialized;
static bool s_upload_started;
static bool s_mutation_active;
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
        "default-src 'none'; base-uri 'none'; frame-ancestors 'none'; "
        "form-action 'self'; style-src 'unsafe-inline'; "
        "script-src 'unsafe-inline'; connect-src 'self'; img-src blob:");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
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
    const bool recovery_mode = boot_recovery_is_active();
    portENTER_CRITICAL(&s_status_lock);
    if (recovery_mode) {
        s_session_started_tick = 0U;
        s_session_deadline_active = false;
    } else {
        s_session_started_tick = now;
        s_session_deadline_active = true;
    }
    portEXIT_CRITICAL(&s_status_lock);
}

static TickType_t session_deadline_remaining_ticks(void)
{
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    uint32_t remaining = portMAX_DELAY;
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
    return send_page(
        request, "200 OK", "text/html; charset=utf-8",
        boot_recovery_is_active() ? RECOVERY_SETTINGS_PAGE
                                  : SETTINGS_PAGE);
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
        s_upload_started, s_mutation_active, false);
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
            s_upload_started, s_mutation_active, false)) {
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
            s_upload_started, s_mutation_active, false)) {
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

static bool image_filename_is_safe(const char *filename)
{
    if (filename == NULL || filename[0] == '\0' || filename[0] == '.') {
        return false;
    }
    size_t length = 0U;
    while (length < SD_IMAGE_FILENAME_CAPACITY &&
           filename[length] != '\0') {
        const char character = filename[length];
        const bool allowed =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.';
        if (!allowed) {
            return false;
        }
        ++length;
    }
    return length > 0U && length < SD_IMAGE_FILENAME_CAPACITY;
}

static bool parse_image_name_form(
    const char *body, size_t length, bool require_delete_confirmation,
    char filename[SD_IMAGE_FILENAME_CAPACITY])
{
    static const char prefix[] = "name=";
    static const char delete_suffix[] = "&confirm=DELETE";
    if (body == NULL || filename == NULL ||
        length <= sizeof(prefix) - 1U) {
        return false;
    }

    size_t suffix_length = 0U;
    if (require_delete_confirmation) {
        suffix_length = sizeof(delete_suffix) - 1U;
        if (length <= sizeof(prefix) - 1U + suffix_length ||
            memcmp(body + length - suffix_length, delete_suffix,
                   suffix_length) != 0) {
            return false;
        }
    }
    const size_t filename_length =
        length - (sizeof(prefix) - 1U) - suffix_length;
    if (memcmp(body, prefix, sizeof(prefix) - 1U) != 0 ||
        filename_length == 0U ||
        filename_length >= SD_IMAGE_FILENAME_CAPACITY) {
        return false;
    }
    memcpy(filename, body + sizeof(prefix) - 1U, filename_length);
    filename[filename_length] = '\0';
    if (!image_filename_is_safe(filename)) {
        memset(filename, 0, SD_IMAGE_FILENAME_CAPACITY);
        return false;
    }
    return true;
}

static bool request_image_name(
    httpd_req_t *request,
    char filename[SD_IMAGE_FILENAME_CAPACITY])
{
    static const char prefix[] = "name=";
    if (request == NULL || filename == NULL) {
        return false;
    }
    const size_t query_length = httpd_req_get_url_query_len(request);
    if (query_length <= sizeof(prefix) - 1U ||
        query_length >= SETTINGS_IMAGE_FORM_CAPACITY) {
        return false;
    }
    char query[SETTINGS_IMAGE_FORM_CAPACITY] = {0};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) !=
        ESP_OK) {
        return false;
    }
    const size_t filename_length = query_length - (sizeof(prefix) - 1U);
    if (memcmp(query, prefix, sizeof(prefix) - 1U) != 0 ||
        filename_length == 0U ||
        filename_length >= SD_IMAGE_FILENAME_CAPACITY) {
        return false;
    }
    memcpy(filename, query + sizeof(prefix) - 1U, filename_length);
    filename[filename_length] = '\0';
    if (!image_filename_is_safe(filename)) {
        memset(filename, 0, SD_IMAGE_FILENAME_CAPACITY);
        return false;
    }
    return true;
}

static bool cached_image_exists(const char *filename)
{
    if (!image_filename_is_safe(filename)) {
        return false;
    }
    const size_t count = sd_image_store_count();
    for (size_t index = 0U; index < count; ++index) {
        char candidate[SD_IMAGE_FILENAME_CAPACITY] = {0};
        if (sd_image_store_filename_at(index, candidate,
                                       sizeof(candidate)) &&
            strcmp(candidate, filename) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t image_list_get_handler(httpd_req_t *request)
{
    if (!portal_is_ready()) {
        return send_page(request, "409 Conflict",
                         "text/plain; charset=utf-8",
                         "设置会话已关闭。\n");
    }

    typedef struct {
        sd_image_catalog_snapshot_t catalog;
        char json[SETTINGS_IMAGE_LIST_JSON_CAPACITY];
    } image_list_response_t;
    image_list_response_t *const response = heap_caps_calloc(
        1U, sizeof(*response), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL) {
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备内存不足，暂时无法读取图片列表。\n");
    }
    if (!sd_image_store_catalog_snapshot(&response->catalog)) {
        heap_caps_free(response);
        return send_page(request, "500 Internal Server Error",
                         "text/plain; charset=utf-8",
                         "图片列表暂不可用。\n");
    }
    const size_t count = response->catalog.count;
    const size_t selected_index = response->catalog.selected_index;
    size_t length = 0U;
    int written = snprintf(
        response->json, sizeof(response->json),
        "{\"count\":%u,\"selected\":\"%s\",\"images\":[",
        (unsigned int)count,
        selected_index < count
            ? response->catalog.filenames[selected_index]
            : "");
    if (written <= 0 || (size_t)written >= sizeof(response->json)) {
        heap_caps_free(response);
        return send_page(request, "500 Internal Server Error",
                         "text/plain; charset=utf-8",
                         "图片列表暂不可用。\n");
    }
    length = (size_t)written;
    for (size_t index = 0U; index < count; ++index) {
        const char *const filename = response->catalog.filenames[index];
        if (!image_filename_is_safe(filename)) {
            heap_caps_free(response);
            return send_page(request, "500 Internal Server Error",
                             "text/plain; charset=utf-8",
                             "图片列表暂不可用。\n");
        }
        written = snprintf(response->json + length,
                           sizeof(response->json) - length,
                           "%s\"%s\"", index > 0U ? "," : "",
                           filename);
        if (written <= 0 ||
            (size_t)written >= sizeof(response->json) - length) {
            heap_caps_free(response);
            return send_page(request, "500 Internal Server Error",
                             "text/plain; charset=utf-8",
                             "图片列表暂不可用。\n");
        }
        length += (size_t)written;
    }
    written = snprintf(response->json + length,
                       sizeof(response->json) - length, "]}");
    if (written != 2) {
        heap_caps_free(response);
        return send_page(request, "500 Internal Server Error",
                         "text/plain; charset=utf-8",
                         "图片列表暂不可用。\n");
    }
    const esp_err_t send_error = send_page(
        request, "200 OK", "application/json; charset=utf-8",
        response->json);
    heap_caps_free(response);
    return send_error;
}

static esp_err_t image_preview_get_handler(httpd_req_t *request)
{
    if (!portal_is_ready()) {
        return send_page(request, "409 Conflict",
                         "text/plain; charset=utf-8",
                         "设置会话已关闭。\n");
    }
    char filename[SD_IMAGE_FILENAME_CAPACITY] = {0};
    if (!request_image_name(request, filename)) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "图片名称无效。\n");
    }
    uint8_t *const bitmap = heap_caps_malloc(
        MONO_IMAGE_BITMAP_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (bitmap == NULL) {
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备内存不足，暂时无法生成图片预览。\n");
    }
    if (sd_image_store_copy_bitmap(
            filename, bitmap, MONO_IMAGE_BITMAP_BYTES) != ESP_OK) {
        heap_caps_free(bitmap);
        return send_page(request, "404 Not Found",
                         "text/plain; charset=utf-8",
                         "没有找到这张图片。\n");
    }

    static const char pbm_header[] = "P4\n400 300\n";
    httpd_resp_set_status(request, "200 OK");
    httpd_resp_set_type(request, "image/x-portable-bitmap");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Content-Disposition",
                       "inline; filename=preview.pbm");
    if (httpd_resp_send_chunk(request, pbm_header,
                              sizeof(pbm_header) - 1U) != ESP_OK) {
        heap_caps_free(bitmap);
        return ESP_FAIL;
    }
    uint8_t encoded[256];
    for (size_t offset = 0U; offset < MONO_IMAGE_BITMAP_BYTES;
         offset += sizeof(encoded)) {
        const size_t remaining = MONO_IMAGE_BITMAP_BYTES - offset;
        const size_t chunk_size = remaining < sizeof(encoded)
                                      ? remaining
                                      : sizeof(encoded);
        for (size_t index = 0U; index < chunk_size; ++index) {
            encoded[index] = (uint8_t)~bitmap[offset + index];
        }
        if (httpd_resp_send_chunk(request, (const char *)encoded,
                                  chunk_size) != ESP_OK) {
            memset(encoded, 0, sizeof(encoded));
            heap_caps_free(bitmap);
            return ESP_FAIL;
        }
    }
    memset(encoded, 0, sizeof(encoded));
    heap_caps_free(bitmap);
    return httpd_resp_send_chunk(request, NULL, 0U);
}

static const char *settings_sd_state(const sd_image_status_t *status)
{
    if (status == NULL ||
        status->state == SD_IMAGE_STATE_NOT_INITIALIZED ||
        status->state == SD_IMAGE_STATE_LOADING) {
        return "unknown";
    }
    if (status->card_capacity_bytes == 0U) {
        return "missing";
    }
    if (status->catalog_truncated && status->last_io_error == ESP_OK) {
        return "overflow";
    }
    if (status->image_count >= SD_IMAGE_MAX_IMAGES ||
        status->state == SD_IMAGE_STATE_TOO_MANY_FILES) {
        return "full";
    }
    if (status->catalog_truncated ||
        status->state == SD_IMAGE_STATE_NO_MEMORY ||
        status->state == SD_IMAGE_STATE_INTERNAL_ERROR) {
        return "unavailable";
    }
    return "ready";
}

static bool parse_weather_region_id(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }
    uint32_t parsed = 0U;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        const uint32_t digit = (uint32_t)(text[index] - '0');
        if (parsed > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    if (parsed == 0U) {
        return false;
    }
    *value = parsed;
    return true;
}

static esp_err_t weather_regions_get_handler(httpd_req_t *request)
{
    if (!portal_is_ready()) {
        return send_page(request, "409 Conflict",
                         "text/plain; charset=utf-8",
                         "设置会话已关闭。\n");
    }

    uint32_t province_id = 0U;
    const size_t query_length = httpd_req_get_url_query_len(request);
    if (query_length > 0U) {
        static const char prefix[] = "province=";
        char query[32] = {0};
        if (query_length >= sizeof(query) ||
            httpd_req_get_url_query_str(request, query,
                                        sizeof(query)) != ESP_OK ||
            query_length <= sizeof(prefix) - 1U ||
            memcmp(query, prefix, sizeof(prefix) - 1U) != 0 ||
            strchr(query, '&') != NULL ||
            !parse_weather_region_id(query + sizeof(prefix) - 1U,
                                     &province_id) ||
            weather_location_province_by_id(province_id) == NULL) {
            return send_page(request, "400 Bad Request",
                             "text/plain; charset=utf-8",
                             "省份参数无效。\n");
        }
    }

    char *const json = heap_caps_calloc(
        1U, SETTINGS_WEATHER_REGIONS_JSON_CAPACITY,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (json == NULL) {
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备内存不足，暂时无法读取省市列表。\n");
    }
    size_t offset = 0U;
    int written = snprintf(json, SETTINGS_WEATHER_REGIONS_JSON_CAPACITY,
                           "{\"items\":[");
    bool valid = written > 0 &&
                 (size_t)written < SETTINGS_WEATHER_REGIONS_JSON_CAPACITY;
    if (valid) {
        offset = (size_t)written;
    }
    const size_t count = province_id == 0U
                             ? weather_location_province_count()
                             : weather_location_city_count(province_id);
    for (size_t index = 0U; valid && index < count; ++index) {
        const weather_province_t *province = NULL;
        const weather_city_t *city = NULL;
        uint32_t id = 0U;
        const char *name = NULL;
        if (province_id == 0U) {
            province = weather_location_province_at(index);
            if (province != NULL) {
                id = province->id;
                name = province->name_zh;
            }
        } else {
            city = weather_location_city_at(province_id, index);
            if (city != NULL) {
                id = city->id;
                name = city->name_zh;
            }
        }
        char escaped_name[SETTINGS_JSON_ESCAPE_CAPACITY(64U)] = {0};
        if (id == 0U || name == NULL ||
            !settings_portal_json_escape(name, escaped_name,
                                         sizeof(escaped_name))) {
            valid = false;
            break;
        }
        written = snprintf(
            json + offset,
            SETTINGS_WEATHER_REGIONS_JSON_CAPACITY - offset,
            "%s{\"id\":%lu,\"name\":\"%s\"}",
            index == 0U ? "" : ",", (unsigned long)id,
            escaped_name);
        if (written <= 0 ||
            (size_t)written >=
                SETTINGS_WEATHER_REGIONS_JSON_CAPACITY - offset) {
            valid = false;
            break;
        }
        offset += (size_t)written;
    }
    if (valid) {
        written = snprintf(json + offset,
                           SETTINGS_WEATHER_REGIONS_JSON_CAPACITY - offset,
                           "]}");
        valid = written > 0 &&
                (size_t)written <
                    SETTINGS_WEATHER_REGIONS_JSON_CAPACITY - offset;
    }
    if (!valid) {
        weather_config_clear_sensitive(
            json, SETTINGS_WEATHER_REGIONS_JSON_CAPACITY);
        heap_caps_free(json);
        return send_page(request, "500 Internal Server Error",
                         "text/plain; charset=utf-8",
                         "无法生成省市列表。\n");
    }
    const esp_err_t error = send_page(
        request, "200 OK", "application/json; charset=utf-8", json);
    weather_config_clear_sensitive(
        json, SETTINGS_WEATHER_REGIONS_JSON_CAPACITY);
    heap_caps_free(json);
    return error;
}

static const char *conversation_service_form_value(
    conversation_service_t service)
{
    switch (service) {
    case CONVERSATION_SERVICE_ALIYUN_REALTIME:
        return SETTINGS_CONVERSATION_SERVICE_ALIYUN_REALTIME;
    default:
        return NULL;
    }
}

static esp_err_t recovery_state_get_handler(httpd_req_t *request)
{
    network_time_saved_network_t saved_network = {0};
    const esp_err_t network_error =
        network_time_get_saved_network(&saved_network);
    if (network_error != ESP_OK) {
        /* Local recovery OTA must not depend on parsing old Wi-Fi settings.
         * Keep the session token available even if those settings are bad. */
        memset(&saved_network, 0, sizeof(saved_network));
        ESP_LOGW(TAG, "saved Wi-Fi unavailable in recovery: %s",
                 esp_err_to_name(network_error));
    }

    char token[SETTINGS_PORTAL_TOKEN_CAPACITY] = {0};
    char escaped_ssid[
        SETTINGS_JSON_ESCAPE_CAPACITY(NETWORK_SSID_MAX_LENGTH)] = {0};
    char json[512] = {0};
    portENTER_CRITICAL(&s_status_lock);
    memcpy(token, s_session_token, sizeof(token));
    portEXIT_CRITICAL(&s_status_lock);

    if (!settings_portal_json_escape(
            saved_network.ssid, escaped_ssid, sizeof(escaped_ssid))) {
        memset(token, 0, sizeof(token));
        memset(&saved_network, 0, sizeof(saved_network));
        return send_page(request, "500 Internal Server Error",
                         "text/plain; charset=utf-8",
                         "无法生成恢复状态。\n");
    }
    const int written = snprintf(
        json, sizeof(json),
        "{\"recovery\":true,\"wifi_readable\":%s,\"wifi_configured\":%s,"
        "\"wifi_ssid\":\"%s\",\"token\":\"%s\"}",
        network_error == ESP_OK ? "true" : "false",
        saved_network.configured ? "true" : "false",
        escaped_ssid, token);
    memset(token, 0, sizeof(token));
    memset(&saved_network, 0, sizeof(saved_network));
    if (written <= 0 || (size_t)written >= sizeof(json)) {
        memset(json, 0, sizeof(json));
        return send_page(request, "500 Internal Server Error",
                         "text/plain; charset=utf-8",
                         "无法生成恢复状态。\n");
    }
    const esp_err_t send_error = send_page(
        request, "200 OK", "application/json; charset=utf-8", json);
    memset(json, 0, sizeof(json));
    return send_error;
}

static esp_err_t settings_state_get_handler(httpd_req_t *request)
{
    if (!portal_is_ready()) {
        return send_page(request, "409 Conflict",
                         "text/plain; charset=utf-8",
                         "设置会话已关闭。\n");
    }
    if (boot_recovery_is_active()) {
        return recovery_state_get_handler(request);
    }
    app_settings_t settings = {0};
    const esp_err_t error = app_settings_get(&settings);
    if (error != ESP_OK) {
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备设置暂不可用。\n");
    }

    typedef struct {
        char token[SETTINGS_PORTAL_TOKEN_CAPACITY];
        char escaped_ssid[
            SETTINGS_JSON_ESCAPE_CAPACITY(NETWORK_SSID_MAX_LENGTH)];
        char escaped_api_host[
            SETTINGS_JSON_ESCAPE_CAPACITY(
                CONVERSATION_API_HOST_MAX_LENGTH)];
        char escaped_weather_api_host[
            SETTINGS_JSON_ESCAPE_CAPACITY(WEATHER_API_HOST_MAX_LENGTH)];
        char escaped_weather_province[
            SETTINGS_JSON_ESCAPE_CAPACITY(64U)];
        char escaped_weather_city[
            SETTINGS_JSON_ESCAPE_CAPACITY(64U)];
        char json[SETTINGS_STATE_JSON_CAPACITY];
    } settings_state_response_t;
    settings_state_response_t *const response = heap_caps_calloc(
        1U, sizeof(*response), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL) {
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备内存不足，暂时无法读取设置。\n");
    }

    portENTER_CRITICAL(&s_status_lock);
    memcpy(response->token, s_session_token, sizeof(response->token));
    portEXIT_CRITICAL(&s_status_lock);
    sd_image_status_t sd_status = {0};
    sd_image_store_get_status(&sd_status);
    network_time_saved_network_t saved_network = {0};
    const esp_err_t network_error =
        network_time_get_saved_network(&saved_network);
    conversation_config_status_t conversation_status = {0};
    const esp_err_t conversation_error =
        conversation_config_get_status(&conversation_status);
    const bool conversation_available = conversation_error == ESP_OK;
    if (!conversation_available) {
        conversation_config_make_status(NULL, &conversation_status);
        ESP_LOGW(TAG, "cloud conversation settings status unavailable: %s",
                 esp_err_to_name(conversation_error));
    }
    const char *conversation_service =
        conversation_service_form_value(conversation_status.service);
    if (conversation_service == NULL) {
        conversation_service =
            SETTINGS_CONVERSATION_SERVICE_ALIYUN_REALTIME;
    }
    const char *conversation_model =
        conversation_model_name(conversation_status.model);
    if (conversation_model == NULL) {
        conversation_model = SETTINGS_CONVERSATION_MODEL_QWEN3_OMNI;
    }
    const bool conversation_configured =
        conversation_available && conversation_status.configured;
    const bool conversation_enabled =
        conversation_configured && conversation_status.enabled;
    weather_config_status_t weather_status = {0};
    const esp_err_t weather_error =
        weather_config_get_status(&weather_status);
    const bool weather_available = weather_error == ESP_OK;
    if (!weather_available) {
        ESP_LOGW(TAG, "weather settings status unavailable: %s",
                 esp_err_to_name(weather_error));
    }
    const weather_province_t *const weather_province =
        weather_available
            ? weather_location_province_by_id(
                  weather_status.province_id)
            : NULL;
    const weather_city_t *const weather_city =
        weather_available
            ? weather_location_city_by_id(weather_status.city_id)
            : NULL;
    const bool weather_location_valid =
        weather_province != NULL && weather_city != NULL &&
        weather_location_city_belongs_to(weather_status.province_id,
                                         weather_status.city_id);
    const bool weather_configured =
        weather_available && weather_status.configured &&
        weather_location_valid;
    if (network_error != ESP_OK ||
        !settings_portal_json_escape(
            saved_network.ssid, response->escaped_ssid,
            sizeof(response->escaped_ssid)) ||
        !settings_portal_json_escape(
            conversation_status.api_host,
            response->escaped_api_host,
            sizeof(response->escaped_api_host)) ||
        !settings_portal_json_escape(
            weather_status.api_host,
            response->escaped_weather_api_host,
            sizeof(response->escaped_weather_api_host)) ||
        !settings_portal_json_escape(
            weather_location_valid ? weather_province->name_zh : "",
            response->escaped_weather_province,
            sizeof(response->escaped_weather_province)) ||
        !settings_portal_json_escape(
            weather_location_valid ? weather_city->name_zh : "",
            response->escaped_weather_city,
            sizeof(response->escaped_weather_city))) {
        conversation_config_clear_sensitive(response, sizeof(*response));
        heap_caps_free(response);
        memset(&conversation_status, 0, sizeof(conversation_status));
        memset(&weather_status, 0, sizeof(weather_status));
        memset(&saved_network, 0, sizeof(saved_network));
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备设置状态暂不可用。\n");
    }
    const int written = snprintf(
        response->json, sizeof(response->json),
        "{\"wifi_configured\":%s,\"wifi_ssid\":\"%s\","
        "\"timezone\":%d,\"unit\":\"%s\","
        "\"volume\":%u,\"updates\":\"%s\","
        "\"alarm\":\"%s\","
        "\"alarm_hour\":%u,\"alarm_minute\":%u,\"alarm_days\":%u,"
        "\"sd_state\":\"%s\",\"image_count\":%u,"
        "\"weather\":{\"available\":%s,\"configured\":%s,"
        "\"enabled\":%s,\"key_saved\":%s,"
        "\"api_host\":\"%s\",\"province_id\":%lu,"
        "\"province_name\":\"%s\",\"city_id\":%lu,"
        "\"city_name\":\"%s\"},"
        "\"conversation\":{\"available\":%s,\"configured\":%s,"
        "\"enabled\":%s,"
        "\"model\":\"%s\",\"service\":\"%s\","
        "\"api_host\":\"%s\",\"shared_endpoint\":%s},"
        "\"token\":\"%s\"}",
        saved_network.configured ? "true" : "false",
        response->escaped_ssid, settings.utc_offset_minutes,
        settings.temperature_unit == APP_TEMPERATURE_UNIT_FAHRENHEIT ? "f"
                                                                     : "c",
        settings.audio_playback_volume,
        settings.update_channel == APP_UPDATE_CHANNEL_BETA ? "beta"
                                                           : "stable",
        settings.alarm_enabled ? "on" : "off",
        (unsigned int)settings.alarm_hour,
        (unsigned int)settings.alarm_minute,
        (unsigned int)settings.alarm_weekdays,
        settings_sd_state(&sd_status),
        (unsigned int)sd_status.image_count,
        weather_available ? "true" : "false",
        weather_configured ? "true" : "false",
        weather_available && weather_status.enabled ? "true" : "false",
        weather_available && weather_status.key_saved ? "true" : "false",
        response->escaped_weather_api_host,
        (unsigned long)weather_status.province_id,
        response->escaped_weather_province,
        (unsigned long)weather_status.city_id,
        response->escaped_weather_city,
        conversation_available ? "true" : "false",
        conversation_configured ? "true" : "false",
        conversation_enabled ? "true" : "false",
        conversation_model, conversation_service,
        response->escaped_api_host,
        conversation_status.shared_endpoint ? "true" : "false",
        response->token);
    memset(&conversation_status, 0, sizeof(conversation_status));
    memset(&weather_status, 0, sizeof(weather_status));
    memset(&saved_network, 0, sizeof(saved_network));
    if (written <= 0 || (size_t)written >= sizeof(response->json)) {
        conversation_config_clear_sensitive(response, sizeof(*response));
        heap_caps_free(response);
        return send_page(request, "500 Internal Server Error",
                         "text/plain; charset=utf-8",
                         "无法生成设置状态。\n");
    }
    const esp_err_t send_error = send_page(
        request, "200 OK", "application/json; charset=utf-8",
        response->json);
    conversation_config_clear_sensitive(response, sizeof(*response));
    heap_caps_free(response);
    return send_error;
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

static esp_err_t finish_reprovisioning_request(httpd_req_t *request,
                                               const char *message)
{
    const esp_err_t response_error = send_page(
        request, "200 OK", "text/plain; charset=utf-8", message);
    portENTER_CRITICAL(&s_status_lock);
    s_mutation_active = false;
    s_session_closing = true;
    portEXIT_CRITICAL(&s_status_lock);
    xEventGroupSetBits(s_events, UPDATE_EVENT_REPROVISION);
    return response_error;
}

static esp_err_t finish_wifi_change_request(httpd_req_t *request,
                                            const char *message)
{
    const esp_err_t response_error = send_page(
        request, "200 OK", "text/plain; charset=utf-8", message);
    portENTER_CRITICAL(&s_status_lock);
    s_mutation_active = false;
    s_session_closing = true;
    portEXIT_CRITICAL(&s_status_lock);
    xEventGroupSetBits(s_events, UPDATE_EVENT_WIFI_CHANGED);
    return response_error;
}

static esp_err_t finish_weather_refresh_request(httpd_req_t *request,
                                                const char *message)
{
    const esp_err_t response_error = send_page(
        request, "200 OK", "text/plain; charset=utf-8", message);
    portENTER_CRITICAL(&s_status_lock);
    s_session_closing = true;
    portEXIT_CRITICAL(&s_status_lock);
    /* Keep mutation ownership until update_task consumes the terminal bit.
     * This avoids a false/no-event gap at the fixed session deadline. */
    xEventGroupSetBits(s_events, UPDATE_EVENT_WEATHER_REFRESH);
    return response_error;
}

static esp_err_t finish_broken_portal_request(httpd_req_t *request,
                                              const char *message)
{
    const esp_err_t response_error = send_page(
        request, "503 Service Unavailable", "text/plain; charset=utf-8",
        message);
    portENTER_CRITICAL(&s_status_lock);
    s_mutation_active = false;
    s_session_closing = true;
    portEXIT_CRITICAL(&s_status_lock);
    xEventGroupSetBits(s_events, UPDATE_EVENT_CANCEL);
    return response_error;
}

static esp_err_t settings_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_PORTAL_FORM_CAPACITY] = {0};
    size_t length = 0U;
    app_settings_t current = {0};
    app_settings_t settings = {0};
    const esp_err_t receive_error = receive_form(
        request, body, sizeof(body), &length);
    const esp_err_t settings_read_error =
        receive_error == ESP_OK ? app_settings_get(&current)
                                : receive_error;
    const bool valid = settings_read_error == ESP_OK &&
                       app_settings_parse_form(
                           body, length, &current, &settings);
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (receive_error == ESP_OK && settings_read_error != ESP_OK) {
        ESP_LOGW(TAG, "current settings unavailable: %s",
                 esp_err_to_name(settings_read_error));
        return send_page(
            request, "503 Service Unavailable",
            "text/plain; charset=utf-8",
            "设备设置暂不可用，请稍后重试。\n");
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
        return finish_regular_request(
            request,
            "200 OK",
            "设置已保存，但 RTC 未能随时区可靠更新。请使用手机时间校准。\n");
    }
    return finish_regular_request(
        request, "200 OK", "设置已保存并立即生效。\n");
}

static const char *weather_form_error_message(
    weather_config_result_t result)
{
    switch (result) {
    case WEATHER_CONFIG_RESULT_INVALID_API_HOST:
        return "API Host 无效；请填写 QWeather 控制台分配的专属 Host，不含协议、路径或端口。\n";
    case WEATHER_CONFIG_RESULT_API_KEY_REQUIRED:
        return "开启天气前需要填写 API Key。\n";
    case WEATHER_CONFIG_RESULT_INVALID_API_KEY:
        return "API Key 格式无效，请输入可见的英文字符、数字或符号。\n";
    case WEATHER_CONFIG_RESULT_LOCATION_REQUIRED:
        return "开启天气前需要依次选择省份和城市。\n";
    case WEATHER_CONFIG_RESULT_INVALID_LOCATION:
        return "省份与城市不匹配，请重新选择。\n";
    case WEATHER_CONFIG_RESULT_MISSING_FIELD:
        return "天气配置不完整，请填写所有必填项。\n";
    default:
        return "天气配置格式无效，请检查后重试。\n";
    }
}

static esp_err_t weather_config_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char *const body = heap_caps_calloc(
        1U, SETTINGS_WEATHER_FORM_CAPACITY, MALLOC_CAP_8BIT);
    if (body == NULL) {
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备内存不足，暂时无法保存天气配置。\n");
    }
    size_t length = 0U;
    weather_config_update_t update = {0};
    const esp_err_t receive_error = receive_form(
        request, body, SETTINGS_WEATHER_FORM_CAPACITY, &length);
    weather_config_result_t parse_result =
        receive_error == ESP_OK
            ? weather_config_parse_form(body, length, &update)
            : WEATHER_CONFIG_RESULT_INVALID_FORM;
    weather_config_clear_sensitive(body,
                                   SETTINGS_WEATHER_FORM_CAPACITY);
    heap_caps_free(body);
    if (receive_error == ESP_ERR_TIMEOUT) {
        weather_config_clear_sensitive(&update, sizeof(update));
        return send_deadline_response(request);
    }
    if (receive_error == ESP_OK &&
        parse_result == WEATHER_CONFIG_RESULT_OK) {
        const bool empty_location = update.province_id == 0U &&
                                    update.city_id == 0U &&
                                    update.district[0] == '\0';
        if (!empty_location &&
            !weather_location_selection_is_valid(
                update.province_id, update.city_id)) {
            parse_result = WEATHER_CONFIG_RESULT_INVALID_LOCATION;
        }
    }
    if (receive_error != ESP_OK ||
        parse_result != WEATHER_CONFIG_RESULT_OK) {
        ESP_LOGW(TAG, "rejected weather form: %s",
                 weather_config_result_name(parse_result));
        weather_config_clear_sensitive(&update, sizeof(update));
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         weather_form_error_message(parse_result));
    }

    if (update.enabled && update.api_key[0] == '\0') {
        weather_config_status_t status = {0};
        const esp_err_t status_error =
            weather_config_get_status(&status);
        const bool has_saved_key =
            status_error == ESP_OK && status.key_saved;
        memset(&status, 0, sizeof(status));
        if (status_error != ESP_OK) {
            weather_config_clear_sensitive(&update, sizeof(update));
            return send_page(request, "503 Service Unavailable",
                             "text/plain; charset=utf-8",
                             "天气设置暂不可用，请稍后重试。\n");
        }
        if (!has_saved_key) {
            weather_config_clear_sensitive(&update, sizeof(update));
            return send_page(request, "400 Bad Request",
                             "text/plain; charset=utf-8",
                             "开启天气前需要填写 API Key。\n");
        }
    }
    if (!begin_regular_mutation()) {
        weather_config_clear_sensitive(&update, sizeof(update));
        return send_mutation_unavailable(request);
    }

    const bool enabled = update.enabled;
    const esp_err_t error = weather_config_save(&update);
    weather_config_clear_sensitive(&update, sizeof(update));
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "weather config save failed: %s",
                 esp_err_to_name(error));
        return finish_regular_request(
            request,
            error == ESP_ERR_INVALID_ARG ? "400 Bad Request"
                                         : "500 Internal Server Error",
            error == ESP_ERR_INVALID_ARG
                ? "天气配置未通过校验，原配置未更改。\n"
                : "天气配置未能保存，原配置未更改。\n");
    }
    if (!enabled) {
        const esp_err_t notify_error =
            weather_service_notify_configuration_changed();
        if (notify_error != ESP_OK) {
            ESP_LOGW(TAG, "could not apply disabled weather setting: %s",
                     esp_err_to_name(notify_error));
        }
        return finish_regular_request(
            request, "200 OK", "天气配置已保存，天气页面已关闭，无需重启。\n");
    }

    /* update_task consumes this terminal event, stops HTTP/SoftAP, and only
     * then hands the maintenance-owned network session to the weather
     * worker. No device restart is involved. */
    return finish_weather_refresh_request(
        request,
        "天气配置已保存。设置热点即将关闭，设备将联网获取天气；请查看设备屏幕。\n");
}

static esp_err_t weather_clear_post_handler(httpd_req_t *request)
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
                               body, length, "CLEAR_WEATHER");
    weather_config_clear_sensitive(body, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!confirmed) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "清除天气配置需要重新确认。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    esp_err_t error = weather_config_clear();
    if (error == ESP_OK) {
        error = weather_service_clear_cache();
    }
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "weather config or cache clear failed: %s",
                 esp_err_to_name(error));
        (void)weather_service_notify_configuration_changed();
        return finish_regular_request(
            request, "500 Internal Server Error",
            "天气配置未能完整清除，请稍后重试。\n");
    }
    return finish_regular_request(
        request, "200 OK",
        "天气配置与本地缓存已清除，天气页面已关闭，无需重启。\n");
}

static const char *conversation_form_error_message(
    conversation_config_result_t result)
{
    switch (result) {
    case CONVERSATION_CONFIG_RESULT_UNSUPPORTED_SERVICE:
        return "当前仅支持阿里云百炼 Realtime。\n";
    case CONVERSATION_CONFIG_RESULT_INVALID_MODEL:
        return "请选择固件支持的阿里云百炼 Realtime 模型。\n";
    case CONVERSATION_CONFIG_RESULT_INVALID_API_HOST:
        return "API Host 无效；请留空使用北京共享服务，或填写受支持的百炼共享或 Workspace 专属 Host。\n";
    case CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED:
        return "开启 AI 对话前需要填写 API Key。\n";
    case CONVERSATION_CONFIG_RESULT_INVALID_API_KEY:
        return "API Key 格式无效，请输入可见的英文字符、数字或符号。\n";
    case CONVERSATION_CONFIG_RESULT_MISSING_FIELD:
        return "AI 对话配置不完整，请填写所有必填项。\n";
    default:
        return "AI 对话配置格式无效，请检查后重试。\n";
    }
}

static esp_err_t conversation_config_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char *const body = heap_caps_calloc(
        1U, SETTINGS_CONVERSATION_FORM_CAPACITY, MALLOC_CAP_8BIT);
    if (body == NULL) {
        return send_page(request, "503 Service Unavailable",
                         "text/plain; charset=utf-8",
                         "设备内存不足，暂时无法保存 AI 对话配置。\n");
    }
    size_t length = 0U;
    conversation_config_update_t update = {0};
    const esp_err_t receive_error = receive_form(
        request, body, SETTINGS_CONVERSATION_FORM_CAPACITY, &length);
    const conversation_config_result_t parse_result =
        receive_error == ESP_OK
            ? conversation_config_parse_form(body, length, &update)
            : CONVERSATION_CONFIG_RESULT_INVALID_FORM;
    conversation_config_clear_sensitive(
        body, SETTINGS_CONVERSATION_FORM_CAPACITY);
    heap_caps_free(body);
    if (receive_error == ESP_ERR_TIMEOUT) {
        conversation_config_clear_sensitive(&update, sizeof(update));
        return send_deadline_response(request);
    }
    if (receive_error != ESP_OK ||
        parse_result != CONVERSATION_CONFIG_RESULT_OK) {
        ESP_LOGW(TAG, "rejected cloud conversation form: %s",
                 conversation_config_result_name(parse_result));
        conversation_config_clear_sensitive(&update, sizeof(update));
        return send_page(
            request, "400 Bad Request", "text/plain; charset=utf-8",
            conversation_form_error_message(parse_result));
    }

    if (update.api_key[0] == '\0') {
        conversation_config_status_t status = {0};
        const esp_err_t status_error =
            conversation_config_get_status(&status);
        const bool has_saved_key =
            status_error == ESP_OK && status.configured;
        memset(&status, 0, sizeof(status));
        if (status_error != ESP_OK) {
            conversation_config_clear_sensitive(&update, sizeof(update));
            return send_page(request, "503 Service Unavailable",
                             "text/plain; charset=utf-8",
                             "AI 对话设置暂不可用，请稍后重试。\n");
        }
        if (update.enabled && !has_saved_key) {
            conversation_config_clear_sensitive(&update, sizeof(update));
            return send_page(request, "400 Bad Request",
                             "text/plain; charset=utf-8",
                             "开启 AI 对话前需要填写 API Key。\n");
        }
        if (!update.enabled && !has_saved_key) {
            conversation_config_clear_sensitive(&update, sizeof(update));
            return send_page(request, "200 OK",
                             "text/plain; charset=utf-8",
                             "AI 对话保持关闭，无需保存 API Key。\n");
        }
    }
    if (!begin_regular_mutation()) {
        conversation_config_clear_sensitive(&update, sizeof(update));
        return send_mutation_unavailable(request);
    }

    const esp_err_t error = conversation_config_save(&update);
    conversation_config_clear_sensitive(&update, sizeof(update));
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "cloud conversation config save failed: %s",
                 esp_err_to_name(error));
        return finish_regular_request(
            request,
            error == ESP_ERR_INVALID_ARG ? "400 Bad Request"
                                         : "500 Internal Server Error",
            error == ESP_ERR_INVALID_ARG
                ? "AI 对话配置未通过校验，原配置未更改。\n"
                : "AI 对话配置未能保存，原配置未更改。\n");
    }
    return finish_regular_request(
        request, "200 OK", "AI 对话配置已保存，无需重启。\n");
}

static esp_err_t conversation_clear_post_handler(httpd_req_t *request)
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
                               body, length, "CLEAR_CONVERSATION");
    conversation_config_clear_sensitive(body, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!confirmed) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "清除 AI 对话配置需要重新确认。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    const esp_err_t error = conversation_config_clear();
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "cloud conversation config clear failed: %s",
                 esp_err_to_name(error));
        return finish_regular_request(
            request, "500 Internal Server Error",
            "AI 对话配置未能清除，请稍后重试。\n");
    }
    return finish_regular_request(
        request, "200 OK",
        "AI 对话配置已清除，已关闭 AI 对话并恢复默认模型与共享 API Host，无需重启。\n");
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
        return finish_regular_request(
            request,
            "200 OK",
            "偏好已恢复默认值，但 RTC 未能随默认时区可靠更新。请使用手机时间校准。\n");
    }
    return finish_regular_request(
        request, "200 OK", "偏好已恢复默认值并立即生效。\n");
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
    return finish_reprovisioning_request(
        request,
        "Wi-Fi 配置已清除，设置热点将关闭并切换到配网；设备不会重启。\n");
}

static esp_err_t wifi_change_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_WIFI_FORM_CAPACITY] = {0};
    size_t length = 0U;
    network_credentials_t credentials = {0};
    const esp_err_t receive_error = receive_form(
        request, body, sizeof(body), &length);
    const network_credentials_result_t parse_result =
        receive_error == ESP_OK
            ? network_credentials_parse_form(body, length, &credentials)
            : NETWORK_CREDENTIALS_INVALID_FORM;
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        memset(&credentials, 0, sizeof(credentials));
        return send_deadline_response(request);
    }
    if (receive_error != ESP_OK || parse_result != NETWORK_CREDENTIALS_OK) {
        ESP_LOGW(TAG, "rejected settings Wi-Fi form: %s",
                 network_credentials_result_name(parse_result));
        memset(&credentials, 0, sizeof(credentials));
        return send_page(
            request, "400 Bad Request", "text/plain; charset=utf-8",
            "Wi-Fi 名称或密码无效。受保护网络需要 8—63 个 ASCII 字符，开放网络请明确勾选。\n");
    }
    if (!begin_regular_mutation()) {
        memset(&credentials, 0, sizeof(credentials));
        return send_mutation_unavailable(request);
    }

    set_state(FIRMWARE_UPDATE_STATE_WIFI_VALIDATING, ESP_OK);
    bool portal_available = true;
    const esp_err_t error = network_time_validate_and_save_credentials(
        &credentials, SETTINGS_WIFI_VALIDATION_TIMEOUT_MS,
        &portal_available);
    memset(&credentials, 0, sizeof(credentials));
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "settings Wi-Fi validation failed: %s",
                 esp_err_to_name(error));
        if (!portal_available) {
            return finish_broken_portal_request(
                request,
                "无线服务未能恢复设置热点，本次设置会话将关闭。候选网络未启用，请重新打开设置后确认。\n");
        }
        set_state(FIRMWARE_UPDATE_STATE_READY, ESP_OK);
        if (error != ESP_ERR_TIMEOUT) {
            return finish_regular_request(
                request, "503 Service Unavailable",
                "候选网络未启用。设备存储或无线服务暂不可用，请刷新确认后重试。\n");
        }
        return finish_regular_request(
            request, "422 Unprocessable Content",
            "无法连接新网络，原 Wi-Fi 配置未更改。请检查名称、密码和 2.4 GHz 支持后重试。\n");
    }
    set_state(FIRMWARE_UPDATE_STATE_WIFI_SAVED, ESP_OK);
    return finish_wifi_change_request(
        request,
        "新 Wi-Fi 已验证并保存。设置热点将关闭，设备随后会连接并校时；不会重启。\n");
}

static esp_err_t image_select_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_IMAGE_FORM_CAPACITY] = {0};
    char filename[SD_IMAGE_FILENAME_CAPACITY] = {0};
    size_t length = 0U;
    const esp_err_t receive_error = receive_form(
        request, body, sizeof(body), &length);
    const bool valid = receive_error == ESP_OK &&
                       parse_image_name_form(body, length, false,
                                             filename);
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!valid) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "图片名称无效。\n");
    }
    if (!cached_image_exists(filename)) {
        return send_page(request, "404 Not Found",
                         "text/plain; charset=utf-8",
                         "没有找到这张图片，请刷新列表。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    const esp_err_t error = sd_image_store_select_preferred(filename);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "could not select microSD image %s: %s",
                 filename, esp_err_to_name(error));
        return finish_regular_request(
            request, "500 Internal Server Error",
            "图片未能设为当前，请重试。\n");
    }
    ESP_LOGI(TAG, "microSD image selected from settings: %s", filename);
    return finish_regular_request(
        request, "200 OK", "已设为当前图片并保存。\n");
}

static esp_err_t image_delete_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    char body[SETTINGS_IMAGE_FORM_CAPACITY] = {0};
    char filename[SD_IMAGE_FILENAME_CAPACITY] = {0};
    size_t length = 0U;
    const esp_err_t receive_error = receive_form(
        request, body, sizeof(body), &length);
    const bool valid = receive_error == ESP_OK &&
                       parse_image_name_form(body, length, true,
                                             filename);
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!valid) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "删除图片需要重新确认，并使用列表中的精确名称。\n");
    }
    if (!cached_image_exists(filename)) {
        return send_page(request, "404 Not Found",
                         "text/plain; charset=utf-8",
                         "图片已不存在，请刷新列表。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    const esp_err_t error = sd_image_store_delete(filename);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "could not delete microSD image %s: %s",
                 filename, esp_err_to_name(error));
        if (error == ESP_ERR_NOT_FOUND) {
            return finish_regular_request(
                request, "404 Not Found",
                "图片已不存在，请刷新列表。\n");
        }
        if (error == ESP_ERR_INVALID_ARG) {
            return finish_regular_request(
                request, "400 Bad Request", "图片名称无效。\n");
        }
        return finish_regular_request(
            request, "409 Conflict",
            "microSD 当前无法删除图片，原有图片仍可使用。\n");
    }
    ESP_LOGI(TAG, "microSD image deleted from settings: %s", filename);
    return finish_regular_request(
        request, "200 OK", "图片已删除，设备无需重启。\n");
}

static esp_err_t image_upload_post_handler(httpd_req_t *request)
{
    if (!authorize_post(request)) {
        return ESP_OK;
    }
    const size_t total = request->content_len > 0
                             ? (size_t)request->content_len
                             : 0U;
    if (total != SETTINGS_IMAGE_UPLOAD_BYTES) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "图片数据无效，请重新生成黑白预览后上传。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    const sd_image_import_options_t options = {
        .expected_size = total,
        .verify_sha256 = false,
    };
    sd_image_import_t *import = NULL;
    esp_err_t error = sd_image_import_begin(&options, &import);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "microSD image import unavailable: %s",
                 esp_err_to_name(error));
        return finish_regular_request(
            request, "409 Conflict",
            "microSD 当前不可写。请确认使用 FAT32，并在关机状态下重新插卡。\n");
    }
    bool import_open = true;
    uint8_t *buffer = malloc(UPDATE_HTTP_BUFFER_SIZE);
    if (buffer == NULL) {
        error = ESP_ERR_NO_MEM;
        goto failed;
    }
    size_t received_total = 0U;
    while (received_total < total) {
        const size_t remaining = total - received_total;
        const size_t chunk_size = remaining < UPDATE_HTTP_BUFFER_SIZE
                                      ? remaining
                                      : UPDATE_HTTP_BUFFER_SIZE;
        size_t chunk_received = 0U;
        error = receive_exact(request, buffer, chunk_size,
                              &chunk_received);
        if (error != ESP_OK) {
            break;
        }
        error = sd_image_import_write(import, buffer, chunk_received);
        if (error != ESP_OK) {
            break;
        }
        received_total += chunk_received;
    }
    free(buffer);
    buffer = NULL;
    if (error == ESP_OK && received_total != total) {
        error = ESP_ERR_INVALID_SIZE;
    }
    sd_image_import_result_t result = {0};
    if (error == ESP_OK) {
        error = sd_image_import_commit(import, &result);
        import_open = false;
    }
    if (error != ESP_OK) {
        goto failed;
    }

    ESP_LOGI(TAG, "phone image %s: %s",
             result.duplicate ? "already present" : "installed",
             result.filename);
    const esp_err_t preferred_error =
        sd_image_store_select_preferred(result.filename);
    if (preferred_error != ESP_OK) {
        ESP_LOGW(TAG, "could not remember preferred image: %s",
                 esp_err_to_name(preferred_error));
    }
    return finish_regular_request(
        request, "200 OK",
        preferred_error != ESP_OK
            ? "图片已写入 microSD，但未能保存为当前图片；请在图片管理中重试。\n"
            : result.duplicate
                  ? "这张图片已经在 microSD 中，现已选中。\n"
                  : "图片已校验并写入 microSD，可以立即查看。\n");

failed:
    free(buffer);
    if (import_open) {
        (void)sd_image_import_abort(import);
    }
    ESP_LOGW(TAG, "phone image import failed: %s",
             esp_err_to_name(error));
    return finish_regular_request(
        request,
        error == ESP_ERR_TIMEOUT ? "408 Request Timeout"
                                 : "400 Bad Request",
        error == ESP_ERR_TIMEOUT
            ? "设置会话已到期，未完成的图片已丢弃。\n"
            : "图片校验或写入失败，原有图片未受影响。\n");
}

static esp_err_t starter_gallery_post_handler(httpd_req_t *request)
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
                               body, length, "STARTER");
    memset(body, 0, sizeof(body));
    if (receive_error == ESP_ERR_TIMEOUT) {
        return send_deadline_response(request);
    }
    if (!confirmed) {
        return send_page(request, "400 Bad Request",
                         "text/plain; charset=utf-8",
                         "安装演示图集需要重新确认。\n");
    }

    sd_image_status_t sd_status = {0};
    sd_image_store_get_status(&sd_status);
    const char *sd_state = settings_sd_state(&sd_status);
    if (strcmp(sd_state, "ready") != 0) {
        return send_page(request, "409 Conflict",
                         "text/plain; charset=utf-8",
                         strcmp(sd_state, "full") == 0 ||
                                 strcmp(sd_state, "overflow") == 0
                             ? "microSD 图片数量已达到或超过 32 张上限，请先删除不需要的图片。\n"
                             : "microSD 当前不可用，请关机检查存储卡。\n");
    }
    network_time_status_t network_status = {0};
    if (network_time_get_status(&network_status) != ESP_OK ||
        !network_status.configured) {
        return send_page(request, "409 Conflict",
                         "text/plain; charset=utf-8",
                         "请先完成 Wi-Fi 配置，再安装演示图集。\n");
    }
    if (!begin_regular_mutation()) {
        return send_mutation_unavailable(request);
    }

    const esp_err_t response_error = send_page(
        request, "200 OK", "text/plain; charset=utf-8",
        "请求已接收。设置热点即将关闭，请查看设备屏幕。\n");
    /* Keep the mutation owned until update_task consumes this terminal event.
     * A concurrent fixed-deadline cleanup will therefore wait for the gallery
     * bit instead of observing an artificial idle gap. The next portal start
     * resets the mutation state together with the rest of the session. */
    portENTER_CRITICAL(&s_status_lock);
    s_session_closing = true;
    portEXIT_CRITICAL(&s_status_lock);
    xEventGroupSetBits(s_events, UPDATE_EVENT_GALLERY_INSTALL);
    return response_error;
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
    const bool recovery_mode = boot_recovery_is_active();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192U;
    config.max_uri_handlers = 22U;
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
    const httpd_uri_t weather_regions_uri = {
        .uri = "/api/weather/regions",
        .method = HTTP_GET,
        .handler = weather_regions_get_handler,
    };
    const httpd_uri_t weather_config_uri = {
        .uri = "/api/weather/config",
        .method = HTTP_POST,
        .handler = weather_config_post_handler,
    };
    const httpd_uri_t weather_clear_uri = {
        .uri = "/api/weather/clear",
        .method = HTTP_POST,
        .handler = weather_clear_post_handler,
    };
    const httpd_uri_t conversation_config_uri = {
        .uri = "/api/conversation/config",
        .method = HTTP_POST,
        .handler = conversation_config_post_handler,
    };
    const httpd_uri_t conversation_clear_uri = {
        .uri = "/api/conversation/clear",
        .method = HTTP_POST,
        .handler = conversation_clear_post_handler,
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
    const httpd_uri_t wifi_change_uri = {
        .uri = "/api/wifi/change",
        .method = HTTP_POST,
        .handler = wifi_change_post_handler,
    };
    const httpd_uri_t image_list_uri = {
        .uri = "/api/images",
        .method = HTTP_GET,
        .handler = image_list_get_handler,
    };
    const httpd_uri_t image_preview_uri = {
        .uri = "/api/images/preview",
        .method = HTTP_GET,
        .handler = image_preview_get_handler,
    };
    const httpd_uri_t image_select_uri = {
        .uri = "/api/images/select",
        .method = HTTP_POST,
        .handler = image_select_post_handler,
    };
    const httpd_uri_t image_delete_uri = {
        .uri = "/api/images/delete",
        .method = HTTP_POST,
        .handler = image_delete_post_handler,
    };
    const httpd_uri_t image_upload_uri = {
        .uri = "/api/images/upload",
        .method = HTTP_POST,
        .handler = image_upload_post_handler,
    };
    const httpd_uri_t starter_gallery_uri = {
        .uri = "/api/images/starter",
        .method = HTTP_POST,
        .handler = starter_gallery_post_handler,
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
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server, &settings_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &weather_regions_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &weather_config_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &weather_clear_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &conversation_config_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &conversation_clear_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server, &time_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server, &defaults_uri);
    }
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &wifi_clear_uri);
    }
    if (error == ESP_OK) {
        error = httpd_register_uri_handler(s_http_server, &wifi_change_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &image_list_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &image_preview_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &image_select_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &image_delete_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &image_upload_uri);
    }
    if (error == ESP_OK && !recovery_mode) {
        error = httpd_register_uri_handler(s_http_server,
                                           &starter_gallery_uri);
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

    const EventBits_t pending_session_bits =
        xEventGroupGetBits(s_events) & UPDATE_EVENT_SESSION;
    if (pending_session_bits != 0U) {
        return pending_session_bits;
    }

    portENTER_CRITICAL(&s_status_lock);
    if (s_status.state == FIRMWARE_UPDATE_STATE_SUCCESS) {
        terminal_bits = UPDATE_EVENT_COMPLETE;
    } else if (s_status.state == FIRMWARE_UPDATE_STATE_FAILED ||
               s_status.state == FIRMWARE_UPDATE_STATE_EXPIRED) {
        terminal_bits = UPDATE_EVENT_FAILED;
    } else if (s_status.state == FIRMWARE_UPDATE_STATE_CANCELLED) {
        terminal_bits = UPDATE_EVENT_CANCEL;
    }
    action = settings_portal_timeout_action(
        s_upload_started, s_mutation_active, false);
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
    EventBits_t wait_for = 0U;
    if (action == SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_UPLOAD) {
        wait_for = UPDATE_EVENT_COMPLETE | UPDATE_EVENT_FAILED;
    } else if (action == SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_MUTATION) {
        wait_for = UPDATE_EVENT_MUTATION_ENDED |
                   UPDATE_EVENT_REPROVISION |
                   UPDATE_EVENT_GALLERY_INSTALL |
                   UPDATE_EVENT_WIFI_CHANGED |
                   UPDATE_EVENT_WEATHER_REFRESH;
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
    esp_err_t error = audio_music_stop_and_wait(1500);
    if (error == ESP_OK) error = network_time_begin_maintenance();
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
    if ((bits & UPDATE_EVENT_WEATHER_REFRESH) != 0U) {
        vTaskDelay(pdMS_TO_TICKS(UPDATE_SERVER_STOP_DELAY_MS));
        stop_web_server();
        (void)stop_update_ap();
        set_state(FIRMWARE_UPDATE_STATE_IDLE, ESP_OK);
        const esp_err_t weather_error =
            weather_service_request_refresh_from_maintenance();
        if (weather_error != ESP_OK) {
            network_time_end_maintenance();
            ESP_LOGW(TAG, "could not start weather refresh: %s",
                     esp_err_to_name(weather_error));
        }
        vTaskDelete(NULL);
        return;
    }
    if ((bits & UPDATE_EVENT_GALLERY_INSTALL) != 0U) {
        vTaskDelay(pdMS_TO_TICKS(UPDATE_SERVER_STOP_DELAY_MS));
        stop_web_server();
        (void)stop_update_ap();
        set_state(FIRMWARE_UPDATE_STATE_IDLE, ESP_OK);
        const esp_err_t gallery_error = gallery_download_start();
        if (gallery_error != ESP_OK) {
            network_time_end_maintenance();
            ESP_LOGW(TAG, "could not start gallery installation: %s",
                     esp_err_to_name(gallery_error));
        }
        vTaskDelete(NULL);
        return;
    }
    if ((bits & UPDATE_EVENT_REPROVISION) != 0U) {
        vTaskDelay(pdMS_TO_TICKS(UPDATE_SERVER_STOP_DELAY_MS));
        stop_web_server();
        (void)stop_update_ap();
        set_state(FIRMWARE_UPDATE_STATE_IDLE, ESP_OK);
        const esp_err_t reprovision_error =
            network_time_end_maintenance_and_request_provisioning();
        if (reprovision_error != ESP_OK) {
            network_time_end_maintenance();
            ESP_LOGW(TAG, "could not enter provisioning mode: %s",
                     esp_err_to_name(reprovision_error));
        }
        vTaskDelete(NULL);
        return;
    }
    if ((bits & UPDATE_EVENT_WIFI_CHANGED) != 0U) {
        vTaskDelay(pdMS_TO_TICKS(SETTINGS_WIFI_SUCCESS_DISPLAY_MS));
        stop_web_server();
        const esp_err_t stop_error = stop_update_ap();
        set_state(FIRMWARE_UPDATE_STATE_IDLE, ESP_OK);
        esp_err_t sync_error = stop_error;
        if (sync_error == ESP_OK) {
            sync_error = network_time_end_maintenance_and_request_sync();
        }
        if (sync_error != ESP_OK) {
            network_time_end_maintenance();
            ESP_LOGW(TAG, "could not reconnect saved Wi-Fi after change: %s",
                     esp_err_to_name(sync_error));
        }
        vTaskDelete(NULL);
        return;
    }
    if ((bits & UPDATE_EVENT_COMPLETE) != 0U) {
        ESP_LOGI(TAG, "firmware image verified; restarting into new OTA slot");
        vTaskDelay(pdMS_TO_TICKS(UPDATE_RESTART_DELAY_MS));
        boot_recovery_note_planned_restart();
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
    if (!boot_recovery_is_active()) {
        const esp_err_t conversation_error = conversation_config_init();
        if (conversation_error != ESP_OK) {
            ESP_LOGW(TAG,
                     "cloud conversation settings unavailable: %s",
                     esp_err_to_name(conversation_error));
        }
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
    ESP_LOGI(TAG, "%s and dual-slot OTA service ready",
             boot_recovery_is_active() ? "recovery settings"
                                       : "local settings");
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
    if (!s_session_closing && !s_mutation_active &&
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
