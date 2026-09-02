#include "weather_response.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "cJSON.h"

#define WEATHER_TEMPERATURE_MIN_CELSIUS (-100.0)
#define WEATHER_TEMPERATURE_MAX_CELSIUS 80.0
#define WEATHER_FEELS_LIKE_MIN_CELSIUS (-120.0)
#define WEATHER_FEELS_LIKE_MAX_CELSIUS 100.0

static const cJSON *unique_object_item(
    const cJSON *object,
    const char *name)
{
    const cJSON *match = NULL;

    if (!cJSON_IsObject(object) || name == NULL) {
        return NULL;
    }
    for (const cJSON *item = object->child; item != NULL;
         item = item->next) {
        if (item->string != NULL && strcmp(item->string, name) == 0) {
            if (match != NULL) {
                return NULL;
            }
            match = item;
        }
    }
    return match;
}

static cJSON *parse_json_object(
    const char *json,
    size_t length,
    size_t maximum_length,
    weather_response_result_t *result)
{
    const char *parse_end = NULL;
    cJSON *root;

    if (length > maximum_length) {
        *result = WEATHER_RESPONSE_RESULT_TOO_LARGE;
        return NULL;
    }
    if (length == 0U) {
        *result = WEATHER_RESPONSE_RESULT_INVALID_JSON;
        return NULL;
    }

    root = cJSON_ParseWithLengthOpts(json, length, &parse_end, false);
    if (root == NULL) {
        *result = WEATHER_RESPONSE_RESULT_INVALID_JSON;
        return NULL;
    }
    while (parse_end < json + length &&
           (*parse_end == ' ' || *parse_end == '\t' ||
            *parse_end == '\r' || *parse_end == '\n')) {
        ++parse_end;
    }
    if (parse_end != json + length || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        *result = WEATHER_RESPONSE_RESULT_INVALID_JSON;
        return NULL;
    }
    *result = WEATHER_RESPONSE_RESULT_OK;
    return root;
}

static bool utf8_text_is_safe(const char *text, size_t maximum_bytes)
{
    size_t offset = 0U;
    const size_t length = text == NULL ? 0U : strlen(text);

    if (length == 0U || length > maximum_bytes) {
        return false;
    }
    while (offset < length) {
        const unsigned char first = (unsigned char)text[offset];
        size_t sequence_length;

        if (first < 0x80U) {
            if (first < 0x20U || first == 0x7fU) {
                return false;
            }
            sequence_length = 1U;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            sequence_length = 2U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            sequence_length = 3U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            sequence_length = 4U;
        } else {
            return false;
        }
        if (sequence_length > length - offset) {
            return false;
        }
        for (size_t continuation = 1U;
             continuation < sequence_length; ++continuation) {
            const unsigned char value =
                (unsigned char)text[offset + continuation];
            if ((value & 0xc0U) != 0x80U) {
                return false;
            }
        }
        if ((first == 0xe0U &&
             (unsigned char)text[offset + 1U] < 0xa0U) ||
            (first == 0xedU &&
             (unsigned char)text[offset + 1U] >= 0xa0U) ||
            (first == 0xf0U &&
             (unsigned char)text[offset + 1U] < 0x90U) ||
            (first == 0xf4U &&
             (unsigned char)text[offset + 1U] >= 0x90U)) {
            return false;
        }
        offset += sequence_length;
    }
    return true;
}

static bool copy_safe_text(
    const cJSON *item,
    char *destination,
    size_t maximum_bytes)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        !utf8_text_is_safe(item->valuestring, maximum_bytes)) {
        return false;
    }
    memcpy(destination, item->valuestring, strlen(item->valuestring) + 1U);
    return true;
}

