#include <inttypes.h>
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
#include "pcf85063.h"
#include "rtc_time_sync.h"
#include "shtc3.h"

static const char *TAG = "rlcd_firmware";

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "%s firmware v%s (ESP-IDF %s)", BOARD_NAME, app->version, app->idf_ver);
    ESP_LOGI(TAG, "PSRAM available: %u KiB", (unsigned)(psram_bytes / 1024U));
    ESP_LOGI(TAG, "offline dashboard; only explicit SET_TIME writes RTC; Wi-Fi and NTP are disabled");

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

    const esp_err_t time_sync_error = rtc_time_sync_init();
    if (time_sync_error != ESP_OK) {
        ESP_LOGW(TAG, "USB RTC time command unavailable: %s", esp_err_to_name(time_sync_error));
    }

    const esp_err_t battery_init_error = battery_init();
    const bool battery_driver_ready = battery_init_error == ESP_OK;
    if (battery_driver_ready) {
        ESP_LOGI(TAG, "battery monitor ready on GPIO %d", BOARD_BATTERY_ADC_GPIO);
    } else {
        ESP_LOGW(TAG, "battery monitor unavailable: %s", esp_err_to_name(battery_init_error));
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
    uint32_t cycle = 0;

    while (true) {
        rtc_time_sync_poll(rtc_driver_ready);

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

        if (display_ready) {
            display_show_dashboard(&dashboard);
        }
        cycle++;
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
