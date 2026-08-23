#include "usb_commands.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio_diagnostics.h"
#include "app_settings.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_time.h"
#include "pcf85063.h"

#define RTC_COMMAND_MAX_LENGTH 63U
#define RTC_COMMAND_PREFIX "SET_TIME "
#define RTC_COMMAND_PREFIX_LENGTH 9U
#define RTC_COMMAND_DATETIME_LENGTH 19U

static const char *TAG = "usb_commands";
static char s_line[RTC_COMMAND_MAX_LENGTH + 1U];
static size_t s_line_length;
static bool s_line_overflow;
static bool s_ready;

static bool parse_digits(const char *text, size_t count, uint16_t *value)
{
    uint16_t parsed = 0;
    for (size_t index = 0; index < count; index++) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        parsed = (uint16_t)(parsed * 10U + (uint16_t)(text[index] - '0'));
    }
    *value = parsed;
    return true;
}

static bool parse_set_time(const char *line, pcf85063_datetime_t *datetime)
{
    if (strlen(line) != RTC_COMMAND_PREFIX_LENGTH + RTC_COMMAND_DATETIME_LENGTH ||
        memcmp(line, RTC_COMMAND_PREFIX, RTC_COMMAND_PREFIX_LENGTH) != 0) {
        return false;
    }

    const char *text = line + RTC_COMMAND_PREFIX_LENGTH;
    if (text[4] != '-' || text[7] != '-' || text[10] != ' ' ||
        text[13] != ':' || text[16] != ':') {
        return false;
    }

    uint16_t year = 0;
    uint16_t month = 0;
    uint16_t day = 0;
    uint16_t hour = 0;
    uint16_t minute = 0;
    uint16_t second = 0;
    if (!parse_digits(&text[0], 4, &year) || !parse_digits(&text[5], 2, &month) ||
        !parse_digits(&text[8], 2, &day) || !parse_digits(&text[11], 2, &hour) ||
        !parse_digits(&text[14], 2, &minute) || !parse_digits(&text[17], 2, &second)) {
        return false;
    }

    pcf85063_datetime_t value = {
        .year = year,
        .month = (uint8_t)month,
        .day = (uint8_t)day,
        .hour = (uint8_t)hour,
        .minute = (uint8_t)minute,
        .second = (uint8_t)second,
        .clock_integrity = true,
    };
    if (pcf85063_calculate_weekday(value.year, value.month, value.day, &value.weekday) != ESP_OK ||
        !pcf85063_datetime_is_valid(&value)) {
        return false;
    }

    *datetime = value;
    return true;
}

static void log_datetime(const char *prefix, const pcf85063_datetime_t *datetime)
{
    ESP_LOGI(TAG, "%s %04u-%02u-%02u %02u:%02u:%02u weekday=%u",
             prefix, datetime->year, datetime->month, datetime->day,
             datetime->hour, datetime->minute, datetime->second, datetime->weekday);
}

