#include "weather_client.h"

#include <stdlib.h>
#include <string.h>

#include "weather_http_json.h"
#include "weather_location_catalog.h"
#include "weather_request.h"
#include "weather_response.h"

static esp_err_t response_error(weather_response_result_t result)
{
    if (result == WEATHER_RESPONSE_RESULT_OK) {
        return ESP_OK;
    }
    if (result == WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (result == WEATHER_RESPONSE_RESULT_TOO_LARGE) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static int32_t microdegrees_to_ten_thousandths(int32_t value)
{
    return value >= 0 ? (value + 50) / 100 : (value - 50) / 100;
}

esp_err_t weather_client_resolve_location(
    const weather_config_t *config, weather_client_location_t *location)
{
    if (config == NULL || location == NULL ||
        !weather_config_is_configured(config) ||
        !weather_location_selection_is_valid(config->province_id,
                                             config->city_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    const weather_province_t *province =
        weather_location_province_by_id(config->province_id);
    const weather_city_t *city =
        weather_location_city_by_id(config->city_id);
    if (province == NULL || city == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[WEATHER_REQUEST_URL_CAPACITY];
    if (!weather_request_build_geo_url(config->api_host, city->name_zh,
                                       province->name_zh, url,
                                       sizeof(url))) {
        return ESP_ERR_INVALID_ARG;
    }

    char *json = NULL;
    size_t json_length = 0U;
    esp_err_t error = weather_http_get_json(
        url, config->api_key, WEATHER_GEO_RESPONSE_MAX_JSON_BYTES,
        &json, &json_length);
    if (error != ESP_OK) {
        return error;
    }
    weather_location_t resolved = {0};
    const weather_response_result_t parsed =
        weather_response_select_qweather_geo_v2(
            json, json_length, province->name_zh, city->name_zh,
            &resolved);
    memset(json, 0, json_length);
    free(json);
    error = response_error(parsed);
    if (error != ESP_OK) {
        return error;
    }

    weather_client_location_t candidate = {
        .latitude_ten_thousandths =
            microdegrees_to_ten_thousandths(
                resolved.latitude_microdegrees),
        .longitude_ten_thousandths =
            microdegrees_to_ten_thousandths(
                resolved.longitude_microdegrees),
    };
    memcpy(candidate.name, resolved.name, sizeof(candidate.name));
    *location = candidate;
    return ESP_OK;
}

esp_err_t weather_client_fetch_current(
    const weather_config_t *config,
    const weather_client_location_t *location,
    weather_current_t *current)
{
    if (config == NULL || location == NULL || current == NULL ||
        !weather_config_is_configured(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[WEATHER_REQUEST_URL_CAPACITY];
    if (!weather_request_build_current_url(
            config->api_host, location->latitude_ten_thousandths,
            location->longitude_ten_thousandths, url, sizeof(url))) {
        return ESP_ERR_INVALID_ARG;
    }
    char *json = NULL;
    size_t length = 0U;
    esp_err_t error = weather_http_get_json(
        url, config->api_key, WEATHER_CURRENT_RESPONSE_MAX_JSON_BYTES,
        &json, &length);
    if (error == ESP_OK) {
        error = response_error(
            weather_response_parse_qweather_v1_current(
                json, length, current));
    }
    if (json != NULL) {
        memset(json, 0, length);
        free(json);
    }
    return error;
}

esp_err_t weather_client_fetch_daily(
    const weather_config_t *config,
    const weather_client_location_t *location,
    weather_daily_t *daily)
{
    if (config == NULL || location == NULL || daily == NULL ||
        !weather_config_is_configured(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[WEATHER_REQUEST_URL_CAPACITY];
    if (!weather_request_build_daily_url(
            config->api_host, location->latitude_ten_thousandths,
            location->longitude_ten_thousandths, url, sizeof(url))) {
        return ESP_ERR_INVALID_ARG;
    }
    char *json = NULL;
    size_t length = 0U;
    esp_err_t error = weather_http_get_json(
        url, config->api_key, WEATHER_DAILY_RESPONSE_MAX_JSON_BYTES,
        &json, &length);
    if (error == ESP_OK) {
        error = response_error(
            weather_response_parse_qweather_v1_daily(json, length,
                                                      daily));
    }
    if (json != NULL) {
        memset(json, 0, length);
        free(json);
    }
    return error;
}
