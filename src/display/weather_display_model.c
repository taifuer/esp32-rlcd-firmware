#include "weather_display_model.h"

#include <stdio.h>
#include <string.h>

static int rounded_temperature(
    int16_t tenths_celsius, bool fahrenheit)
{
    int value_tenths = tenths_celsius;
    if (fahrenheit) {
        value_tenths = value_tenths * 9 / 5 + 320;
    }
    return value_tenths >= 0 ? (value_tenths + 5) / 10
                             : (value_tenths - 5) / 10;
}

display_weather_icon_t display_weather_icon_from_qweather_code(
    uint16_t condition_code)
{
    if (condition_code == 100U || condition_code == 150U ||
        condition_code == 900U) {
        return DISPLAY_WEATHER_ICON_CLEAR;
    }
    if ((condition_code >= 101U && condition_code <= 104U) ||
        (condition_code >= 151U && condition_code <= 153U)) {
        return DISPLAY_WEATHER_ICON_CLOUDY;
    }
    if (condition_code >= 200U && condition_code <= 213U) {
        return DISPLAY_WEATHER_ICON_WIND;
    }
    if (condition_code >= 302U && condition_code <= 304U) {
        return DISPLAY_WEATHER_ICON_THUNDER;
    }
    if (condition_code >= 300U && condition_code <= 399U) {
        return DISPLAY_WEATHER_ICON_RAIN;
    }
    if ((condition_code >= 400U && condition_code <= 499U) ||
        condition_code == 901U) {
        return DISPLAY_WEATHER_ICON_SNOW;
    }
    if (condition_code >= 500U && condition_code <= 515U) {
        return DISPLAY_WEATHER_ICON_FOG;
    }
    return DISPLAY_WEATHER_ICON_UNKNOWN;
}

bool display_weather_format_temperature(
    char *buffer, size_t capacity,
    int16_t tenths_celsius, bool fahrenheit,
    bool include_unit)
{
    if (buffer == NULL || capacity == 0U) {
        return false;
    }

    const int value = rounded_temperature(tenths_celsius, fahrenheit);
    const int written = include_unit
                            ? snprintf(buffer, capacity, "%d°%c", value,
                                       fahrenheit ? 'F' : 'C')
                            : snprintf(buffer, capacity, "%d°", value);
    if (written < 0 || (size_t)written >= capacity) {
        buffer[0] = '\0';
        return false;
    }
    return true;
}

bool display_weather_format_source(
    char *buffer, size_t capacity,
    display_weather_freshness_t freshness,
    bool update_time_valid,
    uint8_t update_month, uint8_t update_day,
    uint8_t update_hour, uint8_t update_minute)
{
    if (buffer == NULL || capacity == 0U ||
        (update_time_valid &&
         (update_month < 1U || update_month > 12U ||
          update_day < 1U || update_day > 31U ||
          update_hour > 23U || update_minute > 59U))) {
        if (buffer != NULL && capacity > 0U) {
            buffer[0] = '\0';
        }
        return false;
    }

    int written;
    if (freshness == DISPLAY_WEATHER_FRESHNESS_FRESH &&
        update_time_valid) {
        written = snprintf(buffer, capacity, "QWeather %02u:%02u",
                           update_hour, update_minute);
    } else if (freshness == DISPLAY_WEATHER_FRESHNESS_STALE &&
               update_time_valid) {
        written = snprintf(buffer, capacity,
                           "QWeather | CACHED %02u:%02u",
                           update_hour, update_minute);
    } else if (freshness == DISPLAY_WEATHER_FRESHNESS_EXPIRED &&
               update_time_valid) {
        written = snprintf(buffer, capacity,
                           "QWeather | OLD %02u-%02u %02u:%02u",
                           update_month, update_day,
                           update_hour, update_minute);
    } else {
        written = snprintf(buffer, capacity, "QWeather | CACHED");
    }

    if (written < 0 || (size_t)written >= capacity) {
        buffer[0] = '\0';
        return false;
    }
    return true;
}

static bool leap_year(uint16_t year)
{
    return year % 4U == 0U &&
           (year % 100U != 0U || year % 400U == 0U);
}

static bool valid_date(uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t DAYS_PER_MONTH[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (year < 1970U || year > 9999U || month < 1U || month > 12U ||
        day < 1U) {
        return false;
    }
    uint8_t maximum = DAYS_PER_MONTH[month - 1U];
    if (month == 2U && leap_year(year)) {
        ++maximum;
    }
    return day <= maximum;
}

static bool parse_date(const char *text, uint16_t *year,
                       uint8_t *month, uint8_t *day)
{
    if (text == NULL || strlen(text) != 10U || text[4] != '-' ||
        text[7] != '-') {
        return false;
    }
    const size_t digits[] = {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U};
    for (size_t index = 0U; index < sizeof(digits) / sizeof(digits[0]);
         ++index) {
        if (text[digits[index]] < '0' || text[digits[index]] > '9') {
            return false;
        }
    }
    const uint16_t parsed_year =
        (uint16_t)((text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                   (text[2] - '0') * 10 + (text[3] - '0'));
    const uint8_t parsed_month =
        (uint8_t)((text[5] - '0') * 10 + (text[6] - '0'));
    const uint8_t parsed_day =
        (uint8_t)((text[8] - '0') * 10 + (text[9] - '0'));
    if (!valid_date(parsed_year, parsed_month, parsed_day)) {
        return false;
    }
    *year = parsed_year;
    *month = parsed_month;
    *day = parsed_day;
    return true;
}

static int64_t civil_day_number(uint16_t year, uint8_t month, uint8_t day)
{
    int64_t adjusted_year = year;
    adjusted_year -= month <= 2U ? 1 : 0;
    const int64_t era = adjusted_year / 400;
    const unsigned int year_of_era =
        (unsigned int)(adjusted_year - era * 400);
    const unsigned int adjusted_month =
        month > 2U ? (unsigned int)month - 3U
                   : (unsigned int)month + 9U;
    const unsigned int day_of_year =
        (153U * adjusted_month + 2U) / 5U + (unsigned int)day - 1U;
    const unsigned int day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U +
        day_of_year;
    return era * 146097 + (int64_t)day_of_era;
}

bool display_weather_format_day_label(
    char *buffer, size_t capacity, const char *forecast_date,
    bool current_date_valid, uint16_t current_year,
    uint8_t current_month, uint8_t current_day)
{
    if (buffer == NULL || capacity == 0U) {
        return false;
    }
    uint16_t forecast_year = 0U;
    uint8_t forecast_month = 0U;
    uint8_t forecast_day = 0U;
    if (!parse_date(forecast_date, &forecast_year, &forecast_month,
                    &forecast_day)) {
        buffer[0] = '\0';
        return false;
    }

    const char *relative = NULL;
    if (current_date_valid &&
        valid_date(current_year, current_month, current_day)) {
        const int64_t difference =
            civil_day_number(forecast_year, forecast_month, forecast_day) -
            civil_day_number(current_year, current_month, current_day);
        if (difference == 0) {
            relative = "今天";
        } else if (difference == 1) {
            relative = "明天";
        } else if (difference == 2) {
            relative = "后天";
        }
    }

    const int written = relative != NULL
                            ? snprintf(buffer, capacity, "%s", relative)
                            : snprintf(buffer, capacity, "%u/%u",
                                       forecast_month, forecast_day);
    if (written < 0 || (size_t)written >= capacity) {
        buffer[0] = '\0';
        return false;
    }
    return true;
}
