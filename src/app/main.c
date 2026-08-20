#include <stdbool.h>
#include <stdio.h>

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
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_time.h"
#include "page_state.h"
#include "pcf85063.h"
#include "shtc3.h"
#include "usb_commands.h"

static const char *TAG = "rlcd_firmware";
static const char *const RELEASE_URL =
    "https://github.com/taifuer/esp32-rlcd-firmware/releases/latest";

typedef enum {
    APP_DISPLAY_NONE = 0,
    APP_DISPLAY_DASHBOARD,
    APP_DISPLAY_NETWORK_SETUP,
    APP_DISPLAY_NETWORK_STARTING,
    APP_DISPLAY_NETWORK_CONNECTING,
    APP_DISPLAY_NETWORK_SYNCHRONIZING,
    APP_DISPLAY_NETWORK_RETRY,
    APP_DISPLAY_NETWORK_ERROR,
    APP_DISPLAY_CALENDAR,
    APP_DISPLAY_DEVICE_HEALTH,
    APP_DISPLAY_NETWORK_TIME,
    APP_DISPLAY_WIFI_MAINTENANCE,
    APP_DISPLAY_ABOUT_UPDATE,
    APP_DISPLAY_MANUAL_SYNC,
    APP_DISPLAY_MANUAL_SYNC_RESULT,
    APP_DISPLAY_WIFI_RESET_PROMPT,
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
    case APP_PAGE_WIFI_MAINTENANCE:
        return "Wi-Fi maintenance";
    case APP_PAGE_ABOUT_UPDATE:
        return "about and update";
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
    default:
        return DISPLAY_NETWORK_ERROR;
    }
}

static const char *device_network_state_name(network_time_state_t state)
{
    switch (state) {
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
        return "RETRY WAIT";
    case NETWORK_TIME_STATE_ERROR:
    default:
        return "ERROR";
    }
}

