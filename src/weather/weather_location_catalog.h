#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_LOCATION_PROVINCE_COUNT 31U
#define WEATHER_LOCATION_CITY_COUNT 370U

/* IDs are derived from the public six-digit Chinese administrative codes.
 * A directly administered city or county keeps its own code. */
typedef uint32_t weather_province_id_t;
typedef uint32_t weather_city_id_t;

typedef struct {
    weather_province_id_t id;
    const char *name_zh;
} weather_province_t;

typedef struct {
    weather_city_id_t id;
    weather_province_id_t province_id;
    const char *name_zh;
} weather_city_t;

size_t weather_location_province_count(void);
const weather_province_t *weather_location_province_at(size_t index);
const weather_province_t *weather_location_province_by_id(
    weather_province_id_t province_id);

size_t weather_location_city_count(weather_province_id_t province_id);
const weather_city_t *weather_location_city_at(
    weather_province_id_t province_id, size_t index);
const weather_city_t *weather_location_city_by_id(
    weather_city_id_t city_id);

bool weather_location_city_belongs_to(
    weather_province_id_t province_id, weather_city_id_t city_id);
bool weather_location_selection_is_valid(
    weather_province_id_t province_id, weather_city_id_t city_id);

#ifdef __cplusplus
}
#endif