static weather_response_result_t parse_condition(
    const cJSON *condition,
    uint16_t *code,
    char text[WEATHER_CONDITION_TEXT_CAPACITY])
{
    const cJSON *code_item;
    const cJSON *text_item;
    uint16_t parsed_code = 0U;

    if (!cJSON_IsObject(condition)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    code_item = unique_object_item(condition, "code");
    text_item = unique_object_item(condition, "text");
    if (!cJSON_IsString(code_item) || code_item->valuestring == NULL ||
        strlen(code_item->valuestring) != 3U ||
        !cJSON_IsString(text_item) || text_item->valuestring == NULL ||
        !utf8_text_is_safe(text_item->valuestring,
                           WEATHER_CONDITION_TEXT_MAX_BYTES)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    for (size_t index = 0U; index < 3U; ++index) {
        const char character = code_item->valuestring[index];
        if (character < '0' || character > '9') {
            return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
        }
        parsed_code = (uint16_t)(parsed_code * 10U +
                                 (uint16_t)(character - '0'));
    }
    if (parsed_code < 100U || parsed_code > 999U) {
        return WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE;
    }
    *code = parsed_code;
    memcpy(text, text_item->valuestring, strlen(text_item->valuestring) + 1U);
    return WEATHER_RESPONSE_RESULT_OK;
}

static weather_response_result_t parse_temperature(
    const cJSON *temperature,
    double minimum_celsius,
    double maximum_celsius,
    int16_t *tenths_celsius)
{
    const cJSON *value;
    const cJSON *unit;
    double scaled;

    if (!cJSON_IsObject(temperature)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    value = unique_object_item(temperature, "value");
    unit = unique_object_item(temperature, "unit");
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        !cJSON_IsString(unit) || unit->valuestring == NULL ||
        strcmp(unit->valuestring, "°C") != 0) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    if (value->valuedouble < minimum_celsius ||
        value->valuedouble > maximum_celsius) {
        return WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE;
    }

    scaled = value->valuedouble * 10.0;
    *tenths_celsius = (int16_t)(scaled >= 0.0 ? scaled + 0.5
                                              : scaled - 0.5);
    return WEATHER_RESPONSE_RESULT_OK;
}

static bool leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int two_digits(const char *text)
{
    if (text[0] < '0' || text[0] > '9' ||
        text[1] < '0' || text[1] > '9') {
        return -1;
    }
    return (text[0] - '0') * 10 + (text[1] - '0');
}

static int four_digits(const char *text)
{
    int value = 0;

    for (size_t index = 0U; index < 4U; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10 + text[index] - '0';
    }
    return value;
}

static bool rfc3339_date_is_valid(const char *text)
{
    static const uint8_t DAYS_PER_MONTH[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    const size_t length = text == NULL ? 0U : strlen(text);
    int year;
    int month;
    int day;
    int hour;
    int minute;
    size_t timezone_offset;

    if (length < 17U || length > 32U || text[4] != '-' ||
        text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        length <= 16U) {
        return false;
    }
    year = four_digits(text);
    month = two_digits(text + 5U);
    day = two_digits(text + 8U);
    hour = two_digits(text + 11U);
    minute = two_digits(text + 14U);
    if (year < 1970 || year > 9999 || month < 1 || month > 12 ||
        day < 1 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59) {
        return false;
    }
    int days_in_month = DAYS_PER_MONTH[month - 1];
    if (month == 2 && leap_year(year)) {
        ++days_in_month;
    }
    if (day > days_in_month) {
        return false;
    }

    timezone_offset = 16U;
    if (text[timezone_offset] == ':') {
        if (timezone_offset + 3U > length) {
            return false;
        }
        const int second = two_digits(text + timezone_offset + 1U);
        if (second < 0 || second > 59) {
            return false;
        }
        timezone_offset += 3U;
    }
    if (timezone_offset < length && text[timezone_offset] == '.') {
        /* RFC 3339 permits a fractional part only after seconds. */
        if (timezone_offset < 19U) {
            return false;
        }
        ++timezone_offset;
        const size_t fractional_start = timezone_offset;
        while (timezone_offset < length &&
               text[timezone_offset] >= '0' &&
               text[timezone_offset] <= '9') {
            ++timezone_offset;
        }
        if (timezone_offset == fractional_start) {
            return false;
        }
    }
    if (timezone_offset + 1U == length &&
        text[timezone_offset] == 'Z') {
        return true;
    }
    if (timezone_offset + 6U != length ||
        (text[timezone_offset] != '+' && text[timezone_offset] != '-') ||
        text[timezone_offset + 3U] != ':') {
        return false;
    }
    const int timezone_hour = two_digits(text + timezone_offset + 1U);
    const int timezone_minute = two_digits(text + timezone_offset + 4U);
    return timezone_hour >= 0 && timezone_hour <= 14 &&
           timezone_minute >= 0 && timezone_minute <= 59 &&
           (timezone_hour != 14 || timezone_minute == 0);
}

static weather_response_result_t parse_current_response(
    const char *json,
    size_t length,
    weather_current_t *current)
{
    weather_response_result_t result;
    cJSON *root = parse_json_object(
        json, length, WEATHER_CURRENT_RESPONSE_MAX_JSON_BYTES, &result);

    if (root == NULL) {
        return result;
    }
    result = parse_condition(
        unique_object_item(root, "condition"),
        &current->condition_code,
        current->condition_text);
    if (result == WEATHER_RESPONSE_RESULT_OK) {
        result = parse_temperature(
            unique_object_item(root, "temperature"),
            WEATHER_TEMPERATURE_MIN_CELSIUS,
            WEATHER_TEMPERATURE_MAX_CELSIUS,
            &current->temperature_tenths_celsius);
    }
    if (result == WEATHER_RESPONSE_RESULT_OK) {
        result = parse_temperature(
            unique_object_item(root, "feelsLike"),
            WEATHER_FEELS_LIKE_MIN_CELSIUS,
            WEATHER_FEELS_LIKE_MAX_CELSIUS,
            &current->feels_like_tenths_celsius);
    }
    cJSON_Delete(root);
    return result;
}

static weather_response_result_t parse_probability(
    const cJSON *precipitation,
    uint8_t *percent)
{
    const cJSON *probability;

    if (!cJSON_IsObject(precipitation)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    probability = unique_object_item(precipitation, "probability");
    if (!cJSON_IsNumber(probability) ||
        !isfinite(probability->valuedouble)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    if (probability->valuedouble < 0.0 ||
        probability->valuedouble > 1.0) {
        return WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE;
    }
    *percent = (uint8_t)(probability->valuedouble * 100.0 + 0.5);
    return WEATHER_RESPONSE_RESULT_OK;
}

static weather_response_result_t parse_forecast_day(
    const cJSON *object,
    weather_forecast_day_t *day)
{
    const cJSON *forecast_start_time;
    const cJSON *daytime;
    weather_response_result_t result;

    if (!cJSON_IsObject(object)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    forecast_start_time = unique_object_item(object, "forecastStartTime");
    if (!cJSON_IsString(forecast_start_time) ||
        forecast_start_time->valuestring == NULL ||
        !rfc3339_date_is_valid(forecast_start_time->valuestring)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    memcpy(day->date, forecast_start_time->valuestring,
           WEATHER_DATE_CAPACITY - 1U);
    day->date[WEATHER_DATE_CAPACITY - 1U] = '\0';

    result = parse_temperature(
        unique_object_item(object, "temperatureMax"),
        WEATHER_TEMPERATURE_MIN_CELSIUS,
        WEATHER_TEMPERATURE_MAX_CELSIUS,
        &day->temperature_high_tenths_celsius);
    if (result != WEATHER_RESPONSE_RESULT_OK) {
        return result;
    }
    result = parse_temperature(
        unique_object_item(object, "temperatureMin"),
        WEATHER_TEMPERATURE_MIN_CELSIUS,
        WEATHER_TEMPERATURE_MAX_CELSIUS,
        &day->temperature_low_tenths_celsius);
    if (result != WEATHER_RESPONSE_RESULT_OK) {
        return result;
    }
    if (day->temperature_low_tenths_celsius >
        day->temperature_high_tenths_celsius) {
        return WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE;
    }

    daytime = unique_object_item(object, "daytime");
    if (!cJSON_IsObject(daytime)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    result = parse_condition(
        unique_object_item(daytime, "condition"),
        &day->daytime_condition_code,
        day->daytime_condition_text);
    if (result != WEATHER_RESPONSE_RESULT_OK) {
        return result;
    }
    return parse_probability(
        unique_object_item(daytime, "precipitation"),
        &day->precipitation_probability_percent);
}

static weather_response_result_t parse_daily_response(
    const char *json,
    size_t length,
    weather_daily_t *daily)
{
    weather_response_result_t result;
    cJSON *root = parse_json_object(
        json, length, WEATHER_DAILY_RESPONSE_MAX_JSON_BYTES, &result);
    const cJSON *days;
    int count;

    if (root == NULL) {
        return result;
    }
    days = unique_object_item(root, "days");
    if (!cJSON_IsArray(days)) {
        cJSON_Delete(root);
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    count = cJSON_GetArraySize(days);
    if (count < 1 || count > (int)WEATHER_FORECAST_DAY_LIMIT) {
        cJSON_Delete(root);
        return WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE;
    }
    daily->day_count = (size_t)count;
    for (int index = 0; index < count; ++index) {
        result = parse_forecast_day(
            cJSON_GetArrayItem(days, index), &daily->days[index]);
        if (result != WEATHER_RESPONSE_RESULT_OK) {
            cJSON_Delete(root);
            return result;
        }
        if (index > 0 &&
            strcmp(daily->days[index - 1].date,
                   daily->days[index].date) >= 0) {
            cJSON_Delete(root);
            return WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE;
        }
    }
    cJSON_Delete(root);
    return WEATHER_RESPONSE_RESULT_OK;
}

weather_response_result_t weather_response_parse_qweather_v1_current(
    const char *json,
    size_t json_length,
    weather_current_t *current)
{
    weather_current_t candidate = {0};
    weather_response_result_t result;

    if (json == NULL || current == NULL) {
        return WEATHER_RESPONSE_RESULT_INVALID_ARGUMENT;
    }
    result = parse_current_response(json, json_length, &candidate);
    if (result == WEATHER_RESPONSE_RESULT_OK) {
        *current = candidate;
    }
    return result;
}

weather_response_result_t weather_response_parse_qweather_v1_daily(
    const char *json,
    size_t json_length,
    weather_daily_t *daily)
{
    weather_daily_t candidate = {0};
    weather_response_result_t result;

    if (json == NULL || daily == NULL) {
        return WEATHER_RESPONSE_RESULT_INVALID_ARGUMENT;
    }
    result = parse_daily_response(json, json_length, &candidate);
    if (result == WEATHER_RESPONSE_RESULT_OK) {
        *daily = candidate;
    }
    return result;
}

weather_response_result_t weather_response_parse_qweather_v1(
    const char *current_json,
    size_t current_json_length,
    const char *daily_json,
    size_t daily_json_length,
    int64_t fetched_at_epoch_seconds,
    weather_snapshot_t *snapshot)
{
    weather_snapshot_t candidate = {0};
    weather_response_result_t result;

    if (current_json == NULL || daily_json == NULL || snapshot == NULL ||
        fetched_at_epoch_seconds <= 0) {
        return WEATHER_RESPONSE_RESULT_INVALID_ARGUMENT;
    }
    result = weather_response_parse_qweather_v1_current(
        current_json, current_json_length, &candidate.current);
    if (result != WEATHER_RESPONSE_RESULT_OK) {
        return result;
    }
    result = weather_response_parse_qweather_v1_daily(
        daily_json, daily_json_length, &candidate.daily);
    if (result != WEATHER_RESPONSE_RESULT_OK) {
        return result;
    }
    candidate.fetched_at_epoch_seconds = fetched_at_epoch_seconds;
    *snapshot = candidate;
    return WEATHER_RESPONSE_RESULT_OK;
}

static bool string_has_suffix(
    const char *text,
    size_t text_length,
    const char *suffix,
    size_t suffix_length)
{
    return text_length > suffix_length &&
           memcmp(text + text_length - suffix_length,
                  suffix, suffix_length) == 0;
}

static size_t administrative_stem_length(const char *text)
{
    static const char *const SUFFIXES[] = {
        "特别行政区", "维吾尔自治区", "壮族自治区",
        "回族自治区", "自治区", "自治州", "自治县",
        "自治旗", "地区", "新区", "省", "市", "区", "县", "盟", "旗",
    };
    const size_t length = strlen(text);

    for (size_t index = 0U;
         index < sizeof(SUFFIXES) / sizeof(SUFFIXES[0]); ++index) {
        const size_t suffix_length = strlen(SUFFIXES[index]);
        if (string_has_suffix(text, length, SUFFIXES[index],
                              suffix_length)) {
            return length - suffix_length;
        }
    }
    return length;
}

static bool administrative_name_matches(
    const char *expected,
    const char *actual)
{
    size_t expected_length;
    size_t actual_length;

    if (expected == NULL || actual == NULL ||
        expected[0] == '\0' || actual[0] == '\0') {
        return false;
    }
    if (strcmp(expected, actual) == 0) {
        return true;
    }
    expected_length = administrative_stem_length(expected);
    actual_length = administrative_stem_length(actual);
    return expected_length == actual_length && expected_length > 0U &&
           memcmp(expected, actual, expected_length) == 0;
}

static bool parse_microdegrees(
    const cJSON *item,
    int32_t absolute_limit,
    int32_t *microdegrees)
{
    const char *text;
    bool negative = false;
    size_t offset = 0U;
    int32_t integer_part = 0;
    int32_t fractional_part = 0;
    size_t integer_digits = 0U;
    size_t fractional_digits = 0U;
    int64_t value;

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    text = item->valuestring;
    if (text[offset] == '-' || text[offset] == '+') {
        negative = text[offset] == '-';
        ++offset;
    }
    while (text[offset] >= '0' && text[offset] <= '9') {
        if (integer_digits >= 3U) {
            return false;
        }
        integer_part = integer_part * 10 + text[offset] - '0';
        ++integer_digits;
        ++offset;
    }
    if (integer_digits == 0U) {
        return false;
    }
    if (text[offset] == '.') {
        ++offset;
        while (text[offset] >= '0' && text[offset] <= '9') {
            if (fractional_digits >= 6U) {
                return false;
            }
            fractional_part = fractional_part * 10 + text[offset] - '0';
            ++fractional_digits;
            ++offset;
        }
        if (fractional_digits == 0U) {
            return false;
        }
    }
    if (text[offset] != '\0') {
        return false;
    }
    while (fractional_digits < 6U) {
        fractional_part *= 10;
        ++fractional_digits;
    }
    value = (int64_t)integer_part * INT64_C(1000000) + fractional_part;
    if (negative) {
        value = -value;
    }
    if (value < -(int64_t)absolute_limit ||
        value > (int64_t)absolute_limit) {
        return false;
    }
    *microdegrees = (int32_t)value;
    return true;
}

typedef struct {
    char name[WEATHER_LOCATION_NAME_CAPACITY];
    char administrative_level_one[WEATHER_LOCATION_NAME_CAPACITY];
    char administrative_level_two[WEATHER_LOCATION_NAME_CAPACITY];
    int32_t latitude_microdegrees;
    int32_t longitude_microdegrees;
} weather_location_candidate_t;

static weather_response_result_t parse_location_candidate(
    const cJSON *object,
    weather_location_candidate_t *candidate)
{
    if (!cJSON_IsObject(object) ||
        !copy_safe_text(unique_object_item(object, "name"),
                        candidate->name,
                        WEATHER_LOCATION_NAME_MAX_BYTES) ||
        !copy_safe_text(unique_object_item(object, "adm1"),
                        candidate->administrative_level_one,
                        WEATHER_LOCATION_NAME_MAX_BYTES) ||
        !copy_safe_text(unique_object_item(object, "adm2"),
                        candidate->administrative_level_two,
                        WEATHER_LOCATION_NAME_MAX_BYTES)) {
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    if (!parse_microdegrees(unique_object_item(object, "lat"),
                            INT32_C(90000000),
                            &candidate->latitude_microdegrees) ||
        !parse_microdegrees(unique_object_item(object, "lon"),
                            INT32_C(180000000),
                            &candidate->longitude_microdegrees)) {
        return WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE;
    }
    return WEATHER_RESPONSE_RESULT_OK;
}

static int location_match_score(
    const weather_location_candidate_t *candidate,
    const char *province,
    const char *city)
{
    if (!administrative_name_matches(
            province, candidate->administrative_level_one)) {
        return -1;
    }
    if (administrative_name_matches(city, candidate->name)) {
        return 200;
    }
    if (administrative_name_matches(
            city, candidate->administrative_level_two)) {
        return 100;
    }
    return -1;
}

weather_response_result_t weather_response_select_qweather_geo_v2(
    const char *json,
    size_t json_length,
    const char *province,
    const char *city,
    weather_location_t *location)
{
    weather_response_result_t result;
    cJSON *root;
    const cJSON *code;
    const cJSON *locations;
    int count;
    int best_score = -1;
    weather_location_t candidate_location = {0};

    if (json == NULL || province == NULL || city == NULL ||
        location == NULL ||
        !utf8_text_is_safe(province, WEATHER_LOCATION_NAME_MAX_BYTES) ||
        !utf8_text_is_safe(city, WEATHER_LOCATION_NAME_MAX_BYTES)) {
        return WEATHER_RESPONSE_RESULT_INVALID_ARGUMENT;
    }
    root = parse_json_object(
        json, json_length, WEATHER_GEO_RESPONSE_MAX_JSON_BYTES, &result);
    if (root == NULL) {
        return result;
    }
    code = unique_object_item(root, "code");
    if (!cJSON_IsString(code) || code->valuestring == NULL) {
        cJSON_Delete(root);
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    if (strcmp(code->valuestring, "404") == 0) {
        cJSON_Delete(root);
        return WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND;
    }
    if (strcmp(code->valuestring, "200") != 0) {
        cJSON_Delete(root);
        return WEATHER_RESPONSE_RESULT_API_ERROR;
    }
    locations = unique_object_item(root, "location");
    if (!cJSON_IsArray(locations)) {
        cJSON_Delete(root);
        return WEATHER_RESPONSE_RESULT_INVALID_SCHEMA;
    }
    count = cJSON_GetArraySize(locations);
    if (count == 0) {
        cJSON_Delete(root);
        return WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND;
    }
    if (count > (int)WEATHER_GEO_LOCATION_LIMIT) {
        cJSON_Delete(root);
        return WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE;
    }

    for (int index = 0; index < count; ++index) {
        weather_location_candidate_t parsed = {0};
        result = parse_location_candidate(
            cJSON_GetArrayItem(locations, index), &parsed);
        if (result != WEATHER_RESPONSE_RESULT_OK) {
            continue;
        }
        const int score = location_match_score(&parsed, province, city);
        if (score > best_score) {
            best_score = score;
            memcpy(candidate_location.name, parsed.name,
                   strlen(parsed.name) + 1U);
            candidate_location.latitude_microdegrees =
                parsed.latitude_microdegrees;
            candidate_location.longitude_microdegrees =
                parsed.longitude_microdegrees;
        }
    }
    cJSON_Delete(root);
    if (best_score < 0) {
        return WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND;
    }
    *location = candidate_location;
    return WEATHER_RESPONSE_RESULT_OK;
}

const char *weather_response_result_name(weather_response_result_t result)
{
    switch (result) {
    case WEATHER_RESPONSE_RESULT_OK:
        return "ok";
    case WEATHER_RESPONSE_RESULT_INVALID_ARGUMENT:
        return "invalid_argument";
    case WEATHER_RESPONSE_RESULT_TOO_LARGE:
        return "too_large";
    case WEATHER_RESPONSE_RESULT_INVALID_JSON:
        return "invalid_json";
    case WEATHER_RESPONSE_RESULT_INVALID_SCHEMA:
        return "invalid_schema";
    case WEATHER_RESPONSE_RESULT_VALUE_OUT_OF_RANGE:
        return "value_out_of_range";
    case WEATHER_RESPONSE_RESULT_API_ERROR:
        return "api_error";
    case WEATHER_RESPONSE_RESULT_LOCATION_NOT_FOUND:
        return "location_not_found";
    default:
        return "invalid";
    }
}
