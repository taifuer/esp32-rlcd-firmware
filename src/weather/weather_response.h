#pragma once

#include <stddef.h>
#include <stdint.h>

#include "weather_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_CURRENT_RESPONSE_MAX_JSON_BYTES 8192U
#define WEATHER_DAILY_RESPONSE_MAX_JSON_BYTES 24576U
#define WEATHER_GEO_RESPONSE_MAX_JSON_BYTES 16384U
#define WEATHER_GEO_LOCATION_LIMIT 20U
#define WEATHER_LOCATION_NAME_MAX_BYTES 48U
#define WEATHER_LOCATION_NAME_CAPACITY \
    (WEATHER_LOCATION_NAME_MAX_BYTES + 1U)

typedef struct {
    char name[WEATHER_LOCATION_NAME_CAPACITY];
    int32_t latitude_microdegrees;
    int32_t longitude_microdegrees;
} weather_location_t;

typedef enum {
    WEATHER_RESPONSE_RESULT_OK = 0,
    WEATHER_RESPONSE_RESULT_INVALID_ARGUMENT,
    WEATHER_RESPONSE_RESULT_TOO_LARGE,
    WEATHER_RESPONSE_RESULT_INVALID_JSON,
    WEATHER_RESPONSE_RESULT_INVALID_SCHEMA,
    WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE,
    WEATHER_RESPONSE_RESULT_API_ERROR,
    WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND,
} weather_response_result_t;

weather_response_result_t weather_response_parse_qweather_v1_current(
    const char *json,
    size_t json_length,
    weather_current_t *current);

weather_response_result_t weather_response_parse_qweather_v1_daily(
    const char *json,
    size_t json_length,
    weather_daily_t *daily);

/* Parses the QWeather Weather API v1 current and daily responses. The caller
 * must request days=3, localTime=true and lang=zh. Unknown response fields are
 * ignored, but every field represented by weather_snapshot_t is mandatory and
 * type checked. The output is assigned only after both responses succeed. */
weather_response_result_t weather_response_parse_qweather_v1(
    const char *current_json,
    size_t current_json_length,
    const char *daily_json,
    size_t daily_json_length,
    int64_t fetched_at_epoch_seconds,
    weather_snapshot_t *snapshot);

/* Selects a Chinese city from a QWeather GeoAPI v2 city-lookup response.
 * Province and city are required. Matching is suffix-tolerant (for example,
 * "江苏" equals "江苏省") and prefers an exact city result. */
weather_response_result_t weather_response_select_qweather_geo_v2(
    const char *json,
    size_t json_length,
    const char *province,
    const char *city,
    weather_location_t *location);

const char *weather_response_result_name(weather_response_result_t result);

#ifdef __cplusplus
}
#endif
