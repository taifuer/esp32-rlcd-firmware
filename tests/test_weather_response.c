#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "weather_response.h"

static const char VALID_CURRENT[] =
    "{\"metadata\":{\"tag\":\"ignored\"},"
    "\"condition\":{\"text\":\"多云\",\"code\":\"101\",\"future\":true},"
    "\"temperature\":{\"value\":24.26,\"unit\":\"°C\"},"
    "\"feelsLike\":{\"value\":25.84,\"unit\":\"°C\"},"
    "\"humidity\":0.69}";

static const char VALID_DAILY[] =
    "{\"metadata\":{\"tag\":\"ignored\"},\"days\":["
    "{\"forecastStartTime\":\"2026-09-01T00:00+08:00\","
    "\"temperatureMax\":{\"value\":31.04,\"unit\":\"°C\"},"
    "\"temperatureMin\":{\"value\":22.95,\"unit\":\"°C\"},"
    "\"daytime\":{\"condition\":{\"text\":\"晴\",\"code\":\"100\"},"
    "\"precipitation\":{\"probability\":0.104,\"type\":\"rain\"}}},"
    "{\"forecastStartTime\":\"2026-09-02T00:00:00+08:00\","
    "\"temperatureMax\":{\"value\":29,\"unit\":\"°C\"},"
    "\"temperatureMin\":{\"value\":21,\"unit\":\"°C\"},"
    "\"daytime\":{\"condition\":{\"text\":\"中雨\",\"code\":\"306\"},"
    "\"precipitation\":{\"probability\":0.705}}},"
    "{\"forecastStartTime\":\"2026-09-03T00:00:00.250+08:00\","
    "\"temperatureMax\":{\"value\":27.5,\"unit\":\"°C\"},"
    "\"temperatureMin\":{\"value\":-3.26,\"unit\":\"°C\"},"
    "\"daytime\":{\"condition\":{\"text\":\"多云\",\"code\":\"101\"},"
    "\"precipitation\":{\"probability\":0}}}],"
    "\"unknown\":{\"accepted\":true}}";

static const char ONE_DAY_DAILY[] =
    "{\"days\":[{"
    "\"forecastStartTime\":\"2024-02-29T22:00Z\","
    "\"temperatureMax\":{\"value\":10,\"unit\":\"°C\"},"
    "\"temperatureMin\":{\"value\":0,\"unit\":\"°C\"},"
    "\"daytime\":{\"condition\":{\"text\":\"雪\",\"code\":\"499\"},"
    "\"precipitation\":{\"probability\":1}}}]}";

static const char VALID_GEO[] =
    "{\"code\":\"200\",\"location\":["
    "{\"name\":\"朝阳\",\"lat\":\"41.573\",\"lon\":\"120.4500\","
    "\"adm2\":\"朝阳市\",\"adm1\":\"辽宁省\",\"id\":\"101071201\"},"
    "{\"name\":\"朝阳区\",\"lat\":\"39.9219\",\"lon\":\"116.44355\","
    "\"adm2\":\"北京市\",\"adm1\":\"北京市\"},"
    "{\"name\":\"北京\",\"lat\":\"39.90499\",\"lon\":\"116.40529\","
    "\"adm2\":\"北京\",\"adm1\":\"北京市\"}],"
    "\"refer\":{\"sources\":[\"QWeather\"]}}";

static weather_response_result_t parse_snapshot(
    const char *current,
    const char *daily,
    weather_snapshot_t *snapshot)
{
    return weather_response_parse_qweather_v1(
        current, strlen(current), daily, strlen(daily),
        INT64_C(1788256800), snapshot);
}

