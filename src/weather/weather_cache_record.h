#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_CACHE_LOCATION_NAME_MAX_BYTES 48U
#define WEATHER_CACHE_LOCATION_NAME_CAPACITY \
    (WEATHER_CACHE_LOCATION_NAME_MAX_BYTES + 1U)
#define WEATHER_CACHE_RECORD_ENCODED_SIZE 282U

typedef struct {
    uint32_t location_fingerprint;
    int32_t latitude_ten_thousandths;
    int32_t longitude_ten_thousandths;
    char location_name[WEATHER_CACHE_LOCATION_NAME_CAPACITY];
    int64_t daily_fetched_at_epoch_seconds;
    weather_snapshot_t snapshot;
} weather_cache_data_t;

typedef enum {
    WEATHER_CACHE_RECORD_SLOT_NONE = 0,
    WEATHER_CACHE_RECORD_SLOT_A,
    WEATHER_CACHE_RECORD_SLOT_B,
} weather_cache_record_slot_t;

typedef struct {
    uint32_t generation;
    weather_cache_data_t data;
} weather_cache_record_t;

bool weather_cache_data_is_valid(const weather_cache_data_t *data);
bool weather_cache_record_encode(uint32_t generation,
                                 const weather_cache_data_t *data,
                                 uint8_t *encoded, size_t encoded_size);
bool weather_cache_record_decode(const uint8_t *encoded,
                                 size_t encoded_size,
                                 weather_cache_record_t *record);
weather_cache_record_slot_t weather_cache_record_select_latest(
    const weather_cache_record_t *slot_a,
    const weather_cache_record_t *slot_b);

#ifdef __cplusplus
}
#endif
