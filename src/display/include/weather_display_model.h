#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_WEATHER_FRESHNESS_UNKNOWN = 0,
    DISPLAY_WEATHER_FRESHNESS_FRESH,
    DISPLAY_WEATHER_FRESHNESS_STALE,
    DISPLAY_WEATHER_FRESHNESS_EXPIRED,
} display_weather_freshness_t;

typedef enum {
    DISPLAY_WEATHER_ICON_UNKNOWN = 0,
    DISPLAY_WEATHER_ICON_CLEAR,
    DISPLAY_WEATHER_ICON_CLOUDY,
    DISPLAY_WEATHER_ICON_WIND,
    DISPLAY_WEATHER_ICON_RAIN,
    DISPLAY_WEATHER_ICON_THUNDER,
    DISPLAY_WEATHER_ICON_SNOW,
    DISPLAY_WEATHER_ICON_FOG,
} display_weather_icon_t;

display_weather_icon_t display_weather_icon_from_qweather_code(
    uint16_t condition_code);

bool display_weather_format_temperature(
    char *buffer, size_t capacity,
    int16_t tenths_celsius, bool fahrenheit,
    bool include_unit);

bool display_weather_format_source(
    char *buffer, size_t capacity,
    display_weather_freshness_t freshness,
    bool update_time_valid,
    uint8_t update_month, uint8_t update_day,
    uint8_t update_hour, uint8_t update_minute);

bool display_weather_format_current_date(
    char *buffer, size_t capacity, bool current_date_valid,
    uint16_t current_year, uint8_t current_month, uint8_t current_day);

bool display_weather_format_day_label(
    char *buffer, size_t capacity, const char *forecast_date,
    bool current_date_valid, uint16_t current_year,
    uint8_t current_month, uint8_t current_day);

#ifdef __cplusplus
}
#endif
