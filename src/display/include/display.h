#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_NETWORK_UNCONFIGURED = 0,
    DISPLAY_NETWORK_CONNECTING,
    DISPLAY_NETWORK_SYNCHRONIZED,
    DISPLAY_NETWORK_ERROR,
} display_network_state_t;

typedef struct {
    bool time_valid;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool lunar_valid;
    const char *lunar_text;
    bool environment_valid;
    float temperature_c;
    float humidity_percent;
    bool battery_valid;
    uint8_t battery_percent;
    display_network_state_t network_state;
} display_dashboard_t;

typedef struct {
    const char *firmware_version;
    const char *idf_version;
    uint32_t psram_kib;
    bool rtc_ready;
    bool time_valid;
    const char *rtc_backup_state;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    bool sensor_ready;
    bool environment_valid;
    float temperature_c;
    float humidity_percent;
    bool battery_ready;
    bool battery_valid;
    uint16_t battery_voltage_mv;
    uint8_t battery_percent;
    bool network_ready;
    bool network_configured;
    const char *network_state;
    bool last_sync_valid;
    uint16_t last_sync_year;
    uint8_t last_sync_month;
    uint8_t last_sync_day;
    uint8_t last_sync_hour;
    uint8_t last_sync_minute;
} display_system_status_t;

typedef enum {
    DISPLAY_AUDIO_STATE_IDLE = 0,
    DISPLAY_AUDIO_STATE_PLAYING_TONE,
    DISPLAY_AUDIO_STATE_PREPARING_RECORDING,
    DISPLAY_AUDIO_STATE_RECORDING,
    DISPLAY_AUDIO_STATE_ANALYZING,
    DISPLAY_AUDIO_STATE_PLAYBACK,
    DISPLAY_AUDIO_STATE_COMPLETED,
    DISPLAY_AUDIO_STATE_CANCELLED,
    DISPLAY_AUDIO_STATE_FAILED,
} display_audio_state_t;

typedef struct {
    bool initialized;
    bool speaker_ready;
    bool microphones_ready;
    bool test_completed;
    bool tone_played;
    bool microphone_capture_completed;
    bool voice_played;
    bool playback_stopped;
    uint8_t microphone_1_level_percent;
    uint8_t microphone_2_level_percent;
    uint8_t playback_microphone;
    uint32_t recording_elapsed_ms;
    uint32_t recording_duration_ms;
    uint32_t playback_elapsed_ms;
    uint32_t max_recording_ms;
    uint32_t sample_rate_hz;
    uint8_t bits_per_sample;
    display_audio_state_t state;
    const char *result;
} display_audio_status_t;

esp_err_t display_init(void);
void display_show_status(const char *title, const char *detail);
void display_show_network_setup(const char *ssid, const char *password, const char *url);
void display_show_firmware_update_ready(const char *ssid, const char *password,
                                        const char *url);
void display_show_dashboard(const display_dashboard_t *dashboard);
void display_show_calendar(const display_dashboard_t *dashboard);
void display_show_device_health(const display_system_status_t *status);
void display_show_network_time(const display_system_status_t *status);
void display_show_audio(const display_audio_status_t *status);
void display_show_wifi_maintenance(const display_system_status_t *status);
void display_show_about_update(const display_system_status_t *status,
                               const char *release_url);

#ifdef __cplusplus
}
#endif
