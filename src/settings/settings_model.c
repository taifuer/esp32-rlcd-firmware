#include "settings_model.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    FORM_FIELD_TIMEZONE = 1U << 0,
    FORM_FIELD_UNIT = 1U << 1,
    FORM_FIELD_VOLUME = 1U << 2,
    FORM_FIELD_UPDATES = 1U << 3,
    FORM_FIELD_ALARM = 1U << 4,
    FORM_FIELD_ALARM_HOUR = 1U << 5,
    FORM_FIELD_ALARM_MINUTE = 1U << 6,
    FORM_FIELD_ALARM_DAYS = 1U << 7,
    FORM_FIELD_ALL = FORM_FIELD_TIMEZONE | FORM_FIELD_UNIT |
                     FORM_FIELD_VOLUME |
                     FORM_FIELD_UPDATES | FORM_FIELD_ALARM |
                     FORM_FIELD_ALARM_HOUR | FORM_FIELD_ALARM_MINUTE |
                     FORM_FIELD_ALARM_DAYS,
    FORM_KEY_CAPACITY = 16,
    FORM_VALUE_CAPACITY = 24,
};

void app_settings_defaults(app_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    *settings = (app_settings_t){
        .schema_version = APP_SETTINGS_SCHEMA_VERSION,
        .manual_saving_requested = false,
        .utc_offset_minutes = APP_SETTINGS_DEFAULT_UTC_OFFSET_MINUTES,
        .temperature_unit = APP_TEMPERATURE_UNIT_CELSIUS,
        .audio_playback_volume =
            APP_SETTINGS_DEFAULT_AUDIO_PLAYBACK_VOLUME,
        .update_channel = APP_UPDATE_CHANNEL_STABLE,
        .alarm_enabled = false,
        .alarm_hour = APP_SETTINGS_DEFAULT_ALARM_HOUR,
        .alarm_minute = APP_SETTINGS_DEFAULT_ALARM_MINUTE,
        .alarm_weekdays = APP_SETTINGS_ALARM_WEEKDAYS_MASK,
    };
}

static bool utc_offset_is_valid(int16_t offset)
{
    return offset >= APP_SETTINGS_MIN_UTC_OFFSET_MINUTES &&
           offset <= APP_SETTINGS_MAX_UTC_OFFSET_MINUTES &&
           offset % APP_SETTINGS_UTC_OFFSET_STEP_MINUTES == 0;
}

bool app_settings_validate(const app_settings_t *settings)
{
    return settings != NULL &&
           settings->schema_version == APP_SETTINGS_SCHEMA_VERSION &&
           utc_offset_is_valid(settings->utc_offset_minutes) &&
           (settings->temperature_unit == APP_TEMPERATURE_UNIT_CELSIUS ||
            settings->temperature_unit ==
                APP_TEMPERATURE_UNIT_FAHRENHEIT) &&
           settings->audio_playback_volume <= 100U &&
           (settings->update_channel == APP_UPDATE_CHANNEL_STABLE ||
            settings->update_channel == APP_UPDATE_CHANNEL_BETA) &&
           settings->alarm_hour < 24U &&
           settings->alarm_minute < 60U &&
           settings->alarm_weekdays != 0U &&
           (settings->alarm_weekdays &
            (uint8_t)~APP_SETTINGS_ALARM_ALL_DAYS_MASK) == 0U;
}

bool app_manual_saving_from_legacy_power(uint16_t schema_version,
                                         uint8_t value,
                                         bool *manual_requested)
{
    if (manual_requested == NULL || schema_version < 1U ||
        schema_version > 5U) {
        return false;
    }
    if (schema_version <= 4U) {
        if (value > 1U) {
            return false;
        }
        *manual_requested = value == 1U;
        return true;
    }
    if (value > 2U) {
        return false;
    }
    *manual_requested = value == 1U;
    return true;
}

bool app_settings_format_posix_tz(int16_t utc_offset_minutes,
                                  char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0U ||
        !utc_offset_is_valid(utc_offset_minutes)) {
        return false;
    }

    if (utc_offset_minutes == 0) {
        const int written = snprintf(buffer, capacity, "UTC0");
        return written > 0 && (size_t)written < capacity;
    }

    const char sign = utc_offset_minutes > 0 ? '-' : '+';
    const int absolute_minutes = utc_offset_minutes > 0
                                     ? utc_offset_minutes
                                     : -utc_offset_minutes;
    const int hours = absolute_minutes / 60;
    const int minutes = absolute_minutes % 60;
    const int written = minutes == 0
                            ? snprintf(buffer, capacity, "UTC%c%d", sign,
                                       hours)
                            : snprintf(buffer, capacity, "UTC%c%d:%02d", sign,
                                       hours, minutes);
    return written > 0 && (size_t)written < capacity;
}

static int hexadecimal_value(unsigned char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool decode_form_part(const char *text, size_t length, char *decoded,
                             size_t capacity)
{
    if (text == NULL || decoded == NULL || capacity == 0U) {
        return false;
    }

    size_t output = 0U;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char value = (unsigned char)text[index];
        if (value == '%') {
            if (index + 2U >= length) {
                return false;
            }
            const int high = hexadecimal_value((unsigned char)text[index + 1U]);
            const int low = hexadecimal_value((unsigned char)text[index + 2U]);
            if (high < 0 || low < 0) {
                return false;
            }
            value = (unsigned char)((high << 4U) | low);
            index += 2U;
        } else if (value == '+') {
            value = ' ';
        }
        if (value == '\0' || output + 1U >= capacity) {
            return false;
        }
        decoded[output++] = (char)value;
    }
    decoded[output] = '\0';
    return true;
}

