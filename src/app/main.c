#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "app_storage.h"
#include "audio_diagnostics.h"
#include "battery.h"
#include "board_buttons.h"
#include "board_i2c.h"
#include "board_pins.h"
#include "button_state.h"
#include "chinese_lunar.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "firmware_update.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_time.h"
#include "network_screen_policy.h"
#include "online_firmware_update.h"
#include "page_state.h"
#include "pcf85063.h"
#include "rtc_backup.h"
#include "shtc3.h"
#include "usb_commands.h"

static const char *TAG = "rlcd_firmware";
typedef enum {
    APP_DISPLAY_NONE = 0,
    APP_DISPLAY_DASHBOARD,
    APP_DISPLAY_NETWORK_SETUP,
    APP_DISPLAY_CALENDAR,
    APP_DISPLAY_DEVICE_HEALTH,
    APP_DISPLAY_NETWORK_TIME,
    APP_DISPLAY_AUDIO,
    APP_DISPLAY_WIFI_MAINTENANCE,
    APP_DISPLAY_ONLINE_UPDATE,
    APP_DISPLAY_LOCAL_UPDATE,
    APP_DISPLAY_MANUAL_SYNC,
    APP_DISPLAY_MANUAL_SYNC_RESULT,
    APP_DISPLAY_WIFI_RESET_PROMPT,
    APP_DISPLAY_FIRMWARE_UPDATE_PROMPT,
    APP_DISPLAY_FIRMWARE_UPDATE_STARTING,
    APP_DISPLAY_FIRMWARE_UPDATE_READY,
    APP_DISPLAY_FIRMWARE_UPDATE_RECEIVING,
    APP_DISPLAY_FIRMWARE_UPDATE_VERIFYING,
    APP_DISPLAY_FIRMWARE_UPDATE_RESULT,
} app_display_mode_t;

typedef enum {
    MANUAL_SYNC_UI_NONE = 0,
    MANUAL_SYNC_UI_ACTIVE,
    MANUAL_SYNC_UI_SUCCESS,
    MANUAL_SYNC_UI_FAILED,
    MANUAL_SYNC_UI_UNAVAILABLE,
} manual_sync_ui_t;

enum {
    APP_LOOP_INTERVAL_MS = 50,
    APP_PERIODIC_UPDATE_MS = 1000,
    APP_MANUAL_SYNC_RESULT_MS = 2000,
    APP_FIRMWARE_UPDATE_RESULT_MS = 2500,
    APP_OTA_CONFIRM_DELAY_MS = 5000,
};

static const char *page_name(app_page_t page)
{
    switch (page) {
    case APP_PAGE_CALENDAR:
        return "calendar";
    case APP_PAGE_DEVICE_HEALTH:
        return "device health";
    case APP_PAGE_NETWORK_TIME:
        return "network and time";
    case APP_PAGE_AUDIO:
        return "audio";
    case APP_PAGE_WIFI_MAINTENANCE:
        return "Wi-Fi maintenance";
    case APP_PAGE_ONLINE_UPDATE:
        return "online update";
    case APP_PAGE_LOCAL_UPDATE:
        return "local update";
    case APP_PAGE_HOME:
    default:
        return "home";
    }
}

static const char *manual_sync_detail(network_time_state_t state)
{
    switch (state) {
    case NETWORK_TIME_STATE_STARTING:
        return "Starting Wi-Fi";
    case NETWORK_TIME_STATE_CONNECTING:
        return "Connecting...";
    case NETWORK_TIME_STATE_SYNCHRONIZING:
        return "Waiting for NTP";
    default:
        return "Request accepted";
    }
}

static const char *firmware_update_error_detail(esp_err_t error)
{
    switch (error) {
    case ESP_ERR_INVALID_STATE:
        return "Wait for network task";
    case ESP_ERR_NOT_SUPPORTED:
        return "OTA partitions unavailable";
    case ESP_ERR_INVALID_SIZE:
        return "Wrong firmware size";
    case ESP_ERR_OTA_VALIDATE_FAILED:
        return "Invalid OTA firmware";
    case ESP_ERR_TIMEOUT:
        return "Upload connection timed out";
    default:
        return esp_err_to_name(error);
    }
}

static display_audio_state_t display_audio_state(
    audio_session_state_t state)
{
    switch (state) {
    case AUDIO_SESSION_STATE_PLAYING_TONE:
        return DISPLAY_AUDIO_STATE_PLAYING_TONE;
    case AUDIO_SESSION_STATE_PREPARING_RECORDING:
        return DISPLAY_AUDIO_STATE_PREPARING_RECORDING;
    case AUDIO_SESSION_STATE_RECORDING:
        return DISPLAY_AUDIO_STATE_RECORDING;
    case AUDIO_SESSION_STATE_ANALYZING:
        return DISPLAY_AUDIO_STATE_ANALYZING;
    case AUDIO_SESSION_STATE_PLAYBACK:
        return DISPLAY_AUDIO_STATE_PLAYBACK;
    case AUDIO_SESSION_STATE_COMPLETED:
        return DISPLAY_AUDIO_STATE_COMPLETED;
    case AUDIO_SESSION_STATE_CANCELLED:
        return DISPLAY_AUDIO_STATE_CANCELLED;
    case AUDIO_SESSION_STATE_FAILED:
        return DISPLAY_AUDIO_STATE_FAILED;
    case AUDIO_SESSION_STATE_IDLE:
    default:
        return DISPLAY_AUDIO_STATE_IDLE;
    }
}

static display_network_state_t dashboard_network_state(
    const network_time_status_t *status)
{
    switch (status->state) {
    case NETWORK_TIME_STATE_SYNCHRONIZED:
        return DISPLAY_NETWORK_SYNCHRONIZED;
    case NETWORK_TIME_STATE_UNINITIALIZED:
    case NETWORK_TIME_STATE_STARTING:
    case NETWORK_TIME_STATE_CONNECTING:
    case NETWORK_TIME_STATE_SYNCHRONIZING:
        return DISPLAY_NETWORK_CONNECTING;
    case NETWORK_TIME_STATE_PROVISIONING:
        return status->configured ? DISPLAY_NETWORK_ERROR
                                  : DISPLAY_NETWORK_UNCONFIGURED;
    case NETWORK_TIME_STATE_RETRY_WAIT:
    case NETWORK_TIME_STATE_ERROR:
        return status->configured ? DISPLAY_NETWORK_ERROR
                                  : DISPLAY_NETWORK_UNCONFIGURED;
    default:
        return DISPLAY_NETWORK_ERROR;
    }
}

static const char *device_network_state_name(
    const network_time_status_t *status)
{
    switch (status->state) {
    case NETWORK_TIME_STATE_UNINITIALIZED:
        return "UNINITIALIZED";
    case NETWORK_TIME_STATE_STARTING:
        return "STARTING";
    case NETWORK_TIME_STATE_PROVISIONING:
        return "PROVISIONING";
    case NETWORK_TIME_STATE_CONNECTING:
        return "CONNECTING";
    case NETWORK_TIME_STATE_SYNCHRONIZING:
        return "SYNCHRONIZING";
    case NETWORK_TIME_STATE_SYNCHRONIZED:
        return "SYNCHRONIZED";
    case NETWORK_TIME_STATE_RETRY_WAIT:
        if (status->last_failure == NETWORK_TIME_FAILURE_WIFI) {
            return "WI-FI OFFLINE";
        }
        if (status->last_failure == NETWORK_TIME_FAILURE_NTP) {
            return "NTP UNAVAILABLE";
        }
        if (status->last_failure == NETWORK_TIME_FAILURE_SERVICE) {
            return "SERVICE ERROR";
        }
        return "OFFLINE";
    case NETWORK_TIME_STATE_ERROR:
    default:
        return "ERROR";
    }
}

