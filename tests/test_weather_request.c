#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "weather_request.h"

int main(void)
{
    char url[WEATHER_REQUEST_URL_CAPACITY];
    assert(weather_request_build_geo_url(
        "abc123.qweatherapi.com", "浦东新区", "上海市", url,
        sizeof(url)));
    assert(strcmp(
               url,
               "https://abc123.qweatherapi.com/geo/v2/city/lookup?"
               "location=%E6%B5%A6%E4%B8%9C%E6%96%B0%E5%8C%BA&"
               "adm=%E4%B8%8A%E6%B5%B7%E5%B8%82&range=cn&number=10&lang=zh") ==
           0);
    assert(weather_request_build_current_url(
        "abc123.qweatherapi.com", 312304, 1214737, url, sizeof(url)));
    assert(strcmp(
               url,
               "https://abc123.qweatherapi.com/weather/v1/current/"
               "31.23/121.47?localTime=true&lang=zh") == 0);
    assert(weather_request_build_daily_url(
        "abc123.qweatherapi.com", -338688, 1512093, url, sizeof(url)));
    assert(strstr(url, "/daily/-33.87/151.21?") != NULL);
    assert(strstr(url, "days=3&localTime=true&lang=zh") != NULL);

    char short_url[16] = "stale";
    assert(!weather_request_build_geo_url(
        "abc123.qweatherapi.com", "北京市", "北京市", short_url,
        sizeof(short_url)));
    assert(short_url[0] == '\0');
    assert(!weather_request_build_current_url(
        "abc123.qweatherapi.com", 900001, 0, url, sizeof(url)));
    assert(!weather_request_build_daily_url(NULL, 0, 0, url,
                                            sizeof(url)));

    assert(weather_location_fingerprint(310000U, 310000U) ==
           weather_location_fingerprint(310000U, 310000U));
    assert(weather_location_fingerprint(310000U, 310000U) !=
           weather_location_fingerprint(310000U, 310100U));
    assert(weather_location_fingerprint(310000U, 310000U) != 0U);

    puts("weather request tests passed");
    return 0;
}