static bool parse_decimal(const char *text, int minimum, int maximum,
                          int *parsed)
{
    if (text == NULL || parsed == NULL || text[0] == '\0') {
        return false;
    }

    size_t index = 0U;
    int sign = 1;
    if (text[index] == '+' || text[index] == '-') {
        sign = text[index] == '-' ? -1 : 1;
        ++index;
    }
    if (text[index] == '\0') {
        return false;
    }

    int value = 0;
    for (; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        const int digit = text[index] - '0';
        if (value > (INT_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    value *= sign;
    if (value < minimum || value > maximum) {
        return false;
    }
    *parsed = value;
    return true;
}

static bool assign_form_field(const char *key, const char *value,
                              app_settings_t *settings,
                              unsigned *seen_fields)
{
    unsigned field = 0U;
    if (strcmp(key, "timezone") == 0) {
        field = FORM_FIELD_TIMEZONE;
        int offset = 0;
        if (!parse_decimal(value, APP_SETTINGS_MIN_UTC_OFFSET_MINUTES,
                           APP_SETTINGS_MAX_UTC_OFFSET_MINUTES, &offset)) {
            return false;
        }
        settings->utc_offset_minutes = (int16_t)offset;
    } else if (strcmp(key, "unit") == 0) {
        field = FORM_FIELD_UNIT;
        if (strcmp(value, "c") == 0) {
            settings->temperature_unit = APP_TEMPERATURE_UNIT_CELSIUS;
        } else if (strcmp(value, "f") == 0) {
            settings->temperature_unit = APP_TEMPERATURE_UNIT_FAHRENHEIT;
        } else {
            return false;
        }
    } else if (strcmp(key, "volume") == 0) {
        field = FORM_FIELD_VOLUME;
        int volume = 0;
        if (!parse_decimal(value, 0, 100, &volume)) {
            return false;
        }
        settings->audio_playback_volume = (uint8_t)volume;
    } else if (strcmp(key, "updates") == 0) {
        field = FORM_FIELD_UPDATES;
        if (strcmp(value, "stable") == 0) {
            settings->update_channel = APP_UPDATE_CHANNEL_STABLE;
        } else if (strcmp(value, "beta") == 0) {
            settings->update_channel = APP_UPDATE_CHANNEL_BETA;
        } else {
            return false;
        }
    } else if (strcmp(key, "alarm") == 0) {
        field = FORM_FIELD_ALARM;
        if (strcmp(value, "off") == 0) {
            settings->alarm_enabled = false;
        } else if (strcmp(value, "on") == 0) {
            settings->alarm_enabled = true;
        } else {
            return false;
        }
    } else if (strcmp(key, "alarm_hour") == 0) {
        field = FORM_FIELD_ALARM_HOUR;
        int hour = 0;
        if (!parse_decimal(value, 0, 23, &hour)) {
            return false;
        }
        settings->alarm_hour = (uint8_t)hour;
    } else if (strcmp(key, "alarm_minute") == 0) {
        field = FORM_FIELD_ALARM_MINUTE;
        int minute = 0;
        if (!parse_decimal(value, 0, 59, &minute)) {
            return false;
        }
        settings->alarm_minute = (uint8_t)minute;
    } else if (strcmp(key, "alarm_days") == 0) {
        field = FORM_FIELD_ALARM_DAYS;
        int weekdays = 0;
        if (!parse_decimal(value, 1,
                           APP_SETTINGS_ALARM_ALL_DAYS_MASK,
                           &weekdays)) {
            return false;
        }
        settings->alarm_weekdays = (uint8_t)weekdays;
    } else {
        return false;
    }

    if ((*seen_fields & field) != 0U) {
        return false;
    }
    *seen_fields |= field;
    return true;
}

bool app_settings_parse_form(const char *body, size_t length,
                             const app_settings_t *base,
                             app_settings_t *settings)
{
    if (body == NULL || base == NULL || settings == NULL ||
        !app_settings_validate(base) || length == 0U ||
        length > APP_SETTINGS_FORM_MAX_LENGTH) {
        return false;
    }

    app_settings_t parsed = *base;
    unsigned seen_fields = 0U;
    size_t start = 0U;
    while (start < length) {
        size_t end = start;
        while (end < length && body[end] != '&') {
            ++end;
        }
        if (end == start) {
            return false;
        }

        size_t equals = start;
        while (equals < end && body[equals] != '=') {
            ++equals;
        }
        if (equals == start || equals == end) {
            return false;
        }
        for (size_t index = equals + 1U; index < end; ++index) {
            if (body[index] == '=') {
                return false;
            }
        }

        char key[FORM_KEY_CAPACITY];
        char value[FORM_VALUE_CAPACITY];
        if (!decode_form_part(&body[start], equals - start, key,
                              sizeof(key)) ||
            !decode_form_part(&body[equals + 1U], end - equals - 1U, value,
                              sizeof(value)) ||
            !assign_form_field(key, value, &parsed, &seen_fields)) {
            return false;
        }

        if (end == length) {
            start = length;
        } else {
            start = end + 1U;
            if (start == length) {
                return false;
            }
        }
    }

    if (seen_fields != FORM_FIELD_ALL || !app_settings_validate(&parsed)) {
        return false;
    }
    *settings = parsed;
    return true;
}
