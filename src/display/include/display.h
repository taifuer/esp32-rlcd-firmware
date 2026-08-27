#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "monochrome_image.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_NETWORK_HIDDEN = 0,
    DISPLAY_NETWORK_CONNECTED,
} display_network_state_t;

typedef enum {
    DISPLAY_ENVIRONMENT_COMFORT_UNKNOWN = 0,
    DISPLAY_ENVIRONMENT_COMFORT_COMFORTABLE,
    DISPLAY_ENVIRONMENT_COMFORT_FAIR,
    DISPLAY_ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT,
} display_environment_comfort_t;

typedef enum {
    DISPLAY_POWER_MODE_AUTO = 0,
    DISPLAY_POWER_MODE_NORMAL,
    DISPLAY_POWER_MODE_SAVING,
} display_power_mode_t;

typedef struct {
    bool time_valid;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool show_seconds;
    bool lunar_valid;
    const char *lunar_text;
    bool environment_valid;
    float temperature_c;
    bool temperature_fahrenheit;
    float humidity_percent;
    display_environment_comfort_t environment_comfort;
    bool battery_valid;
    uint8_t battery_percent;
    display_network_state_t network_state;
} display_dashboard_t;

typedef struct {
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
    bool temperature_fahrenheit;
    float humidity_percent;
    bool battery_ready;
    bool battery_valid;
    uint16_t battery_voltage_mv;
    uint8_t battery_percent;
    bool network_ready;
    bool network_configured;
    const char *network_state;
    bool last_sync_valid;
    uint8_t last_sync_month;
    uint8_t last_sync_day;
    uint8_t last_sync_hour;
    uint8_t last_sync_minute;
} display_system_status_t;

typedef struct {
    display_power_mode_t power_mode;
    bool effective_low_power;
    bool power_apply_pending;
    int16_t utc_offset_minutes;
    bool temperature_fahrenheit;
    uint8_t playback_volume_percent;
    bool alarm_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t alarm_weekdays;
} display_settings_status_t;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    bool snooze_available;
} display_alarm_status_t;

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

typedef enum {
    DISPLAY_ONLINE_UPDATE_STATE_NOT_CHECKED = 0,
    DISPLAY_ONLINE_UPDATE_STATE_CHECKING,
    DISPLAY_ONLINE_UPDATE_STATE_UP_TO_DATE,
    DISPLAY_ONLINE_UPDATE_STATE_UPDATE_AVAILABLE,
    DISPLAY_ONLINE_UPDATE_STATE_CONFIRM_INSTALL,
    DISPLAY_ONLINE_UPDATE_STATE_CONNECTING,
    DISPLAY_ONLINE_UPDATE_STATE_DOWNLOADING,
    DISPLAY_ONLINE_UPDATE_STATE_VERIFYING,
    DISPLAY_ONLINE_UPDATE_STATE_SUCCESS,
    DISPLAY_ONLINE_UPDATE_STATE_FAILED,
} display_online_update_state_t;

typedef struct {
    display_online_update_state_t state;
    bool beta_channel;
    const char *current_version;
    const char *latest_version;
    const char *last_checked;
    const char *detail;
    uint32_t downloaded_bytes;
    uint32_t total_bytes;
    uint8_t progress_percent;
} display_online_update_status_t;

typedef enum {
    DISPLAY_IMAGE_DELETE_DELETING = 0,
    DISPLAY_IMAGE_DELETE_DELETED,
    DISPLAY_IMAGE_DELETE_FAILED,
} display_image_delete_status_t;

esp_err_t display_init(void);
void display_show_status(const char *title, const char *detail);
void display_show_network_setup(const char *ssid, const char *password, const char *url);
void display_show_settings_portal_ready(const char *ssid,
                                        const char *password,
                                        const char *url);
void display_show_dashboard(const display_dashboard_t *dashboard);
void display_show_calendar(const display_dashboard_t *dashboard);
void display_show_monochrome_image(
    const uint8_t bitmap[MONO_IMAGE_BITMAP_BYTES],
    size_t selected_index, size_t image_count);
void display_show_image_delete_confirmation(
    const uint8_t bitmap[MONO_IMAGE_BITMAP_BYTES],
    size_t selected_index, size_t image_count, bool delete_ready);
void display_show_image_delete_status(display_image_delete_status_t status);
void display_show_system_status(const display_system_status_t *status);
void display_show_audio(const display_audio_status_t *status);
void display_show_settings(const display_settings_status_t *status);
void display_show_alarm(const display_alarm_status_t *status);
void display_show_online_update(const display_online_update_status_t *status);

#ifdef __cplusplus
}
#endif