static void process_line(const char *line, bool rtc_available)
{
    if (strcmp(line, "HELP") == 0) {
        ESP_LOGI(TAG, "Commands: SET_TIME YYYY-MM-DD HH:MM:SS | GET_TIME | GET_NETWORK | GET_AUDIO | GET_SETTINGS | RESET_WIFI | HELP");
        return;
    }

    if (strcmp(line, "GET_SETTINGS") == 0) {
        app_settings_t settings = {0};
        const esp_err_t error = app_settings_get(&settings);
        if (error == ESP_OK) {
            ESP_LOGI(TAG,
                     "SETTINGS power=%s utc_offset_minutes=%d unit=%s playback_volume=%u updates=%s",
                     settings.power_mode == APP_POWER_MODE_SAVING
                         ? "saving"
                         : "normal",
                     settings.utc_offset_minutes,
                     settings.temperature_unit ==
                             APP_TEMPERATURE_UNIT_FAHRENHEIT
                         ? "F"
                         : "C",
                     settings.audio_playback_volume,
                     settings.update_channel == APP_UPDATE_CHANNEL_BETA
                         ? "beta"
                         : "stable");
        } else {
            ESP_LOGW(TAG, "SETTINGS_ERROR %s", esp_err_to_name(error));
        }
        return;
    }

    if (strcmp(line, "GET_AUDIO") == 0) {
        audio_diagnostics_status_t status = {0};
        audio_diagnostics_get_status(&status);
        ESP_LOGI(TAG,
                 "AUDIO state=%s initialized=%s speaker=%s microphones=%s tested=%s tone=%s mic1=%u%% mic2=%u%% duration_ms=%u loopback=%s source=MIC%u result=%s last_error=%s",
                 audio_session_state_name(status.state),
                 status.initialized ? "yes" : "no",
                 status.speaker_ready ? "ready" : "not_ready",
                 status.microphones_ready ? "ready" : "not_ready",
                 status.test_completed ? "yes" : "no",
                 status.tone_played ? "played" : "not_played",
                 status.microphone_1_level_percent,
                 status.microphone_2_level_percent,
                 (unsigned)status.recording_duration_ms,
                 status.voice_played
                     ? "played"
                     : (status.playback_stopped ? "stopped" : "not_played"),
                 status.playback_microphone,
                 audio_diagnostics_result_name(status.result),
                 esp_err_to_name(status.last_error));
        return;
    }

    if (strcmp(line, "GET_NETWORK") == 0) {
        network_time_status_t status = {0};
        const esp_err_t error = network_time_get_status(&status);
        if (error == ESP_OK) {
            ESP_LOGI(TAG,
                     "NETWORK state=%s configured=%s automatic=%s failure=%s last_error=%s",
                     network_time_state_name(status.state),
                     status.configured ? "yes" : "no",
                     status.automatic_sync_enabled ? "yes" : "no",
                     network_time_failure_name(status.last_failure),
                     esp_err_to_name(status.last_error));
        } else {
            ESP_LOGW(TAG, "NETWORK_ERROR %s", esp_err_to_name(error));
        }
        return;
    }

    if (strcmp(line, "RESET_WIFI") == 0) {
        const esp_err_t error = network_time_clear_credentials();
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "WIFI_RESET_ERROR %s", esp_err_to_name(error));
            return;
        }
        ESP_LOGI(TAG, "WIFI_RESET_OK restarting into setup mode");
        vTaskDelay(pdMS_TO_TICKS(150));
        esp_restart();
        return;
    }

    const bool is_get_time = strcmp(line, "GET_TIME") == 0;
    const bool is_set_time = strncmp(line, RTC_COMMAND_PREFIX,
                                     RTC_COMMAND_PREFIX_LENGTH) == 0;
    if (!is_get_time && !is_set_time) {
        ESP_LOGW(TAG, "COMMAND_ERROR unknown command; use HELP");
        return;
    }

    if (!rtc_available) {
        ESP_LOGW(TAG, "RTC_SET_ERROR PCF85063 is not available");
        return;
    }

    if (is_get_time) {
        pcf85063_datetime_t current = {0};
        const esp_err_t error = pcf85063_read(&current);
        if (error == ESP_OK) {
            log_datetime(current.clock_integrity ? "RTC_TIME" : "RTC_TIME_INVALID", &current);
        } else {
            ESP_LOGW(TAG, "RTC_READ_ERROR %s", esp_err_to_name(error));
        }
        return;
    }

    pcf85063_datetime_t requested = {0};
    if (!parse_set_time(line, &requested)) {
        ESP_LOGW(TAG, "RTC_SET_ERROR expected: SET_TIME YYYY-MM-DD HH:MM:SS");
        return;
    }

    esp_err_t error = pcf85063_write(&requested);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "RTC_SET_ERROR write failed: %s", esp_err_to_name(error));
        return;
    }

    pcf85063_datetime_t verified = {0};
    error = pcf85063_read(&verified);
    if (error != ESP_OK || !verified.clock_integrity) {
        ESP_LOGW(TAG, "RTC_SET_ERROR verification failed: %s",
                 error == ESP_OK ? "clock integrity flag is set" : esp_err_to_name(error));
        return;
    }
    log_datetime("RTC_SET_OK", &verified);
}

static void consume_byte(uint8_t byte, bool rtc_available)
{
    if (byte == '\r') {
        return;
    }
    if (byte == '\b' || byte == 0x7fU) {
        if (s_line_length > 0U) {
            s_line_length--;
        }
        return;
    }
    if (byte != '\n') {
        if (byte >= 0x20U && byte <= 0x7eU) {
            if (s_line_length < RTC_COMMAND_MAX_LENGTH) {
                s_line[s_line_length++] = (char)byte;
            } else {
                s_line_overflow = true;
            }
        }
        return;
    }

    if (s_line_overflow) {
        ESP_LOGW(TAG, "COMMAND_ERROR command is too long");
    } else if (s_line_length > 0U) {
        s_line[s_line_length] = '\0';
        process_line(s_line, rtc_available);
    }
    s_line_length = 0U;
    s_line_overflow = false;
}

esp_err_t usb_commands_init(void)
{
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        const esp_err_t error = usb_serial_jtag_driver_install(&config);
        if (error != ESP_OK) {
            return error;
        }
    }

    usb_serial_jtag_vfs_use_driver();
    s_ready = true;
    ESP_LOGI(TAG, "USB command console ready; use HELP to list commands");
    return ESP_OK;
}

void usb_commands_poll(bool rtc_available)
{
    if (!s_ready) {
        return;
    }

    uint8_t input[32];
    int received = 0;
    do {
        received = usb_serial_jtag_read_bytes(input, sizeof(input), 0);
        for (int index = 0; index < received; index++) {
            consume_byte(input[index], rtc_available);
        }
    } while (received > 0);
}