static void test_valid_qweather_v1_snapshot(void)
{
    weather_snapshot_t snapshot = {0};
    assert(parse_snapshot(VALID_CURRENT, VALID_DAILY, &snapshot) ==
           WEATHER_RESPONSE_RESULT_OK);
    assert(snapshot.fetched_at_epoch_seconds == INT64_C(1788256800));
    assert(snapshot.current.temperature_tenths_celsius == 243);
    assert(snapshot.current.feels_like_tenths_celsius == 258);
    assert(snapshot.current.condition_code == 101U);
    assert(strcmp(snapshot.current.condition_text, "多云") == 0);
    assert(snapshot.daily.day_count == 3U);
    assert(strcmp(snapshot.daily.days[0].date, "2026-09-01") == 0);
    assert(snapshot.daily.days[0].temperature_high_tenths_celsius == 310);
    assert(snapshot.daily.days[0].temperature_low_tenths_celsius == 230);
    assert(snapshot.daily.days[0].precipitation_probability_percent == 10U);
    assert(snapshot.daily.days[1].precipitation_probability_percent == 71U);
    assert(snapshot.daily.days[2].temperature_low_tenths_celsius == -33);
    assert(snapshot.daily.days[2].daytime_condition_code == 101U);

    assert(parse_snapshot(VALID_CURRENT, ONE_DAY_DAILY, &snapshot) ==
           WEATHER_RESPONSE_RESULT_OK);
    assert(snapshot.daily.day_count == 1U);
    assert(strcmp(snapshot.daily.days[0].date, "2024-02-29") == 0);
}

static void test_independent_parsers_are_atomic(void)
{
    weather_current_t current;
    weather_current_t current_before;
    weather_daily_t daily;
    weather_daily_t daily_before;

    memset(&current, 0xa5, sizeof(current));
    current_before = current;
    assert(weather_response_parse_qweather_v1_current(
               "{}", 2U, &current) ==
           WEATHER_RESPONSE_RESULT_INVALID_SCHEMA);
    assert(memcmp(&current, &current_before, sizeof(current)) == 0);
    assert(weather_response_parse_qweather_v1_current(
               VALID_CURRENT, strlen(VALID_CURRENT), &current) ==
           WEATHER_RESPONSE_RESULT_OK);
    assert(current.condition_code == 101U);

    memset(&daily, 0xa5, sizeof(daily));
    daily_before = daily;
    assert(weather_response_parse_qweather_v1_daily(
               "{\"days\":[]}", strlen("{\"days\":[]}"), &daily) ==
           WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE);
    assert(memcmp(&daily, &daily_before, sizeof(daily)) == 0);
    assert(weather_response_parse_qweather_v1_daily(
               VALID_DAILY, strlen(VALID_DAILY), &daily) ==
           WEATHER_RESPONSE_RESULT_OK);
    assert(daily.day_count == 3U);
}

static void test_rejects_invalid_current_response(void)
{
    static const char *const INVALID[] = {
        "{}",
        "[]",
        "{\"condition\":{\"text\":\"晴\",\"code\":\"99\"},"
            "\"temperature\":{\"value\":20,\"unit\":\"°C\"},"
            "\"feelsLike\":{\"value\":20,\"unit\":\"°C\"}}",
        "{\"condition\":{\"text\":\"晴\",\"code\":\"100\"},"
            "\"temperature\":{\"value\":\"20\",\"unit\":\"°C\"},"
            "\"feelsLike\":{\"value\":20,\"unit\":\"°C\"}}",
        "{\"condition\":{\"text\":\"晴\",\"code\":\"100\"},"
            "\"temperature\":{\"value\":81,\"unit\":\"°C\"},"
            "\"feelsLike\":{\"value\":20,\"unit\":\"°C\"}}",
        "{\"condition\":{\"text\":\"晴\",\"code\":\"100\"},"
            "\"temperature\":{\"value\":20,\"unit\":\"°F\"},"
            "\"feelsLike\":{\"value\":20,\"unit\":\"°C\"}}",
        "{\"condition\":{\"text\":\"晴晴晴晴晴晴晴晴晴晴晴\","
            "\"code\":\"100\"},"
            "\"temperature\":{\"value\":20,\"unit\":\"°C\"},"
            "\"feelsLike\":{\"value\":20,\"unit\":\"°C\"}}",
        "{\"condition\":{\"text\":\"晴\",\"code\":\"100\","
            "\"code\":\"101\"},"
            "\"temperature\":{\"value\":20,\"unit\":\"°C\"},"
            "\"feelsLike\":{\"value\":20,\"unit\":\"°C\"}}",
    };
    weather_snapshot_t snapshot = {0};

    for (size_t index = 0U; index < sizeof(INVALID) / sizeof(INVALID[0]);
         ++index) {
        assert(parse_snapshot(INVALID[index], ONE_DAY_DAILY, &snapshot) !=
               WEATHER_RESPONSE_RESULT_OK);
    }
    const char trailing[] = "{} trailing";
    assert(weather_response_parse_qweather_v1(
               trailing, sizeof(trailing) - 1U,
               ONE_DAY_DAILY, strlen(ONE_DAY_DAILY),
               INT64_C(1788256800), &snapshot) ==
           WEATHER_RESPONSE_RESULT_INVALID_JSON);
}

