#include <stdbool.h>

#include "battery.h"
#include "board_i2c.h"
#include "board_pins.h"
#include "chinese_lunar.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_time.h"
#include "pcf85063.h"
#include "shtc3.h"
#include "usb_commands.h"

static const char *TAG = "rlcd_firmware";

typedef enum {
    APP_DISPLAY_NONE = 0,
    APP_DISPLAY_DASHBOARD,
    APP_DISPLAY_NETWORK_SETUP,
    APP_DISPLAY_NETWORK_STARTING,
    APP_DISPLAY_NETWORK_CONNECTING,
    APP_DISPLAY_NETWORK_SYNCHRONIZING,
    APP_DISPLAY_NETWORK_RETRY,
    APP_DISPLAY_NETWORK_ERROR,
} app_display_mode_t;

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
    TickType_t last_wake = xTaskGetTickCount();
    network_time_state_t previous_network_state = NETWORK_TIME_STATE_UNINITIALIZED;
    TickType_t provisioning_started = last_wake;
    app_display_mode_t previous_display_mode = APP_DISPLAY_NONE;
    uint32_t cycle = 0;

    while (true) {
        usb_commands_poll(rtc_driver_ready);

        network_time_datetime_t synchronized_time = {0};
        if (network_time_take_datetime(&synchronized_time)) {
            if (!rtc_driver_ready) {
                ESP_LOGW(TAG, "SNTP succeeded but PCF85063 is unavailable");
            } else {
                const esp_err_t error = write_network_time_to_rtc(&synchronized_time);
                if (error != ESP_OK) {
                    ESP_LOGW(TAG, "could not write SNTP time to RTC: %s",
                             esp_err_to_name(error));
                }
            }
        }

        if (rtc_driver_ready) {
            const esp_err_t error = pcf85063_read(&datetime);
            if (error == ESP_OK) {
                dashboard.time_valid = datetime.clock_integrity;
                dashboard.year = datetime.year;
                dashboard.month = datetime.month;
                dashboard.day = datetime.day;
                dashboard.weekday = datetime.weekday;
                dashboard.hour = datetime.hour;
                dashboard.minute = datetime.minute;
                dashboard.second = datetime.second;
                dashboard.lunar_valid = datetime.clock_integrity &&
                                          chinese_lunar_from_gregorian(datetime.year,
                                                                       datetime.month,
                                                                       datetime.day,
                                                                       &lunar_date) &&
                                          chinese_lunar_format(&lunar_date, lunar_text,
                                                               sizeof(lunar_text));
                dashboard.lunar_text = dashboard.lunar_valid ? lunar_text : NULL;
            } else {
                dashboard.time_valid = false;
                dashboard.lunar_valid = false;
                dashboard.lunar_text = NULL;
                if ((cycle % 10U) == 0U) {
                    ESP_LOGW(TAG, "RTC read failed: %s", esp_err_to_name(error));
                }
            }
        }

        if (sensor_driver_ready && (cycle % 5U) == 0U) {
            const esp_err_t error = shtc3_read(&measurement);
            if (error == ESP_OK) {
                dashboard.environment_valid = true;
                dashboard.temperature_c = measurement.temperature_c;
                dashboard.humidity_percent = measurement.humidity_percent;
                if (dashboard.time_valid) {
                    ESP_LOGI(TAG, "RTC %04u-%02u-%02u %02u:%02u:%02u, temp %.1f C, humidity %.1f %%",
                             datetime.year, datetime.month, datetime.day,
                             datetime.hour, datetime.minute, datetime.second,
                             (double)measurement.temperature_c, (double)measurement.humidity_percent);
                } else {
                    ESP_LOGI(TAG, "RTC time invalid, temp %.1f C, humidity %.1f %%",
                             (double)measurement.temperature_c, (double)measurement.humidity_percent);
                }
            } else {
                dashboard.environment_valid = false;
                ESP_LOGW(TAG, "SHTC3 read failed: %s", esp_err_to_name(error));
            }
        }

        if (battery_driver_ready && (cycle % 30U) == 0U) {
            const esp_err_t error = battery_read(&battery_measurement);
            if (error == ESP_OK) {
                dashboard.battery_valid = true;
                dashboard.battery_percent = battery_measurement.percent;
                if ((cycle % 300U) == 0U) {
                    ESP_LOGI(TAG, "battery %u mV, %u%% (%s)", battery_measurement.voltage_mv,
                             battery_measurement.percent,
                             battery_measurement.calibrated ? "calibrated" : "nominal");
                }
            } else {
                dashboard.battery_valid = false;
                if ((cycle % 300U) == 0U) {
                    ESP_LOGW(TAG, "battery read failed: %s", esp_err_to_name(error));
                }
            }
        }

        network_time_status_t network_status = {0};
        (void)network_time_get_status(&network_status);
        dashboard.network_state = dashboard_network_state(&network_status);
        const TickType_t now = xTaskGetTickCount();
        if (network_status.state != previous_network_state) {
            if (network_status.state == NETWORK_TIME_STATE_PROVISIONING) {
                provisioning_started = now;
            }
            previous_network_state = network_status.state;
        }

        if (display_ready) {
            const TickType_t provisioning_elapsed = now - provisioning_started;
            const bool show_setup =
                network_status.state == NETWORK_TIME_STATE_PROVISIONING &&
                (!dashboard.time_valid ||
                 (provisioning_elapsed >= pdMS_TO_TICKS(3000U) &&
                  provisioning_elapsed < pdMS_TO_TICKS(63000U)));
            app_display_mode_t display_mode = APP_DISPLAY_DASHBOARD;
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
            } else if (display_mode == previous_display_mode) {
                /* Static status pages need no one-second redraw. */
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
        }
        cycle++;
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
