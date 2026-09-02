#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_FORECAST_DAY_LIMIT 3U
#define WEATHER_DATE_CAPACITY 11U
#define WEATHER_CONDITION_TEXT_MAX_BYTES 31U
#define WEATHER_CONDITION_TEXT_CAPACITY \
    (WEATHER_CONDITION_TEXT_MAX_BYTES + 1U)

#define WEATHER_FRESH_WINDOW_SECONDS (90 * 60)
#define WEATHER_STALE_WINDOW_SECONDS (6 * 60 * 60)
#define WEATHER_DAILY_FRESH_WINDOW_SECONDS (9 * 60 * 60)
#define WEATHER_DAILY_STALE_WINDOW_SECONDS (36 * 60 * 60)

typedef struct {
    int16_t temperature_tenths_celsius;
    int16_t feels_like_tenths_celsius;
    uint16_t condition_code;
    char condition_text[WEATHER_CONDITION_TEXT_CAPACITY];
} weather_current_t;

typedef struct {
    char date[WEATHER_DATE_CAPACITY];
    int16_t temperature_high_tenths_celsius;
    int16_t temperature_low_tenths_celsius;
    uint16_t daytime_condition_code;
    char daytime_condition_text[WEATHER_CONDITION_TEXT_CAPACITY];
    uint8_t precipitation_probability_percent;
} weather_forecast_day_t;

typedef struct {
    size_t day_count;
    weather_forecast_day_t days[WEATHER_FORECAST_DAY_LIMIT];
} weather_daily_t;

typedef struct {
    /* This timestamp belongs to current conditions. Daily forecast refresh
     * time is owned by the service/cache layer because it uses a different
     * refresh interval. */
    int64_t fetched_at_epoch_seconds;
    weather_current_t current;
    weather_daily_t daily;
} weather_snapshot_t;

typedef enum {
    WEATHER_FRESHNESS_UNKNOWN = 0,
    WEATHER_FRESHNESS_FRESH,
    WEATHER_FRESHNESS_STALE,
    WEATHER_FRESHNESS_EXPIRED,
} weather_freshness_t;

void weather_snapshot_clear(weather_snapshot_t *snapshot);

/* RTC invalidity and clock rollback are reported as UNKNOWN. This prevents a
 * future-dated cached response from being presented as fresh. */
weather_freshness_t weather_freshness_evaluate(
    int64_t fetched_at_epoch_seconds,
    int64_t now_epoch_seconds,
    bool rtc_valid);

weather_freshness_t weather_snapshot_freshness(
    const weather_snapshot_t *snapshot,
    int64_t now_epoch_seconds,
    bool rtc_valid);

weather_freshness_t weather_daily_freshness_evaluate(
    int64_t fetched_at_epoch_seconds,
    int64_t now_epoch_seconds,
    bool rtc_valid);

const char *weather_freshness_name(weather_freshness_t freshness);

#ifdef __cplusplus
}
#endif