static void test_rejects_invalid_daily_response_atomically(void)
{
    static const char *const INVALID[] = {
        "{}",
        "{\"days\":[]}",
        "{\"days\":[1,2,3,4]}",
        "{\"days\":[{\"forecastStartTime\":\"2026-02-29T00:00:00Z\","
            "\"temperatureMax\":{\"value\":10,\"unit\":\"°C\"},"
            "\"temperatureMin\":{\"value\":0,\"unit\":\"°C\"},"
            "\"daytime\":{\"condition\":{\"text\":\"晴\",\"code\":\"100\"},"
            "\"precipitation\":{\"probability\":0}}}]}",
        "{\"days\":[{\"forecastStartTime\":\"2026-09-01T00:00:00Z\","
            "\"temperatureMax\":{\"value\":10,\"unit\":\"°C\"},"
            "\"temperatureMin\":{\"value\":20,\"unit\":\"°C\"},"
            "\"daytime\":{\"condition\":{\"text\":\"晴\",\"code\":\"100\"},"
            "\"precipitation\":{\"probability\":0}}}]}",
        "{\"days\":[{\"forecastStartTime\":\"2026-09-01T00:00:00Z\","
            "\"temperatureMax\":{\"value\":20,\"unit\":\"°C\"},"
            "\"temperatureMin\":{\"value\":10,\"unit\":\"°C\"},"
            "\"daytime\":{\"condition\":{\"text\":\"晴\",\"code\":\"100\"},"
            "\"precipitation\":{\"probability\":1.01}}}]}",
    };
    weather_snapshot_t snapshot;
    weather_snapshot_t before;

    memset(&snapshot, 0xa5, sizeof(snapshot));
    before = snapshot;
    for (size_t index = 0U; index < sizeof(INVALID) / sizeof(INVALID[0]);
         ++index) {
        assert(parse_snapshot(VALID_CURRENT, INVALID[index], &snapshot) !=
               WEATHER_RESPONSE_RESULT_OK);
        assert(memcmp(&snapshot, &before, sizeof(snapshot)) == 0);
    }
}

static void test_argument_and_size_limits(void)
{
    weather_snapshot_t snapshot = {0};
    char oversized[WEATHER_CURRENT_RESPONSE_MAX_JSON_BYTES + 1U];
    memset(oversized, 'x', sizeof(oversized));

    assert(weather_response_parse_qweather_v1(
               NULL, 0U, VALID_DAILY, strlen(VALID_DAILY),
               INT64_C(1788256800), &snapshot) ==
           WEATHER_RESPONSE_RESULT_INVALID_ARGUMENT);
    assert(weather_response_parse_qweather_v1(
               VALID_CURRENT, strlen(VALID_CURRENT),
               VALID_DAILY, strlen(VALID_DAILY), 0, &snapshot) ==
           WEATHER_RESPONSE_RESULT_INVALID_ARGUMENT);
    assert(weather_response_parse_qweather_v1(
               oversized, sizeof(oversized),
               VALID_DAILY, strlen(VALID_DAILY),
               INT64_C(1788256800), &snapshot) ==
           WEATHER_RESPONSE_RESULT_TOO_LARGE);
}

