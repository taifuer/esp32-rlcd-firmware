#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_REQUEST_URL_CAPACITY 640U

uint32_t weather_location_fingerprint(uint32_t province_id,
                                      uint32_t city_id);

bool weather_request_build_geo_url(const char *api_host,
                                   const char *location,
                                   const char *administrative_area,
                                   char *url, size_t capacity);
bool weather_request_build_current_url(
    const char *api_host, int32_t latitude_ten_thousandths,
    int32_t longitude_ten_thousandths, char *url, size_t capacity);
bool weather_request_build_daily_url(
    const char *api_host, int32_t latitude_ten_thousandths,
    int32_t longitude_ten_thousandths, char *url, size_t capacity);

#ifdef __cplusplus
}
#endif