static void configure_key_timing(button_state_t *state, app_page_t page)
{
    const uint32_t action_threshold_ms =
        app_page_key_hold_threshold_ms(page);
    if (app_page_key_hold_action(page) == APP_PAGE_ACTION_RESET_WIFI) {
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

    const esp_err_t i2c_error = board_i2c_init();
    const bool i2c_ready = i2c_error == ESP_OK;
    if (!i2c_ready) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(i2c_error));
    }

    bool rtc_driver_ready = false;
    bool sensor_driver_ready = false;
    uint16_t sensor_id = 0;

    if (i2c_ready) {
        esp_err_t error = board_i2c_probe(PCF85063_I2C_ADDRESS, 100);
        if (error == ESP_OK) {
            error = pcf85063_init(board_i2c_bus());
        }
        rtc_driver_ready = error == ESP_OK;
        if (rtc_driver_ready) {
            ESP_LOGI(TAG, "PCF85063 detected at I2C address 0x%02X", PCF85063_I2C_ADDRESS);
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

    const esp_err_t usb_commands_error = usb_commands_init();
    if (usb_commands_error != ESP_OK) {
        ESP_LOGW(TAG, "USB command console unavailable: %s",
                 esp_err_to_name(usb_commands_error));
    }

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
    network_time_state_t previous_network_state = NETWORK_TIME_STATE_UNINITIALIZED;
    network_time_state_t previous_manual_sync_network_state =
        NETWORK_TIME_STATE_UNINITIALIZED;
    TickType_t provisioning_started = initial_tick;
    app_display_mode_t previous_display_mode = APP_DISPLAY_NONE;
    manual_sync_ui_t manual_sync_ui = MANUAL_SYNC_UI_NONE;
    esp_err_t manual_sync_error = ESP_OK;
    bool first_periodic_update = true;
    uint8_t previous_reset_seconds = 0U;
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

        button_event_t key_event = BUTTON_EVENT_NONE;
        button_event_t boot_event = BUTTON_EVENT_NONE;
        bool key_pressed = false;
        bool boot_pressed = false;
        const app_page_t input_page = app_page_state_current(&page_state);
        if (buttons_ready) {
            configure_key_timing(&key_button_state, input_page);
            key_pressed = board_key_is_pressed();
            boot_pressed = board_boot_is_pressed();
            key_event = button_state_update(
                &key_button_state, key_pressed, button_elapsed_ms);
            boot_event = button_state_update(
                &boot_button_state, boot_pressed, button_elapsed_ms);
        }
        if (key_event == BUTTON_EVENT_SHORT_PRESS) {
            if (manual_sync_ui == MANUAL_SYNC_UI_NONE) {
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
            } else {
                ESP_LOGI(TAG, "KEY hold released without an action");
            }
        } else if (key_event == BUTTON_EVENT_LONG_PRESS) {
            app_page_state_note_activity(&page_state);
            const app_page_action_t key_action =
                app_page_key_hold_action(input_page);
            if (key_action == APP_PAGE_ACTION_SYNC_TIME &&
                manual_sync_ui != MANUAL_SYNC_UI_ACTIVE) {
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
                       manual_sync_ui == MANUAL_SYNC_UI_NONE) {
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
            }
        }

        if (boot_event == BUTTON_EVENT_SHORT_PRESS) {
            if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE) {
                ESP_LOGI(TAG, "BOOT short press ignored while time sync is active");
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
        uint8_t reset_seconds_remaining = 0U;
        if (reset_prompt_active) {
            reset_seconds_remaining = (uint8_t)(
                (APP_PAGE_WIFI_RESET_HOLD_MS - button_hold_ms + 999U) /
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
            !button_interaction_active &&
            app_page_state_tick(&page_state, button_elapsed_ms)) {
            render_requested = true;
            ESP_LOGI(TAG, "page timeout: returning home");
        }

        usb_commands_poll(rtc_driver_ready);

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

        const app_page_t active_page = app_page_state_current(&page_state);
        if (display_ready && reset_prompt_active) {
            if (previous_display_mode != APP_DISPLAY_WIFI_RESET_PROMPT ||
                reset_seconds_remaining != previous_reset_seconds) {
                char detail[40];
                snprintf(detail, sizeof(detail), "Keep holding: %us",
                         reset_seconds_remaining);
                display_show_status("RESET WI-FI", detail);
            }
            previous_display_mode = APP_DISPLAY_WIFI_RESET_PROMPT;
            previous_reset_seconds = reset_seconds_remaining;
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
            } else if (active_page == APP_PAGE_WIFI_MAINTENANCE) {
                system_display_mode = APP_DISPLAY_WIFI_MAINTENANCE;
            } else if (active_page == APP_PAGE_ABOUT_UPDATE) {
                system_display_mode = APP_DISPLAY_ABOUT_UPDATE;
            }
            const bool system_data_changed =
                (active_page == APP_PAGE_DEVICE_HEALTH &&
                 device_health_data_changed) ||
                (active_page == APP_PAGE_NETWORK_TIME &&
                 network_time_data_changed) ||
                (active_page == APP_PAGE_WIFI_MAINTENANCE &&
                 wifi_maintenance_data_changed);
            if (render_requested ||
                previous_display_mode != system_display_mode ||
                system_data_changed) {
                const display_system_status_t system_status = {
                    .firmware_version = app->version,
                    .idf_version = app->idf_ver,
                    .psram_kib = (uint32_t)(psram_bytes / 1024U),
                    .rtc_ready = rtc_driver_ready,
                    .time_valid = dashboard.time_valid,
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
                        device_network_state_name(network_status.state),
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
                } else if (active_page == APP_PAGE_WIFI_MAINTENANCE) {
                    display_show_wifi_maintenance(&system_status);
                } else {
                    display_show_about_update(&system_status, RELEASE_URL);
                }
            }
            previous_display_mode = system_display_mode;
            previous_reset_seconds = 0U;
        } else if (display_ready &&
                   (periodic_update || render_requested ||
                    previous_display_mode == APP_DISPLAY_DEVICE_HEALTH ||
                    previous_display_mode == APP_DISPLAY_NETWORK_TIME ||
                    previous_display_mode == APP_DISPLAY_WIFI_MAINTENANCE ||
                    previous_display_mode == APP_DISPLAY_ABOUT_UPDATE ||
                    previous_display_mode == APP_DISPLAY_MANUAL_SYNC ||
                    previous_display_mode == APP_DISPLAY_MANUAL_SYNC_RESULT ||
                    previous_display_mode == APP_DISPLAY_WIFI_RESET_PROMPT)) {
            const TickType_t provisioning_elapsed = now - provisioning_started;
            const bool show_setup =
                network_status.state == NETWORK_TIME_STATE_PROVISIONING &&
                (!dashboard.time_valid ||
                 (provisioning_elapsed >= pdMS_TO_TICKS(3000U) &&
                  provisioning_elapsed < pdMS_TO_TICKS(63000U)));
            app_display_mode_t display_mode = APP_DISPLAY_DASHBOARD;
            if (active_page == APP_PAGE_CALENDAR) {
                display_mode = APP_DISPLAY_CALENDAR;
            }
            if (show_setup) {
                display_mode = APP_DISPLAY_NETWORK_SETUP;
            } else if (!dashboard.time_valid &&
                       network_status.state == NETWORK_TIME_STATE_STARTING) {
                display_mode = APP_DISPLAY_NETWORK_STARTING;
            } else if (!dashboard.time_valid &&
                       network_status.state == NETWORK_TIME_STATE_CONNECTING) {
                display_mode = APP_DISPLAY_NETWORK_CONNECTING;
            } else if (!dashboard.time_valid &&
                       network_status.state == NETWORK_TIME_STATE_SYNCHRONIZING) {
                display_mode = APP_DISPLAY_NETWORK_SYNCHRONIZING;
            } else if (!dashboard.time_valid &&
                       network_status.state == NETWORK_TIME_STATE_RETRY_WAIT) {
                display_mode = APP_DISPLAY_NETWORK_RETRY;
            } else if (!dashboard.time_valid &&
                       network_status.state == NETWORK_TIME_STATE_ERROR) {
                display_mode = APP_DISPLAY_NETWORK_ERROR;
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
                /* Static network pages need no one-second redraw. */
            } else if (display_mode == APP_DISPLAY_NETWORK_SETUP) {
                display_show_network_setup(network_status.setup_ssid,
                                           network_status.setup_password,
                                           network_status.setup_url);
            } else if (display_mode == APP_DISPLAY_NETWORK_STARTING) {
                display_show_status("WI-FI", "Starting network");
            } else if (display_mode == APP_DISPLAY_NETWORK_CONNECTING) {
                display_show_status("WI-FI", "Connecting...");
            } else if (display_mode == APP_DISPLAY_NETWORK_SYNCHRONIZING) {
                display_show_status("SETTING TIME", "Waiting for NTP");
            } else if (display_mode == APP_DISPLAY_NETWORK_RETRY) {
                display_show_status("WI-FI RETRY", "Check setup details");
            } else if (display_mode == APP_DISPLAY_NETWORK_ERROR) {
                display_show_status("NETWORK ERROR", "Use USB SET_TIME");
            }
            previous_display_mode = display_mode;
            previous_reset_seconds = 0U;
        }

        vTaskDelay(pdMS_TO_TICKS(APP_LOOP_INTERVAL_MS));
    }
}