static void test_geo_selects_province_and_city(void)
{
    weather_location_t location = {0};

    assert(weather_response_select_qweather_geo_v2(
               VALID_GEO, strlen(VALID_GEO), "辽宁", "朝阳市",
               &location) == WEATHER_RESPONSE_RESULT_OK);
    assert(strcmp(location.name, "朝阳") == 0);
    assert(location.latitude_microdegrees == INT32_C(41573000));
    assert(location.longitude_microdegrees == INT32_C(120450000));

    assert(weather_response_select_qweather_geo_v2(
               VALID_GEO, strlen(VALID_GEO), "北京市", "北京",
               &location) == WEATHER_RESPONSE_RESULT_OK);
    assert(strcmp(location.name, "北京") == 0);
    assert(location.latitude_microdegrees == INT32_C(39904990));
    assert(location.longitude_microdegrees == INT32_C(116405290));

    assert(weather_response_select_qweather_geo_v2(
               VALID_GEO, strlen(VALID_GEO), "北京", "北京市",
               &location) == WEATHER_RESPONSE_RESULT_OK);
    assert(strcmp(location.name, "北京") == 0);
}

static void test_geo_rejects_errors_and_keeps_output(void)
{
    weather_location_t location;
    weather_location_t before;
    memset(&location, 0xa5, sizeof(location));
    before = location;

    const char *invalid[] = {
        "{\"code\":200,\"location\":[]}",
        "{\"code\":\"200\",\"location\":[]}",
        "{\"code\":\"200\",\"location\":[{\"name\":\"北京\","
            "\"lat\":\"91\",\"lon\":\"116.4\",\"adm2\":\"北京\","
            "\"adm1\":\"北京\"}]}",
    };
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        assert(weather_response_select_qweather_geo_v2(
                   invalid[index], strlen(invalid[index]),
                   "北京", "北京", &location) !=
               WEATHER_RESPONSE_RESULT_OK);
        assert(memcmp(&location, &before, sizeof(location)) == 0);
    }
    const char not_found[] = "{\"code\":\"404\"}";
    assert(weather_response_select_qweather_geo_v2(
               not_found, strlen(not_found), "北京", "北京",
               &location) == WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND);
    const char api_error[] = "{\"code\":\"500\"}";
    assert(weather_response_select_qweather_geo_v2(
               api_error, strlen(api_error), "北京", "北京",
               &location) == WEATHER_RESPONSE_RESULT_API_ERROR);
    const char empty[] = "{\"code\":\"200\",\"location\":[]}";
    assert(weather_response_select_qweather_geo_v2(
               empty, strlen(empty), "北京", "北京",
               &location) == WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND);
    const char malformed_then_valid[] =
        "{\"code\":\"200\",\"location\":[{},"
        "{\"name\":\"北京\",\"lat\":\"39.9042\","
        "\"lon\":\"116.4074\",\"adm2\":\"北京\","
        "\"adm1\":\"北京\"}]}";
    assert(weather_response_select_qweather_geo_v2(
               malformed_then_valid, strlen(malformed_then_valid),
               "北京", "北京", &location) ==
           WEATHER_RESPONSE_RESULT_OK);
    assert(weather_response_select_qweather_geo_v2(
               VALID_GEO, strlen(VALID_GEO), "河北", "朝阳",
               &location) == WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND);
}

static void test_result_names(void)
{
    assert(strcmp(weather_response_result_name(WEATHER_RESPONSE_RESULT_OK),
                  "ok") == 0);
    assert(strcmp(weather_response_result_name(
                      WEATHER_RESPONSE_RESULT_API_ERROR),
                  "api_error") == 0);
    assert(strcmp(weather_response_result_name(
                      WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND),
                  "location_not_found") == 0);
    assert(strcmp(weather_response_result_name(
                      (weather_response_result_t)99),
                  "invalid") == 0);
}

int main(void)
{
    test_valid_qweather_v1_snapshot();
    test_independent_parsers_are_atomic();
    test_rejects_invalid_current_response();
    test_rejects_invalid_daily_response_atomically();
    test_argument_and_size_limits();
    test_geo_selects_province_and_city();
    test_geo_rejects_errors_and_keeps_output();
    test_result_names();
    puts("weather response tests passed");
    return 0;
}
