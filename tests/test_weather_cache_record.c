#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "weather_cache_record.h"

static weather_cache_data_t sample(void)
{
    weather_cache_data_t data = {
        .location_fingerprint = 0x12345678U,
        .latitude_ten_thousandths = 312304,
        .longitude_ten_thousandths = 1214737,
        .location_name = "上海市",
        .daily_fetched_at_epoch_seconds = INT64_C(1788336000),
        .snapshot = {
            .fetched_at_epoch_seconds = INT64_C(1788336000),
            .current = {
                .temperature_tenths_celsius = 287,
                .feels_like_tenths_celsius = 301,
                .condition_code = 101U,
                .condition_text = "多云",
            },
            .daily = {
                .day_count = 2U,
                .days = {
                {
                    .date = "2026-09-02",
                    .temperature_high_tenths_celsius = 310,
                    .temperature_low_tenths_celsius = 246,
                    .daytime_condition_code = 101U,
                    .daytime_condition_text = "多云",
                    .precipitation_probability_percent = 20U,
                },
                {
                    .date = "2026-09-03",
                    .temperature_high_tenths_celsius = 302,
                    .temperature_low_tenths_celsius = 238,
                    .daytime_condition_code = 305U,
                    .daytime_condition_text = "小雨",
                    .precipitation_probability_percent = 70U,
                },
                },
            },
        },
    };
    return data;
}

int main(void)
{
    weather_cache_data_t data = sample();
    uint8_t encoded[WEATHER_CACHE_RECORD_ENCODED_SIZE];
    assert(weather_cache_data_is_valid(&data));
    assert(weather_cache_record_encode(9U, &data, encoded,
                                       sizeof(encoded)));

    weather_cache_record_t decoded = {0};
    assert(weather_cache_record_decode(encoded, sizeof(encoded), &decoded));
    assert(decoded.generation == 9U);
    assert(decoded.data.location_fingerprint == data.location_fingerprint);
    assert(decoded.data.latitude_ten_thousandths ==
           data.latitude_ten_thousandths);
    assert(strcmp(decoded.data.location_name, "上海市") == 0);
    assert(strcmp(decoded.data.snapshot.current.condition_text,
                  "多云") == 0);
    assert(decoded.data.snapshot.daily.day_count == 2U);
    assert(strcmp(decoded.data.snapshot.daily.days[1].date,
                  "2026-09-03") == 0);
    assert(decoded.data.snapshot.daily.days[1]
               .precipitation_probability_percent == 70U);

    encoded[40] ^= 1U;
    assert(!weather_cache_record_decode(encoded, sizeof(encoded), &decoded));
    assert(!weather_cache_record_encode(1U, NULL, encoded,
                                        sizeof(encoded)));
    assert(!weather_cache_record_decode(NULL, sizeof(encoded), &decoded));

    weather_cache_record_t a = {.generation = UINT32_MAX};
    weather_cache_record_t b = {.generation = 0U};
    assert(weather_cache_record_select_latest(&a, &b) ==
           WEATHER_CACHE_RECORD_SLOT_B);
    assert(weather_cache_record_select_latest(&a, NULL) ==
           WEATHER_CACHE_RECORD_SLOT_A);
    assert(weather_cache_record_select_latest(NULL, &b) ==
           WEATHER_CACHE_RECORD_SLOT_B);
    assert(weather_cache_record_select_latest(NULL, NULL) ==
           WEATHER_CACHE_RECORD_SLOT_NONE);

    data.snapshot.daily.day_count = 0U;
    assert(!weather_cache_data_is_valid(&data));
    data = sample();
    data.location_name[0] = '\0';
    assert(!weather_cache_data_is_valid(&data));

    puts("weather cache record tests passed");
    return 0;
}
