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
    /* Confirmed enumerating data host; never infer this from battery voltage. */
    bool usb_data_host_connected;
    display_network_state_t network_state;
} display_dashboard_t;

typedef struct {
    bool rtc_ready;
    bool time_valid;
    const char *rtc_backup_state;
    bool sensor_ready;
    bool environment_valid;
    bool environment_stale;
    float temperature_c;
    bool temperature_fahrenheit;
    float humidity_percent;
    bool battery_ready;
    bool battery_valid;
    uint16_t battery_voltage_mv;
    uint8_t battery_percent;
    /* Confirmed enumerating data host; ordinary chargers remain unknown. */
    bool usb_data_host_connected;
    const char *time_sync_state;
    const char *wifi_state;
    bool last_sync_valid;
    uint8_t last_sync_month;
    uint8_t last_sync_day;
    uint8_t last_sync_hour;
    uint8_t last_sync_minute;
} display_system_status_t;

typedef struct {
    bool manual_saving_requested;
    bool automatic_saving_active;
    bool effective_low_power;
    /* Confirmed enumerating data host; ordinary chargers remain unknown. */
    bool usb_data_host_connected;
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
    DISPLAY_VOICE_STATE_READY = 0,
    DISPLAY_VOICE_STATE_WAITING_FOR_RELEASE,
    DISPLAY_VOICE_STATE_PREPARING,
    DISPLAY_VOICE_STATE_LISTENING,
    DISPLAY_VOICE_STATE_RECOGNIZING,
    DISPLAY_VOICE_STATE_SUCCEEDED,
    DISPLAY_VOICE_STATE_NO_VOICE,
    DISPLAY_VOICE_STATE_NOT_UNDERSTOOD,
    DISPLAY_VOICE_STATE_TARGET_UNAVAILABLE,
    DISPLAY_VOICE_STATE_UNAVAILABLE,
    DISPLAY_VOICE_STATE_CANCELLED,
    DISPLAY_VOICE_STATE_FAILED,
} display_voice_state_t;

typedef struct {
    bool engine_available;
    uint32_t elapsed_ms;
    uint32_t max_listening_ms;
    display_voice_state_t state;
    const char *detail;
} display_voice_status_t;

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
void display_show_voice(const display_voice_status_t *status);
void display_show_settings(const display_settings_status_t *status);
void display_show_alarm(const display_alarm_status_t *status);
void display_show_online_update(const display_online_update_status_t *status);

#ifdef __cplusplus
}
#endif