static void configure_key_timing(
    button_state_t *state,
    app_page_t page,
    online_update_state_t online_update_state)
{
    uint32_t action_threshold_ms = app_page_key_hold_threshold_ms(page);
    const app_page_action_t action = app_page_key_hold_action(page);
    if (page == APP_PAGE_ONLINE_UPDATE &&
        online_update_state == ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION) {
        action_threshold_ms = APP_PAGE_ONLINE_UPDATE_INSTALL_HOLD_MS;
    }
    if (action == APP_PAGE_ACTION_RESET_WIFI ||
        action == APP_PAGE_ACTION_START_LOCAL_UPDATE ||
        (page == APP_PAGE_ONLINE_UPDATE &&
         online_update_state ==
             ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION)) {
        (void)button_state_set_timing(state, BUTTON_HOLD_PROMPT_MS,
                                      action_threshold_ms);
    } else if (action_threshold_ms > 0U) {
        (void)button_state_set_timing(state, action_threshold_ms,
                                      action_threshold_ms);
    } else {
        (void)button_state_set_timing(state, BUTTON_HOLD_PROMPT_MS,
                                      BUTTON_LONG_PRESS_DISABLED_MS);
    }
}

static esp_err_t seed_system_time_from_rtc(
    const pcf85063_datetime_t *datetime)
{
    if (datetime == NULL || !datetime->clock_integrity) {
        return ESP_ERR_INVALID_ARG;
    }
    struct tm local = {
        .tm_sec = datetime->second,
        .tm_min = datetime->minute,
        .tm_hour = datetime->hour,
        .tm_mday = datetime->day,
        .tm_mon = (int)datetime->month - 1,
        .tm_year = (int)datetime->year - 1900,
        .tm_isdst = -1,
    };
    const time_t seconds = mktime(&local);
    if (seconds == (time_t)-1) {
        return ESP_ERR_INVALID_ARG;
    }
    const struct timeval value = {
        .tv_sec = seconds,
        .tv_usec = 0,
    };
    return settimeofday(&value, NULL) == 0 ? ESP_OK : ESP_FAIL;
}

static display_online_update_state_t display_online_update_state(
    online_update_state_t state)
{
    switch (state) {
    case ONLINE_UPDATE_STATE_CHECKING:
        return DISPLAY_ONLINE_UPDATE_STATE_CHECKING;
    case ONLINE_UPDATE_STATE_UP_TO_DATE:
        return DISPLAY_ONLINE_UPDATE_STATE_UP_TO_DATE;
    case ONLINE_UPDATE_STATE_AVAILABLE:
        return DISPLAY_ONLINE_UPDATE_STATE_UPDATE_AVAILABLE;
    case ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION:
        return DISPLAY_ONLINE_UPDATE_STATE_CONFIRM_INSTALL;
    case ONLINE_UPDATE_STATE_CONNECTING:
        return DISPLAY_ONLINE_UPDATE_STATE_CONNECTING;
    case ONLINE_UPDATE_STATE_DOWNLOADING:
        return DISPLAY_ONLINE_UPDATE_STATE_DOWNLOADING;
    case ONLINE_UPDATE_STATE_VERIFYING:
        return DISPLAY_ONLINE_UPDATE_STATE_VERIFYING;
    case ONLINE_UPDATE_STATE_SUCCESS:
        return DISPLAY_ONLINE_UPDATE_STATE_SUCCESS;
    case ONLINE_UPDATE_STATE_FAILED:
        return DISPLAY_ONLINE_UPDATE_STATE_FAILED;
    case ONLINE_UPDATE_STATE_IDLE:
    default:
        return DISPLAY_ONLINE_UPDATE_STATE_NOT_CHECKED;
    }
}

