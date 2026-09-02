#include "weather_request.h"

#include <stdio.h>
#include <string.h>

static bool unreserved(uint8_t value)
{
    return (value >= (uint8_t)'a' && value <= (uint8_t)'z') ||
           (value >= (uint8_t)'A' && value <= (uint8_t)'Z') ||
           (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
           value == (uint8_t)'-' || value == (uint8_t)'_' ||
           value == (uint8_t)'.' || value == (uint8_t)'~';
}

static bool append_percent_encoded(char *output, size_t capacity,
                                   size_t *length, const char *input)
{
    static const char HEX[] = "0123456789ABCDEF";
    if (output == NULL || capacity == 0U || length == NULL ||
        input == NULL || *length >= capacity) {
        return false;
    }
    for (size_t index = 0U; input[index] != '\0'; ++index) {
        const uint8_t value = (uint8_t)input[index];
        const size_t needed = unreserved(value) ? 1U : 3U;
        if (needed >= capacity - *length) {
            output[0] = '\0';
            return false;
        }
        if (needed == 1U) {
            output[(*length)++] = (char)value;
        } else {
            output[(*length)++] = '%';
            output[(*length)++] = HEX[value >> 4U];
            output[(*length)++] = HEX[value & 0x0fU];
        }
    }
    output[*length] = '\0';
    return true;
}

uint32_t weather_location_fingerprint(uint32_t province_id,
                                      uint32_t city_id)
{
    uint32_t hash = UINT32_C(2166136261);
    const uint32_t ids[] = {province_id, city_id};
    for (size_t item = 0U; item < 2U; ++item) {
        for (size_t byte = 0U; byte < 4U; ++byte) {
            hash ^= (ids[item] >> (byte * 8U)) & 0xffU;
            hash *= UINT32_C(16777619);
        }
    }
    return hash == 0U ? 1U : hash;
}

bool weather_request_build_geo_url(const char *api_host,
                                   const char *location,
                                   const char *administrative_area,
                                   char *url, size_t capacity)
{
    if (api_host == NULL || api_host[0] == '\0' || location == NULL ||
        location[0] == '\0' || administrative_area == NULL ||
        administrative_area[0] == '\0' || url == NULL || capacity == 0U) {
        return false;
    }
    const int prefix_length = snprintf(
        url, capacity, "https://%s/geo/v2/city/lookup?location=", api_host);
    if (prefix_length < 0 || (size_t)prefix_length >= capacity) {
        if (capacity > 0U) {
            url[0] = '\0';
        }
        return false;
    }
    size_t length = (size_t)prefix_length;
    if (!append_percent_encoded(url, capacity, &length, location)) {
        return false;
    }
    static const char ADM[] = "&adm=";
    if (sizeof(ADM) - 1U >= capacity - length) {
        url[0] = '\0';
        return false;
    }
    memcpy(url + length, ADM, sizeof(ADM));
    length += sizeof(ADM) - 1U;
    if (!append_percent_encoded(url, capacity, &length,
                                administrative_area)) {
        return false;
    }
    static const char SUFFIX[] = "&range=cn&number=10&lang=zh";
    if (sizeof(SUFFIX) > capacity - length) {
        url[0] = '\0';
        return false;
    }
    memcpy(url + length, SUFFIX, sizeof(SUFFIX));
    return true;
}

static bool format_coordinate(int32_t value, char *output, size_t capacity)
{
    if (output == NULL || capacity == 0U || value < -1800000 ||
        value > 1800000) {
        return false;
    }
    /* The QWeather Weather v1 endpoint accepts at most two decimal places. */
    const int32_t hundredths =
        value >= 0 ? (value + 50) / 100 : (value - 50) / 100;
    const bool negative = hundredths < 0;
    const uint32_t magnitude = negative
                                   ? (uint32_t)(-(int64_t)hundredths)
                                   : (uint32_t)hundredths;
    const int written = snprintf(output, capacity, "%s%u.%02u",
                                 negative ? "-" : "",
                                 (unsigned)(magnitude / 100U),
                                 (unsigned)(magnitude % 100U));
    return written > 0 && (size_t)written < capacity;
}

static bool build_weather_url(const char *api_host, const char *resource,
                              int32_t latitude, int32_t longitude,
                              const char *query, char *url, size_t capacity)
{
    if (api_host == NULL || api_host[0] == '\0' || resource == NULL ||
        query == NULL || url == NULL || capacity == 0U ||
        latitude < -900000 || latitude > 900000 ||
        longitude < -1800000 || longitude > 1800000) {
        return false;
    }
    char latitude_text[16];
    char longitude_text[16];
    if (!format_coordinate(latitude, latitude_text, sizeof(latitude_text)) ||
        !format_coordinate(longitude, longitude_text,
                           sizeof(longitude_text))) {
        return false;
    }
    const int written = snprintf(url, capacity,
                                 "https://%s/weather/v1/%s/%s/%s?%s",
                                 api_host, resource, latitude_text,
                                 longitude_text, query);
    if (written <= 0 || (size_t)written >= capacity) {
        url[0] = '\0';
        return false;
    }
    return true;
}

bool weather_request_build_current_url(
    const char *api_host, int32_t latitude_ten_thousandths,
    int32_t longitude_ten_thousandths, char *url, size_t capacity)
{
    return build_weather_url(api_host, "current",
                             latitude_ten_thousandths,
                             longitude_ten_thousandths,
                             "localTime=true&lang=zh", url, capacity);
}

bool weather_request_build_daily_url(
    const char *api_host, int32_t latitude_ten_thousandths,
    int32_t longitude_ten_thousandths, char *url, size_t capacity)
{
    return build_weather_url(api_host, "daily",
                             latitude_ten_thousandths,
                             longitude_ten_thousandths,
                             "days=3&localTime=true&lang=zh", url,
                             capacity);
}
