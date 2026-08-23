#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SETTINGS_SCHEMA_VERSION 2U
#define APP_SETTINGS_DEFAULT_UTC_OFFSET_MINUTES 480
#define APP_SETTINGS_MIN_UTC_OFFSET_MINUTES (-720)
#define APP_SETTINGS_MAX_UTC_OFFSET_MINUTES 840
#define APP_SETTINGS_UTC_OFFSET_STEP_MINUTES 15
#define APP_SETTINGS_DEFAULT_AUDIO_PLAYBACK_VOLUME 68U
#define APP_SETTINGS_POSIX_TZ_CAPACITY 16U
#define APP_SETTINGS_FORM_MAX_LENGTH 192U

typedef enum {
    APP_POWER_MODE_NORMAL = 0,
    APP_POWER_MODE_SAVING = 1,
} app_power_mode_t;

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
    app_power_mode_t power_mode;
    int16_t utc_offset_minutes;
    app_temperature_unit_t temperature_unit;
    uint8_t audio_playback_volume;
    app_update_channel_t update_channel;
} app_settings_t;

void app_settings_defaults(app_settings_t *settings);
bool app_settings_validate(const app_settings_t *settings);
bool app_settings_format_posix_tz(int16_t utc_offset_minutes,
                                  char *buffer, size_t capacity);
bool app_settings_parse_form(const char *body, size_t length,
                             app_settings_t *settings);

#ifdef __cplusplus
}
#endif