static esp_err_t write_network_time_to_rtc(const network_time_datetime_t *network_time)
{
    pcf85063_datetime_t requested = {
        .year = network_time->year,
        .month = network_time->month,
        .day = network_time->day,
        .hour = network_time->hour,
        .minute = network_time->minute,
        .second = network_time->second,
        .clock_integrity = true,
    };
    esp_err_t error = pcf85063_calculate_weekday(
        requested.year, requested.month, requested.day, &requested.weekday);
    if (error != ESP_OK) {
        return error;
    }
    error = pcf85063_write(&requested);
    if (error != ESP_OK) {
        return error;
    }

    pcf85063_datetime_t verified = {0};
    error = pcf85063_read(&verified);
    if (error != ESP_OK || !verified.clock_integrity) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    ESP_LOGI(TAG, "NTP_RTC_SET_OK %04u-%02u-%02u %02u:%02u:%02u weekday=%u",
             verified.year, verified.month, verified.day, verified.hour,
             verified.minute, verified.second, verified.weekday);
    return ESP_OK;
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "%s firmware v%s (ESP-IDF %s)", BOARD_NAME, app->version, app->idf_ver);
    ESP_LOGI(TAG, "PSRAM available: %u KiB", (unsigned)(psram_bytes / 1024U));
    ESP_LOGI(TAG, "dashboard with SoftAP provisioning, NVS Wi-Fi settings, and SNTP RTC sync");

    const bool display_ready = display_init() == ESP_OK;
    if (display_ready) {
        display_show_status("RLCD FIRMWARE", "Starting dashboard");
    } else {
        ESP_LOGE(TAG, "ST7305 display initialization failed");
    }

    const esp_err_t storage_error = app_storage_init();
    if (storage_error != ESP_OK) {
        ESP_LOGE(TAG, "persistent storage initialization failed: %s",
                 esp_err_to_name(storage_error));
    }

    const esp_err_t i2c_error = board_i2c_init();
    const bool i2c_ready = i2c_error == ESP_OK;
    if (!i2c_ready) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(i2c_error));
    }

    bool rtc_driver_ready = false;
    bool startup_rtc_readable = false;
    bool sensor_driver_ready = false;
    uint16_t sensor_id = 0;
    pcf85063_datetime_t startup_datetime = {0};

    if (i2c_ready) {
        esp_err_t error = board_i2c_probe(PCF85063_I2C_ADDRESS, 100);
        if (error == ESP_OK) {
            error = pcf85063_init(board_i2c_bus());
        }
        rtc_driver_ready = error == ESP_OK;
        if (rtc_driver_ready) {
            ESP_LOGI(TAG, "PCF85063 detected at I2C address 0x%02X", PCF85063_I2C_ADDRESS);
            error = pcf85063_read(&startup_datetime);
            startup_rtc_readable = error == ESP_OK;
            if (!startup_rtc_readable) {
                ESP_LOGW(TAG, "initial RTC read failed: %s",
                         esp_err_to_name(error));
            }
        } else {
            ESP_LOGW(TAG, "PCF85063 not ready: %s", esp_err_to_name(error));
        }

        error = board_i2c_probe(SHTC3_I2C_ADDRESS, 100);
        if (error == ESP_OK) {
            error = shtc3_init(board_i2c_bus(), &sensor_id);
        }
        sensor_driver_ready = error == ESP_OK;
        if (sensor_driver_ready) {
            ESP_LOGI(TAG, "SHTC3 detected at 0x%02X, sensor ID=0x%04X",
                     SHTC3_I2C_ADDRESS, sensor_id);
        } else {
            ESP_LOGW(TAG, "SHTC3 not ready: %s", esp_err_to_name(error));
        }
    }

    const esp_err_t audio_init_error =
        i2c_ready ? audio_diagnostics_init(board_i2c_bus())
                  : ESP_ERR_INVALID_STATE;
    audio_diagnostics_status_t audio_status = {0};
    audio_diagnostics_get_status(&audio_status);
    if (audio_init_error == ESP_OK) {
        ESP_LOGI(TAG, "audio diagnostics ready: ES8311 speaker and ES7210 microphones");
    } else {
        ESP_LOGW(TAG,
                 "audio diagnostics partially unavailable: speaker=%d microphones=%d error=%s",
                 audio_status.speaker_ready,
                 audio_status.microphones_ready,
                 esp_err_to_name(audio_init_error));
    }

    bool rtc_backup_monitor_ready = false;
    if (storage_error == ESP_OK) {
        const esp_reset_reason_t reset_reason = esp_reset_reason();
        const esp_err_t error = rtc_backup_monitor_init(
            reset_reason == ESP_RST_POWERON, startup_rtc_readable,
            startup_rtc_readable ? &startup_datetime : NULL);
        rtc_backup_monitor_ready = error == ESP_OK;
        if (rtc_backup_monitor_ready) {
            ESP_LOGI(TAG, "RTC backup status: %s (reset reason %d)",
                     rtc_backup_status_name(rtc_backup_monitor_status()),
                     (int)reset_reason);
        } else {
            ESP_LOGW(TAG, "RTC backup monitor unavailable: %s",
                     esp_err_to_name(error));
        }
    }

    const esp_err_t battery_init_error = battery_init();
    const bool battery_driver_ready = battery_init_error == ESP_OK;
    if (battery_driver_ready) {
        ESP_LOGI(TAG, "battery monitor ready on GPIO %d", BOARD_BATTERY_ADC_GPIO);
    } else {
        ESP_LOGW(TAG, "battery monitor unavailable: %s", esp_err_to_name(battery_init_error));
    }

    const esp_err_t button_init_error = board_buttons_init();
    const bool buttons_ready = button_init_error == ESP_OK;
    if (buttons_ready) {
        ESP_LOGI(TAG, "BOOT button ready on GPIO %d; KEY button ready on GPIO %d",
                 BOARD_BOOT_GPIO, BOARD_KEY_GPIO);
    } else {
        ESP_LOGW(TAG, "board buttons unavailable: %s",
                 esp_err_to_name(button_init_error));
    }

    const esp_err_t network_error = network_time_init();
    if (network_error != ESP_OK) {
        ESP_LOGW(TAG, "automatic network time unavailable: %s",
                 esp_err_to_name(network_error));
    }
    if (network_error == ESP_OK && startup_rtc_readable &&
        startup_datetime.clock_integrity) {
        const esp_err_t seed_error =
            seed_system_time_from_rtc(&startup_datetime);
        if (seed_error == ESP_OK) {
            ESP_LOGI(TAG, "system clock initialized from RTC for HTTPS");
        } else {
            ESP_LOGW(TAG, "could not initialize system clock from RTC: %s",
                     esp_err_to_name(seed_error));
        }
    }

    const esp_err_t usb_commands_error = usb_commands_init();
    if (usb_commands_error != ESP_OK) {
        ESP_LOGW(TAG, "USB command console unavailable: %s",
                 esp_err_to_name(usb_commands_error));
    }

    const esp_err_t firmware_update_error = firmware_update_init();
    if (firmware_update_error != ESP_OK) {
        ESP_LOGW(TAG, "firmware update service unavailable: %s",
                 esp_err_to_name(firmware_update_error));
    }
    const esp_err_t online_update_error =
        online_firmware_update_init(app->version);
    if (online_update_error != ESP_OK) {
        ESP_LOGW(TAG, "online update service unavailable: %s",
                 esp_err_to_name(online_update_error));
    }
    bool running_image_confirmation_pending =
        firmware_update_error == ESP_OK && display_ready && buttons_ready &&
        network_error == ESP_OK;

    display_dashboard_t dashboard = {
        .lunar_text = NULL,
    };
    pcf85063_datetime_t datetime = {0};
    shtc3_measurement_t measurement = {0};
    battery_measurement_t battery_measurement = {0};
    chinese_lunar_date_t lunar_date = {0};
    char lunar_text[64] = {0};
    network_time_status_t network_status = {0};
    network_time_datetime_t last_sync_time = {0};
    bool last_sync_valid = false;
    firmware_update_status_t firmware_update_status = {0};
    (void)firmware_update_get_status(&firmware_update_status);
    online_firmware_update_status_t online_update_status = {0};
    (void)online_firmware_update_get_status(&online_update_status);
    button_state_t key_button_state;
    button_state_t boot_button_state;
    button_state_init_custom(&key_button_state,
                             buttons_ready && board_key_is_pressed(),
                             BUTTON_HOLD_PROMPT_MS,
                             BUTTON_LONG_PRESS_DISABLED_MS);
    button_state_init_custom(&boot_button_state,
                             buttons_ready && board_boot_is_pressed(),
                             BUTTON_HOLD_PROMPT_MS,
                             BUTTON_LONG_PRESS_DISABLED_MS);
    app_page_state_t page_state;
    app_page_state_init(&page_state);
    const TickType_t initial_tick = xTaskGetTickCount();
    TickType_t last_button_update = initial_tick;
    TickType_t last_periodic_update = initial_tick;
    TickType_t manual_sync_ui_started = initial_tick;
    TickType_t firmware_update_result_started = initial_tick;
    network_time_state_t previous_network_state = NETWORK_TIME_STATE_UNINITIALIZED;
    network_time_state_t previous_manual_sync_network_state =
        NETWORK_TIME_STATE_UNINITIALIZED;
    TickType_t provisioning_started = initial_tick;
    app_display_mode_t previous_display_mode = APP_DISPLAY_NONE;
    manual_sync_ui_t manual_sync_ui = MANUAL_SYNC_UI_NONE;
    esp_err_t manual_sync_error = ESP_OK;
    bool first_periodic_update = true;
    uint8_t previous_reset_seconds = 0U;
    uint8_t previous_update_seconds = 0U;
    uint8_t previous_update_percent = 0U;
    bool setup_screen_dismissed = false;
    firmware_update_state_t previous_update_state =
        FIRMWARE_UPDATE_STATE_IDLE;
    online_update_state_t previous_online_update_state =
        ONLINE_UPDATE_STATE_IDLE;
    uint8_t previous_online_update_percent = 0U;
    bool automatic_update_check_pending = false;
    uint32_t cycle = 0;

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        const uint32_t button_elapsed_ms =
            (uint32_t)(now - last_button_update) * portTICK_PERIOD_MS;
        last_button_update = now;
        bool render_requested = false;
        bool calendar_data_changed = false;
        bool device_health_data_changed = false;
        bool network_time_data_changed = false;
        bool wifi_maintenance_data_changed = false;
        bool online_update_data_changed = false;
        audio_diagnostics_status_t latest_audio_status = {0};
        audio_diagnostics_get_status(&latest_audio_status);
        const bool audio_data_changed =
            latest_audio_status.revision != audio_status.revision;
        audio_status = latest_audio_status;
        const bool audio_session_active =
            audio_session_state_is_active(audio_status.state);

        (void)firmware_update_get_status(&firmware_update_status);
        if (firmware_update_status.state != previous_update_state ||
            firmware_update_status.percent != previous_update_percent) {
            if (firmware_update_status.state != previous_update_state &&
                firmware_update_state_is_dismissible(
                    firmware_update_status.state)) {
                firmware_update_result_started = now;
            }
            previous_update_state = firmware_update_status.state;
            previous_update_percent = firmware_update_status.percent;
            render_requested = true;
        }
        if (firmware_update_state_is_dismissible(
                firmware_update_status.state) &&
            now - firmware_update_result_started >=
                pdMS_TO_TICKS(APP_FIRMWARE_UPDATE_RESULT_MS)) {
            (void)firmware_update_dismiss_result();
            (void)firmware_update_get_status(&firmware_update_status);
            previous_update_state = firmware_update_status.state;
            previous_update_percent = firmware_update_status.percent;
            render_requested = true;
        }
        bool firmware_update_ui_active =
            firmware_update_status.state != FIRMWARE_UPDATE_STATE_IDLE;

        (void)online_firmware_update_get_status(&online_update_status);
        if (online_update_status.state != previous_online_update_state ||
            online_update_status.percent != previous_online_update_percent) {
            previous_online_update_state = online_update_status.state;
            previous_online_update_percent = online_update_status.percent;
            online_update_data_changed = true;
            if (app_page_state_current(&page_state) ==
                APP_PAGE_ONLINE_UPDATE) {
                render_requested = true;
            }
        }
        const bool online_update_busy =
            online_update_state_is_busy(online_update_status.state);
        const bool online_update_confirmation_active =
            online_update_status.state ==
            ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION;

        button_event_t key_event = BUTTON_EVENT_NONE;
        button_event_t boot_event = BUTTON_EVENT_NONE;
        bool key_pressed = false;
        bool boot_pressed = false;
        const app_page_t input_page = app_page_state_current(&page_state);
        if (buttons_ready) {
            configure_key_timing(&key_button_state, input_page,
                                 online_update_status.state);
            key_pressed = board_key_is_pressed();
            boot_pressed = board_boot_is_pressed();
            key_event = button_state_update(
                &key_button_state, key_pressed, button_elapsed_ms);
            boot_event = button_state_update(
                &boot_button_state, boot_pressed, button_elapsed_ms);
        }
        if (audio_session_active) {
            audio_session_input_t audio_input = AUDIO_SESSION_INPUT_NONE;
            if (boot_event == BUTTON_EVENT_SHORT_PRESS) {
                audio_input = AUDIO_SESSION_INPUT_BOOT_SHORT_PRESS;
            } else if (key_event == BUTTON_EVENT_SHORT_PRESS) {
                audio_input = AUDIO_SESSION_INPUT_KEY_SHORT_PRESS;
            }
            const audio_session_action_t audio_action =
                audio_session_input_action(audio_status.state, audio_input);
            if (audio_action == AUDIO_SESSION_ACTION_CANCEL) {
                const esp_err_t cancel_error = audio_diagnostics_cancel();
                if (cancel_error == ESP_OK) {
                    ESP_LOGI(TAG,
                             "BOOT short press: cancelling temporary audio");
                }
                render_requested = true;
            } else if (audio_action == AUDIO_SESSION_ACTION_STOP) {
                const esp_err_t stop_error =
                    audio_diagnostics_request_stop();
                if (stop_error == ESP_OK) {
                    ESP_LOGI(TAG, "KEY short press: stopping %s",
                             audio_session_state_name(audio_status.state));
                }
                render_requested = true;
            }
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
        }
        if (key_event == BUTTON_EVENT_SHORT_PRESS) {
            if (manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                !firmware_update_ui_active &&
                !online_update_confirmation_active &&
                !(online_update_busy &&
                  input_page == APP_PAGE_ONLINE_UPDATE)) {
                if (previous_display_mode == APP_DISPLAY_NETWORK_SETUP) {
                    setup_screen_dismissed = true;
                }
                app_page_state_key_short_press(&page_state);
                render_requested = true;
                ESP_LOGI(TAG, "KEY short press: showing %s page",
                         page_name(app_page_state_current(&page_state)));
            }
        } else if (key_event == BUTTON_EVENT_HOLD_CANCELLED) {
            app_page_state_note_activity(&page_state);
            render_requested = true;
            if (input_page == APP_PAGE_WIFI_MAINTENANCE) {
                ESP_LOGI(TAG,
                         "KEY hold cancelled; Wi-Fi settings unchanged");
            } else if (input_page == APP_PAGE_LOCAL_UPDATE) {
                ESP_LOGI(TAG,
                         "KEY hold cancelled; local update not started");
            } else if (input_page == APP_PAGE_ONLINE_UPDATE) {
                ESP_LOGI(TAG,
                         "KEY hold cancelled; online update unchanged");
            } else {
                ESP_LOGI(TAG, "KEY hold released without an action");
            }
        } else if (key_event == BUTTON_EVENT_LONG_PRESS) {
            app_page_state_note_activity(&page_state);
            const app_page_action_t key_action =
                app_page_key_hold_action(input_page);
            if (key_action == APP_PAGE_ACTION_SYNC_TIME &&
                manual_sync_ui != MANUAL_SYNC_UI_ACTIVE &&
                !online_update_busy) {
                manual_sync_error = network_time_request_sync();
                manual_sync_ui_started = now;
                previous_manual_sync_network_state =
                    NETWORK_TIME_STATE_UNINITIALIZED;
                if (manual_sync_error == ESP_OK) {
                    manual_sync_ui = MANUAL_SYNC_UI_ACTIVE;
                    ESP_LOGI(TAG,
                             "KEY long press: manual time sync started");
                } else {
                    manual_sync_ui = MANUAL_SYNC_UI_UNAVAILABLE;
                    ESP_LOGW(TAG,
                             "KEY long press: manual time sync unavailable: %s",
                             esp_err_to_name(manual_sync_error));
                }
                render_requested = true;
            } else if (key_action == APP_PAGE_ACTION_RESET_WIFI &&
                       manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                       !online_update_busy) {
                ESP_LOGW(TAG, "KEY long press: clearing Wi-Fi settings");
                if (display_ready) {
                    display_show_status("RESET WI-FI", "Clearing settings");
                }
                const esp_err_t clear_error =
                    network_time_clear_credentials();
                if (clear_error == ESP_OK) {
                    if (display_ready) {
                        display_show_status("WI-FI RESET",
                                            "Restarting setup");
                    }
                    ESP_LOGI(TAG,
                             "Wi-Fi settings cleared; restarting into setup mode");
                    vTaskDelay(pdMS_TO_TICKS(500U));
                    esp_restart();
                    continue;
                }
                ESP_LOGE(TAG, "could not clear Wi-Fi settings: %s",
                         esp_err_to_name(clear_error));
                if (display_ready) {
                    display_show_status("RESET FAILED",
                                        "Use USB RESET_WIFI");
                }
                vTaskDelay(pdMS_TO_TICKS(1500U));
                render_requested = true;
                previous_display_mode = APP_DISPLAY_NONE;
            } else if (key_action == APP_PAGE_ACTION_TEST_AUDIO &&
                       manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                       !firmware_update_ui_active &&
                       !online_update_busy) {
                const esp_err_t start_error = audio_diagnostics_start();
                if (start_error == ESP_OK) {
                    ESP_LOGI(TAG,
                             "KEY long press: temporary audio loopback started");
                } else {
                    ESP_LOGW(TAG,
                             "temporary audio loopback could not start: %s",
                             esp_err_to_name(start_error));
                }
                previous_display_mode = APP_DISPLAY_NONE;
                render_requested = true;
            } else if (key_action ==
                           APP_PAGE_ACTION_CHECK_ONLINE_UPDATE &&
                       manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                       !firmware_update_ui_active) {
                esp_err_t action_error = ESP_ERR_INVALID_STATE;
                if (online_update_status.state ==
                    ONLINE_UPDATE_STATE_AVAILABLE) {
                    action_error =
                        online_firmware_update_request_confirmation();
                    ESP_LOGI(TAG,
                             "KEY long press: reviewing online update");
                } else if (online_update_status.state ==
                           ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION) {
                    action_error =
                        online_firmware_update_start_install();
                    ESP_LOGI(TAG,
                             "KEY long press: online update confirmed");
                } else if (!online_update_busy) {
                    action_error =
                        online_firmware_update_request_check();
                    ESP_LOGI(TAG,
                             "KEY long press: checking online update");
                }
                if (action_error != ESP_OK) {
                    ESP_LOGW(TAG, "online update action unavailable: %s",
                             esp_err_to_name(action_error));
                }
                (void)online_firmware_update_get_status(
                    &online_update_status);
                previous_online_update_state =
                    online_update_status.state;
                previous_online_update_percent =
                    online_update_status.percent;
                render_requested = true;
            } else if (key_action ==
                           APP_PAGE_ACTION_START_LOCAL_UPDATE &&
                       manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                       !firmware_update_ui_active &&
                       !online_update_busy &&
                       !online_update_confirmation_active) {
                const esp_err_t start_error = firmware_update_start();
                if (start_error != ESP_OK) {
                    ESP_LOGW(TAG, "could not start firmware update: %s",
                             esp_err_to_name(start_error));
                } else {
                    ESP_LOGI(TAG,
                             "KEY long press: local firmware update started");
                }
                (void)firmware_update_get_status(&firmware_update_status);
                firmware_update_ui_active =
                    firmware_update_status.state !=
                    FIRMWARE_UPDATE_STATE_IDLE;
                previous_update_state = firmware_update_status.state;
                previous_update_percent = firmware_update_status.percent;
                render_requested = true;
            }
        }

        if (boot_event == BUTTON_EVENT_SHORT_PRESS) {
            if (firmware_update_status.state ==
                    FIRMWARE_UPDATE_STATE_STARTING ||
                firmware_update_status.state ==
                    FIRMWARE_UPDATE_STATE_READY) {
                const esp_err_t cancel_error = firmware_update_cancel();
                if (cancel_error == ESP_OK) {
                    ESP_LOGI(TAG, "BOOT short press: closing update mode");
                }
                render_requested = true;
            } else if (firmware_update_ui_active) {
                ESP_LOGI(TAG,
                         "BOOT short press ignored while firmware update is active");
            } else if (input_page == APP_PAGE_ONLINE_UPDATE &&
                       (online_update_status.state ==
                            ONLINE_UPDATE_STATE_CHECKING ||
                        online_update_status.state ==
                            ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION ||
                        online_update_status.state ==
                            ONLINE_UPDATE_STATE_CONNECTING)) {
                const esp_err_t cancel_error =
                    online_firmware_update_cancel();
                if (cancel_error == ESP_OK) {
                    ESP_LOGI(TAG,
                             "BOOT short press: cancelling online update action");
                }
                render_requested = true;
            } else if (input_page == APP_PAGE_ONLINE_UPDATE &&
                       online_update_busy) {
                ESP_LOGI(TAG,
                         "BOOT short press ignored while online update is active");
            } else if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE) {
                ESP_LOGI(TAG, "BOOT short press ignored while time sync is active");
            } else if (previous_display_mode == APP_DISPLAY_NETWORK_SETUP) {
                setup_screen_dismissed = true;
                app_page_state_init(&page_state);
                render_requested = true;
                ESP_LOGI(TAG,
                         "BOOT short press: continuing with offline dashboard");
            } else {
                manual_sync_ui = MANUAL_SYNC_UI_NONE;
                app_page_state_boot_short_press(&page_state);
                render_requested = true;
                ESP_LOGI(TAG, "BOOT short press: showing %s page",
                         page_name(app_page_state_current(&page_state)));
            }
        }

        const uint32_t button_hold_ms =
            button_state_hold_ms(&key_button_state);
        const bool reset_prompt_active =
            buttons_ready && input_page == APP_PAGE_WIFI_MAINTENANCE &&
            manual_sync_ui == MANUAL_SYNC_UI_NONE &&
            button_hold_ms >= BUTTON_HOLD_PROMPT_MS &&
            button_hold_ms < APP_PAGE_WIFI_RESET_HOLD_MS;
        const bool firmware_update_prompt_active =
            buttons_ready && input_page == APP_PAGE_LOCAL_UPDATE &&
            manual_sync_ui == MANUAL_SYNC_UI_NONE &&
            !firmware_update_ui_active && !online_update_busy &&
            !online_update_confirmation_active &&
            button_hold_ms >= BUTTON_HOLD_PROMPT_MS &&
            button_hold_ms < APP_PAGE_LOCAL_UPDATE_HOLD_MS;
        uint8_t reset_seconds_remaining = 0U;
        if (reset_prompt_active) {
            reset_seconds_remaining = (uint8_t)(
                (APP_PAGE_WIFI_RESET_HOLD_MS - button_hold_ms + 999U) /
                1000U);
        }
        uint8_t update_seconds_remaining = 0U;
        if (firmware_update_prompt_active) {
            update_seconds_remaining = (uint8_t)(
                (APP_PAGE_LOCAL_UPDATE_HOLD_MS - button_hold_ms + 999U) /
                1000U);
        }

        const bool button_interaction_active =
            key_pressed || boot_pressed ||
            button_state_is_pressed(&key_button_state) ||
            button_state_is_pressed(&boot_button_state);
        if (button_interaction_active) {
            app_page_state_note_activity(&page_state);
        }

        if (manual_sync_ui != MANUAL_SYNC_UI_NONE &&
            manual_sync_ui != MANUAL_SYNC_UI_ACTIVE &&
            now - manual_sync_ui_started >=
                pdMS_TO_TICKS(APP_MANUAL_SYNC_RESULT_MS)) {
            manual_sync_ui = MANUAL_SYNC_UI_NONE;
            render_requested = true;
        }
        if (manual_sync_ui == MANUAL_SYNC_UI_NONE && !reset_prompt_active &&
            !firmware_update_prompt_active && !firmware_update_ui_active &&
            !(input_page == APP_PAGE_ONLINE_UPDATE &&
              (online_update_busy ||
               online_update_confirmation_active)) &&
            !audio_session_active && !button_interaction_active &&
            app_page_state_tick(&page_state, button_elapsed_ms)) {
            render_requested = true;
            ESP_LOGI(TAG, "page timeout: returning home");
        }

        usb_commands_poll(rtc_driver_ready);

        if (running_image_confirmation_pending &&
            now - initial_tick >= pdMS_TO_TICKS(APP_OTA_CONFIRM_DELAY_MS)) {
            const esp_err_t confirm_error =
                firmware_update_confirm_running_image();
            if (confirm_error == ESP_OK) {
                ESP_LOGI(TAG,
                         "running OTA image confirmed after stable startup");
            } else {
                ESP_LOGE(TAG, "could not confirm running OTA image: %s",
                         esp_err_to_name(confirm_error));
            }
            running_image_confirmation_pending = false;
        }

        const bool periodic_update =
            first_periodic_update ||
            now - last_periodic_update >=
                pdMS_TO_TICKS(APP_PERIODIC_UPDATE_MS);
        if (periodic_update) {
            first_periodic_update = false;
            last_periodic_update = now;

            network_time_datetime_t synchronized_time = {0};
            if (network_time_take_datetime(&synchronized_time)) {
                last_sync_time = synchronized_time;
                last_sync_valid = true;
                automatic_update_check_pending = true;
                network_time_data_changed = true;
                esp_err_t rtc_sync_error = ESP_OK;
                if (!rtc_driver_ready) {
                    rtc_sync_error = ESP_ERR_INVALID_STATE;
                    ESP_LOGW(TAG, "SNTP succeeded but PCF85063 is unavailable");
                } else {
                    rtc_sync_error =
                        write_network_time_to_rtc(&synchronized_time);
                    if (rtc_sync_error != ESP_OK) {
                        ESP_LOGW(TAG, "could not write SNTP time to RTC: %s",
                                 esp_err_to_name(rtc_sync_error));
                    }
                }
                if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE) {
                    manual_sync_error = rtc_sync_error;
                    manual_sync_ui = rtc_sync_error == ESP_OK
                                         ? MANUAL_SYNC_UI_SUCCESS
                                         : MANUAL_SYNC_UI_FAILED;
                    manual_sync_ui_started = now;
                    render_requested = true;
                }
            }

            if (rtc_driver_ready) {
                const esp_err_t error = pcf85063_read(&datetime);
                if (error == ESP_OK) {
                    const bool rtc_display_changed =
                        dashboard.time_valid != datetime.clock_integrity ||
                        (datetime.clock_integrity &&
                         (dashboard.year != datetime.year ||
                          dashboard.month != datetime.month ||
                          dashboard.day != datetime.day ||
                          dashboard.hour != datetime.hour ||
                          dashboard.minute != datetime.minute));
                    dashboard.time_valid = datetime.clock_integrity;
                    dashboard.year = datetime.year;
                    dashboard.month = datetime.month;
                    dashboard.day = datetime.day;
                    dashboard.weekday = datetime.weekday;
                    dashboard.hour = datetime.hour;
                    dashboard.minute = datetime.minute;
                    dashboard.second = datetime.second;
                    dashboard.lunar_valid =
                        datetime.clock_integrity &&
                        chinese_lunar_from_gregorian(
                            datetime.year, datetime.month, datetime.day,
                            &lunar_date) &&
                        chinese_lunar_format(&lunar_date, lunar_text,
                                             sizeof(lunar_text));
                    dashboard.lunar_text =
                        dashboard.lunar_valid ? lunar_text : NULL;
                    if (rtc_backup_monitor_ready &&
                        datetime.clock_integrity) {
                        const esp_err_t arm_error =
                            rtc_backup_monitor_arm(&datetime);
                        if (arm_error != ESP_OK && (cycle % 10U) == 0U) {
                            ESP_LOGW(TAG,
                                     "could not arm RTC backup monitor: %s",
                                     esp_err_to_name(arm_error));
                        }
                    }
                    if (rtc_display_changed) {
                        calendar_data_changed = true;
                        device_health_data_changed = true;
                        network_time_data_changed = true;
                    }
                } else {
                    const bool rtc_display_changed = dashboard.time_valid;
                    dashboard.time_valid = false;
                    dashboard.lunar_valid = false;
                    dashboard.lunar_text = NULL;
                    if ((cycle % 10U) == 0U) {
                        ESP_LOGW(TAG, "RTC read failed: %s",
                                 esp_err_to_name(error));
                    }
                    if (rtc_display_changed) {
                        calendar_data_changed = true;
                        device_health_data_changed = true;
                        network_time_data_changed = true;
                    }
                }
            }

            if (sensor_driver_ready && (cycle % 5U) == 0U) {
                const esp_err_t error = shtc3_read(&measurement);
                if (error == ESP_OK) {
                    const bool sensor_display_changed =
                        !dashboard.environment_valid ||
                        dashboard.temperature_c != measurement.temperature_c ||
                        dashboard.humidity_percent !=
                            measurement.humidity_percent;
                    dashboard.environment_valid = true;
                    dashboard.temperature_c = measurement.temperature_c;
                    dashboard.humidity_percent = measurement.humidity_percent;
                    if (dashboard.time_valid) {
                        ESP_LOGI(
                            TAG,
                            "RTC %04u-%02u-%02u %02u:%02u:%02u, temp %.1f C, humidity %.1f %%",
                            datetime.year, datetime.month, datetime.day,
                            datetime.hour, datetime.minute, datetime.second,
                            (double)measurement.temperature_c,
                            (double)measurement.humidity_percent);
                    } else {
                        ESP_LOGI(TAG,
                                 "RTC time invalid, temp %.1f C, humidity %.1f %%",
                                 (double)measurement.temperature_c,
                                 (double)measurement.humidity_percent);
                    }
                    device_health_data_changed |= sensor_display_changed;
                } else {
                    device_health_data_changed |=
                        dashboard.environment_valid;
                    dashboard.environment_valid = false;
                    ESP_LOGW(TAG, "SHTC3 read failed: %s",
                             esp_err_to_name(error));
                }
            }

            if (battery_driver_ready && (cycle % 30U) == 0U) {
                const uint16_t previous_voltage_mv =
                    battery_measurement.voltage_mv;
                const esp_err_t error = battery_read(&battery_measurement);
                if (error == ESP_OK) {
                    const bool battery_display_changed =
                        !dashboard.battery_valid ||
                        dashboard.battery_percent !=
                            battery_measurement.percent ||
                        previous_voltage_mv !=
                            battery_measurement.voltage_mv;
                    dashboard.battery_valid = true;
                    dashboard.battery_percent = battery_measurement.percent;
                    if ((cycle % 300U) == 0U) {
                        ESP_LOGI(TAG, "battery %u mV, %u%% (%s)",
                                 battery_measurement.voltage_mv,
                                 battery_measurement.percent,
                                 battery_measurement.calibrated ? "calibrated"
                                                                : "nominal");
                    }
                    device_health_data_changed |= battery_display_changed;
                } else {
                    device_health_data_changed |= dashboard.battery_valid;
                    dashboard.battery_valid = false;
                    if ((cycle % 300U) == 0U) {
                        ESP_LOGW(TAG, "battery read failed: %s",
                                 esp_err_to_name(error));
                    }
                }
            }

            const bool previous_network_configured =
                network_status.configured;
            (void)network_time_get_status(&network_status);
            dashboard.network_state = dashboard_network_state(&network_status);
            if (network_status.state != previous_network_state ||
                network_status.configured != previous_network_configured) {
                if (network_status.state == NETWORK_TIME_STATE_PROVISIONING) {
                    provisioning_started = now;
                    setup_screen_dismissed = false;
                }
                previous_network_state = network_status.state;
                network_time_data_changed = true;
                wifi_maintenance_data_changed = true;
                render_requested = true;
            }
            if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE &&
                (network_status.state == NETWORK_TIME_STATE_RETRY_WAIT ||
                 network_status.state == NETWORK_TIME_STATE_ERROR ||
                 (network_status.state == NETWORK_TIME_STATE_PROVISIONING &&
                  network_status.configured))) {
                manual_sync_error = network_status.last_error;
                manual_sync_ui = MANUAL_SYNC_UI_FAILED;
                manual_sync_ui_started = now;
                render_requested = true;
            }
            ++cycle;
        }

        if (automatic_update_check_pending &&
            online_update_error == ESP_OK &&
            network_status.state == NETWORK_TIME_STATE_SYNCHRONIZED &&
            !firmware_update_ui_active && !online_update_busy &&
            !online_update_confirmation_active) {
            if (online_update_status.state == ONLINE_UPDATE_STATE_AVAILABLE) {
                automatic_update_check_pending = false;
            } else {
                const esp_err_t check_error =
                    online_firmware_update_request_check();
                if (check_error == ESP_OK) {
                    automatic_update_check_pending = false;
                    ESP_LOGI(TAG,
                             "automatic online update check requested");
                } else if (check_error != ESP_ERR_INVALID_STATE) {
                    automatic_update_check_pending = false;
                    ESP_LOGW(TAG,
                             "automatic online update check unavailable: %s",
                             esp_err_to_name(check_error));
                }
            }
        }

        const app_page_t active_page = app_page_state_current(&page_state);
        if (display_ready && firmware_update_prompt_active) {
            if (previous_display_mode !=
                    APP_DISPLAY_FIRMWARE_UPDATE_PROMPT ||
                update_seconds_remaining != previous_update_seconds) {
                char detail[40];
                snprintf(detail, sizeof(detail), "Keep holding: %us",
                         update_seconds_remaining);
                display_show_status("START LOCAL UPDATE", detail);
            }
            previous_display_mode = APP_DISPLAY_FIRMWARE_UPDATE_PROMPT;
            previous_update_seconds = update_seconds_remaining;
        } else if (display_ready && firmware_update_ui_active) {
            app_display_mode_t update_display_mode =
                APP_DISPLAY_FIRMWARE_UPDATE_RESULT;
            if (firmware_update_status.state ==
                FIRMWARE_UPDATE_STATE_STARTING) {
                update_display_mode =
                    APP_DISPLAY_FIRMWARE_UPDATE_STARTING;
            } else if (firmware_update_status.state ==
                       FIRMWARE_UPDATE_STATE_READY) {
                update_display_mode = APP_DISPLAY_FIRMWARE_UPDATE_READY;
            } else if (firmware_update_status.state ==
                       FIRMWARE_UPDATE_STATE_RECEIVING) {
                update_display_mode =
                    APP_DISPLAY_FIRMWARE_UPDATE_RECEIVING;
            } else if (firmware_update_status.state ==
                       FIRMWARE_UPDATE_STATE_VERIFYING) {
                update_display_mode =
                    APP_DISPLAY_FIRMWARE_UPDATE_VERIFYING;
            }

            if (render_requested ||
                previous_display_mode != update_display_mode) {
                if (firmware_update_status.state ==
                    FIRMWARE_UPDATE_STATE_STARTING) {
                    display_show_status("STARTING UPDATE",
                                        "Opening temporary Wi-Fi");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_READY) {
                    display_show_firmware_update_ready(
                        firmware_update_status.access_point_ssid,
                        firmware_update_status.access_point_password,
                        firmware_update_status.access_url);
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_RECEIVING) {
                    char detail[64];
                    snprintf(
                        detail, sizeof(detail), "%u%% | %u / %u KiB",
                        firmware_update_status.percent,
                        (unsigned)(firmware_update_status.received_bytes /
                                   1024U),
                        (unsigned)((firmware_update_status.total_bytes +
                                    1023U) /
                                   1024U));
                    display_show_status("RECEIVING UPDATE", detail);
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_VERIFYING) {
                    display_show_status("VERIFYING UPDATE",
                                        "Checking firmware image");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_SUCCESS) {
                    display_show_status("UPDATE COMPLETE",
                                        "Restarting safely");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_EXPIRED) {
                    display_show_status("UPDATE CLOSED",
                                        "No firmware received");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_CANCELLED) {
                    display_show_status("UPDATE CLOSED",
                                        "No changes made");
                } else {
                    display_show_status(
                        "UPDATE FAILED",
                        firmware_update_error_detail(
                            firmware_update_status.last_error));
                }
            }
            previous_display_mode = update_display_mode;
            previous_reset_seconds = 0U;
            previous_update_seconds = 0U;
        } else if (display_ready && reset_prompt_active) {
            if (previous_display_mode != APP_DISPLAY_WIFI_RESET_PROMPT ||
                reset_seconds_remaining != previous_reset_seconds) {
                char detail[40];
                snprintf(detail, sizeof(detail), "Keep holding: %us",
                         reset_seconds_remaining);
                display_show_status("RESET WI-FI", detail);
            }
            previous_display_mode = APP_DISPLAY_WIFI_RESET_PROMPT;
            previous_reset_seconds = reset_seconds_remaining;
            previous_update_seconds = 0U;
        } else if (display_ready && manual_sync_ui != MANUAL_SYNC_UI_NONE) {
            if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE) {
                if (previous_display_mode != APP_DISPLAY_MANUAL_SYNC ||
                    render_requested ||
                    network_status.state !=
                        previous_manual_sync_network_state) {
                    display_show_status(
                        "SYNC TIME",
                        manual_sync_detail(network_status.state));
                }
                previous_manual_sync_network_state = network_status.state;
                previous_display_mode = APP_DISPLAY_MANUAL_SYNC;
            } else {
                if (previous_display_mode != APP_DISPLAY_MANUAL_SYNC_RESULT ||
                    render_requested) {
                    if (manual_sync_ui == MANUAL_SYNC_UI_SUCCESS) {
                        display_show_status("TIME SYNCED", "RTC updated");
                    } else if (manual_sync_ui ==
                               MANUAL_SYNC_UI_UNAVAILABLE) {
                        display_show_status(
                            "SYNC UNAVAILABLE",
                            network_status.configured
                                ? "Network service is busy"
                                : "Configure Wi-Fi first");
                    } else {
                        display_show_status(
                            "SYNC FAILED",
                            !rtc_driver_ready
                                ? "RTC is not ready"
                                : esp_err_to_name(manual_sync_error));
                    }
                }
                previous_display_mode = APP_DISPLAY_MANUAL_SYNC_RESULT;
            }
            previous_reset_seconds = 0U;
        } else if (display_ready && app_page_is_system(active_page)) {
            app_display_mode_t system_display_mode = APP_DISPLAY_DEVICE_HEALTH;
            if (active_page == APP_PAGE_NETWORK_TIME) {
                system_display_mode = APP_DISPLAY_NETWORK_TIME;
            } else if (active_page == APP_PAGE_AUDIO) {
                system_display_mode = APP_DISPLAY_AUDIO;
            } else if (active_page == APP_PAGE_WIFI_MAINTENANCE) {
                system_display_mode = APP_DISPLAY_WIFI_MAINTENANCE;
            } else if (active_page == APP_PAGE_ONLINE_UPDATE) {
                system_display_mode = APP_DISPLAY_ONLINE_UPDATE;
            } else if (active_page == APP_PAGE_LOCAL_UPDATE) {
                system_display_mode = APP_DISPLAY_LOCAL_UPDATE;
            }
            const bool system_data_changed =
                (active_page == APP_PAGE_DEVICE_HEALTH &&
                 device_health_data_changed) ||
                (active_page == APP_PAGE_NETWORK_TIME &&
                 network_time_data_changed) ||
                (active_page == APP_PAGE_AUDIO &&
                 audio_data_changed) ||
                (active_page == APP_PAGE_WIFI_MAINTENANCE &&
                 wifi_maintenance_data_changed) ||
                (active_page == APP_PAGE_ONLINE_UPDATE &&
                 online_update_data_changed);
            if (render_requested ||
                previous_display_mode != system_display_mode ||
                system_data_changed) {
                const display_system_status_t system_status = {
                    .firmware_version = app->version,
                    .idf_version = app->idf_ver,
                    .psram_kib = (uint32_t)(psram_bytes / 1024U),
                    .rtc_ready = rtc_driver_ready,
                    .time_valid = dashboard.time_valid,
                    .rtc_backup_state =
                        rtc_backup_monitor_ready
                            ? rtc_backup_status_name(
                                  rtc_backup_monitor_status())
                            : "NOT READY",
                    .year = dashboard.year,
                    .month = dashboard.month,
                    .day = dashboard.day,
                    .hour = dashboard.hour,
                    .minute = dashboard.minute,
                    .sensor_ready = sensor_driver_ready,
                    .environment_valid = dashboard.environment_valid,
                    .temperature_c = dashboard.temperature_c,
                    .humidity_percent = dashboard.humidity_percent,
                    .battery_ready = battery_driver_ready,
                    .battery_valid = dashboard.battery_valid,
                    .battery_voltage_mv = battery_measurement.voltage_mv,
                    .battery_percent = dashboard.battery_percent,
                    .network_ready = network_error == ESP_OK,
                    .network_configured = network_status.configured,
                    .network_state =
                        device_network_state_name(&network_status),
                    .last_sync_valid = last_sync_valid,
                    .last_sync_year = last_sync_time.year,
                    .last_sync_month = last_sync_time.month,
                    .last_sync_day = last_sync_time.day,
                    .last_sync_hour = last_sync_time.hour,
                    .last_sync_minute = last_sync_time.minute,
                };
                if (active_page == APP_PAGE_DEVICE_HEALTH) {
                    display_show_device_health(&system_status);
                } else if (active_page == APP_PAGE_NETWORK_TIME) {
                    display_show_network_time(&system_status);
                } else if (active_page == APP_PAGE_AUDIO) {
                    audio_diagnostics_get_status(&audio_status);
                    const display_audio_status_t display_audio_status = {
                        .initialized = audio_status.initialized,
                        .speaker_ready = audio_status.speaker_ready,
                        .microphones_ready =
                            audio_status.microphones_ready,
                        .test_completed = audio_status.test_completed,
                        .tone_played = audio_status.tone_played,
                        .microphone_capture_completed =
                            audio_status.microphone_capture_completed,
                        .voice_played = audio_status.voice_played,
                        .playback_stopped =
                            audio_status.playback_stopped,
                        .microphone_1_level_percent =
                            audio_status.microphone_1_level_percent,
                        .microphone_2_level_percent =
                            audio_status.microphone_2_level_percent,
                        .playback_microphone =
                            audio_status.playback_microphone,
                        .recording_elapsed_ms =
                            audio_status.recording_elapsed_ms,
                        .recording_duration_ms =
                            audio_status.recording_duration_ms,
                        .playback_elapsed_ms =
                            audio_status.playback_elapsed_ms,
                        .max_recording_ms =
                            AUDIO_DIAGNOSTICS_MAX_RECORDING_MS,
                        .sample_rate_hz =
                            AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ,
                        .bits_per_sample =
                            AUDIO_DIAGNOSTICS_BITS_PER_SAMPLE,
                        .state = display_audio_state(audio_status.state),
                        .result = audio_diagnostics_result_name(
                            audio_status.result),
                    };
                    display_show_audio(&display_audio_status);
                } else if (active_page == APP_PAGE_WIFI_MAINTENANCE) {
                    display_show_wifi_maintenance(&system_status);
                } else if (active_page == APP_PAGE_ONLINE_UPDATE) {
                    const char *detail = NULL;
                    if (online_update_status.state ==
                        ONLINE_UPDATE_STATE_CHECKING) {
                        detail = "Connecting to update service";
                    } else if (online_update_status.state ==
                               ONLINE_UPDATE_STATE_CONNECTING) {
                        detail = "Rechecking release details";
                    } else if (online_update_status.state ==
                               ONLINE_UPDATE_STATE_VERIFYING) {
                        detail = "Checking download and image";
                    } else if (online_update_status.state ==
                               ONLINE_UPDATE_STATE_SUCCESS) {
                        detail = "Restarting automatically";
                    } else if (online_update_status.state ==
                               ONLINE_UPDATE_STATE_FAILED) {
                        detail = !network_status.configured &&
                                         online_update_status.last_error ==
                                             ESP_ERR_INVALID_STATE
                                     ? "CONFIGURE WI-FI"
                                     : online_firmware_update_error_detail(
                                           &online_update_status);
                    }
                    const display_online_update_status_t display_status = {
                        .state = display_online_update_state(
                            online_update_status.state),
                        .current_version =
                            online_update_status.current_version,
                        .latest_version =
                            online_update_status.latest_version[0] != '\0'
                                ? online_update_status.latest_version
                                : NULL,
                        .last_checked =
                            online_update_status.last_checked[0] != '\0'
                                ? online_update_status.last_checked
                                : NULL,
                        .detail = detail,
                        .downloaded_bytes =
                            (uint32_t)online_update_status.downloaded_bytes,
                        .total_bytes =
                            (uint32_t)online_update_status.total_bytes,
                        .progress_percent = online_update_status.percent,
                    };
                    display_show_online_update(&display_status);
                } else {
                    display_show_local_update(&system_status);
                }
            }
            previous_display_mode = system_display_mode;
            previous_reset_seconds = 0U;
            previous_update_seconds = 0U;
        } else if (display_ready &&
                   (periodic_update || render_requested ||
                    previous_display_mode == APP_DISPLAY_DEVICE_HEALTH ||
                    previous_display_mode == APP_DISPLAY_NETWORK_TIME ||
                    previous_display_mode == APP_DISPLAY_AUDIO ||
                    previous_display_mode == APP_DISPLAY_WIFI_MAINTENANCE ||
                    previous_display_mode == APP_DISPLAY_ONLINE_UPDATE ||
                    previous_display_mode == APP_DISPLAY_LOCAL_UPDATE ||
                    previous_display_mode == APP_DISPLAY_MANUAL_SYNC ||
                    previous_display_mode == APP_DISPLAY_MANUAL_SYNC_RESULT ||
                    previous_display_mode == APP_DISPLAY_WIFI_RESET_PROMPT)) {
            const TickType_t provisioning_elapsed = now - provisioning_started;
            const bool show_setup =
                app_network_setup_should_overlay(
                    network_status.state == NETWORK_TIME_STATE_PROVISIONING,
                    network_status.configured,
                    setup_screen_dismissed,
                    (uint32_t)provisioning_elapsed * portTICK_PERIOD_MS);
            app_display_mode_t display_mode = APP_DISPLAY_DASHBOARD;
            if (active_page == APP_PAGE_CALENDAR) {
                display_mode = APP_DISPLAY_CALENDAR;
            }
            if (show_setup) {
                display_mode = APP_DISPLAY_NETWORK_SETUP;
            }

            if (display_mode == APP_DISPLAY_DASHBOARD) {
                display_show_dashboard(&dashboard);
            } else if (display_mode == APP_DISPLAY_CALENDAR) {
                if (display_mode != previous_display_mode ||
                    render_requested || calendar_data_changed) {
                    display_show_calendar(&dashboard);
                }
            } else if (display_mode == previous_display_mode &&
                       !render_requested) {
                /* The setup page is static and needs no one-second redraw. */
            } else if (display_mode == APP_DISPLAY_NETWORK_SETUP) {
                display_show_network_setup(network_status.setup_ssid,
                                           network_status.setup_password,
                                           network_status.setup_url);
            }
            previous_display_mode = display_mode;
            previous_reset_seconds = 0U;
            previous_update_seconds = 0U;
        }

        vTaskDelay(pdMS_TO_TICKS(APP_LOOP_INTERVAL_MS));
    }
}
