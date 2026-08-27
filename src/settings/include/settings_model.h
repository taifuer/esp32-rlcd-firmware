#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SETTINGS_SCHEMA_VERSION 6U
#define APP_SETTINGS_DEFAULT_UTC_OFFSET_MINUTES 480
#define APP_SETTINGS_MIN_UTC_OFFSET_MINUTES (-720)
#define APP_SETTINGS_MAX_UTC_OFFSET_MINUTES 840
#define APP_SETTINGS_UTC_OFFSET_STEP_MINUTES 15
#define APP_SETTINGS_DEFAULT_AUDIO_PLAYBACK_VOLUME 68U
#define APP_SETTINGS_POSIX_TZ_CAPACITY 16U
#define APP_SETTINGS_FORM_MAX_LENGTH 256U
#define APP_SETTINGS_DEFAULT_ALARM_HOUR 7U
#define APP_SETTINGS_DEFAULT_ALARM_MINUTE 30U
#define APP_SETTINGS_ALARM_WEEKDAY_SUNDAY (1U << 0U)
#define APP_SETTINGS_ALARM_WEEKDAY_MONDAY (1U << 1U)
#define APP_SETTINGS_ALARM_WEEKDAY_TUESDAY (1U << 2U)
#define APP_SETTINGS_ALARM_WEEKDAY_WEDNESDAY (1U << 3U)
#define APP_SETTINGS_ALARM_WEEKDAY_THURSDAY (1U << 4U)
#define APP_SETTINGS_ALARM_WEEKDAY_FRIDAY (1U << 5U)
#define APP_SETTINGS_ALARM_WEEKDAY_SATURDAY (1U << 6U)
#define APP_SETTINGS_ALARM_WEEKDAYS_MASK 0x3eU
#define APP_SETTINGS_ALARM_WEEKENDS_MASK 0x41U
#define APP_SETTINGS_ALARM_ALL_DAYS_MASK 0x7fU

typedef enum {
    APP_TEMPERATURE_UNIT_CELSIUS = 0,
    APP_TEMPERATURE_UNIT_FAHRENHEIT = 1,
} app_temperature_unit_t;

typedef enum {
    APP_UPDATE_CHANNEL_STABLE = 0,
    APP_UPDATE_CHANNEL_BETA = 1,
} app_update_channel_t;

typedef struct {
    uint16_t schema_version;
    bool manual_saving_requested;
    int16_t utc_offset_minutes;
    app_temperature_unit_t temperature_unit;
    uint8_t audio_playback_volume;
    app_update_channel_t update_channel;
    bool alarm_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t alarm_weekdays;
} app_settings_t;

void app_settings_defaults(app_settings_t *settings);
bool app_settings_validate(const app_settings_t *settings);
/* Decode the persisted power value used by settings schema v1-v5. Schema
 * v1-v4 stored NORMAL=0/SAVING=1. Schema v5 added AUTO=0, SAVING=1 and
 * NORMAL=2. The current model keeps only a manual saving request: the
 * automatic battery policy is always active. */
bool app_manual_saving_from_legacy_power(uint16_t schema_version,
                                         uint8_t value,
                                         bool *manual_requested);
bool app_settings_format_posix_tz(int16_t utc_offset_minutes,
                                  char *buffer, size_t capacity);
/* Apply a complete settings-portal form to a validated base record. Fields
 * not exposed by the portal, including the device-side manual saving
 * request, are preserved from base. */
bool app_settings_parse_form(const char *body, size_t length,
                             const app_settings_t *base,
                             app_settings_t *settings);

#ifdef __cplusplus
}
#endif
