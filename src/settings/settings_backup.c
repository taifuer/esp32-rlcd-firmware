#include "settings_backup.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t length;
    size_t offset;
} backup_reader_t;

static void whitespace(backup_reader_t *reader)
{
    while (reader->offset < reader->length) {
        const char ch = reader->text[reader->offset];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        ++reader->offset;
    }
}

static bool token(backup_reader_t *reader, char expected)
{
    whitespace(reader);
    if (reader->offset >= reader->length || reader->text[reader->offset] != expected) return false;
    ++reader->offset;
    return true;
}

/* Only fixed ASCII keys and enum strings belong to this format. Reject
 * escapes instead of accepting alternate spellings of privileged fields. */
static bool string(backup_reader_t *reader, char *buffer, size_t capacity)
{
    if (!token(reader, '"')) return false;
    size_t written = 0U;
    while (reader->offset < reader->length) {
        const unsigned char ch = (unsigned char)reader->text[reader->offset++];
        if (ch == '"') {
            buffer[written] = '\0';
            return true;
        }
        if (ch < 0x20U || ch > 0x7eU || ch == '\\' || written + 1U >= capacity) return false;
        buffer[written++] = (char)ch;
    }
    return false;
}

static bool integer(backup_reader_t *reader, int minimum, int maximum, int *value)
{
    whitespace(reader);
    bool negative = false;
    if (reader->offset < reader->length && reader->text[reader->offset] == '-') {
        negative = true;
        ++reader->offset;
    }
    if (reader->offset >= reader->length) return false;
    const size_t start = reader->offset;
    unsigned magnitude = 0U;
    while (reader->offset < reader->length) {
        const char ch = reader->text[reader->offset];
        if (ch < '0' || ch > '9') break;
        if (reader->offset > start && reader->text[start] == '0') return false;
        magnitude = magnitude * 10U + (unsigned)(ch - '0');
        if (magnitude > 32767U) return false;
        ++reader->offset;
    }
    if (reader->offset == start) return false;
    const int parsed = negative ? -(int)magnitude : (int)magnitude;
    if (parsed < minimum || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static bool boolean(backup_reader_t *reader, bool *value)
{
    whitespace(reader);
    const size_t remaining = reader->length - reader->offset;
    if (remaining >= 4U && memcmp(reader->text + reader->offset, "true", 4U) == 0) {
        reader->offset += 4U;
        *value = true;
        return true;
    }
    if (remaining >= 5U && memcmp(reader->text + reader->offset, "false", 5U) == 0) {
        reader->offset += 5U;
        *value = false;
        return true;
    }
    return false;
}

static bool preferences(backup_reader_t *reader, app_settings_t *settings)
{
    static const char *const keys[] = {
        "utc_offset_minutes", "temperature_unit", "audio_playback_volume",
        "alarm_volume", "alarm_enabled", "alarm_hour", "alarm_minute",
        "alarm_weekdays", "daily_pages_mask", "daily_pages_order",
        "music_mode", "timer_minutes",
    };
    unsigned seen = 0U;
    if (!token(reader, '{')) return false;
    for (size_t item = 0U; item < sizeof(keys) / sizeof(keys[0]); ++item) {
        char key[32];
        if (item != 0U && !token(reader, ',')) return false;
        if (!string(reader, key, sizeof(key)) || !token(reader, ':')) return false;
        unsigned index = 0U;
        while (index < sizeof(keys) / sizeof(keys[0]) && strcmp(key, keys[index]) != 0) ++index;
        if (index == sizeof(keys) / sizeof(keys[0]) || (seen & (1U << index)) != 0U) return false;
        seen |= 1U << index;
        int number = 0;
        char value[24];
        switch (index) {
        case 0:
            if (!integer(reader, APP_SETTINGS_MIN_UTC_OFFSET_MINUTES, APP_SETTINGS_MAX_UTC_OFFSET_MINUTES, &number)) return false;
            settings->utc_offset_minutes = (int16_t)number;
            break;
        case 1:
            if (!string(reader, value, sizeof(value))) return false;
            if (strcmp(value, "c") == 0) settings->temperature_unit = APP_TEMPERATURE_UNIT_CELSIUS;
            else if (strcmp(value, "f") == 0) settings->temperature_unit = APP_TEMPERATURE_UNIT_FAHRENHEIT;
            else return false;
            break;
        case 2:
        case 3:
            if (!integer(reader, 0, 100, &number)) return false;
            if (index == 2U) settings->audio_playback_volume = (uint8_t)number;
            else settings->alarm_volume = (uint8_t)number;
            break;
        case 4:
            if (!boolean(reader, &settings->alarm_enabled)) return false;
            break;
        case 5:
            if (!integer(reader, 0, 23, &number)) return false;
            settings->alarm_hour = (uint8_t)number;
            break;
        case 6:
            if (!integer(reader, 0, 59, &number)) return false;
            settings->alarm_minute = (uint8_t)number;
            break;
        case 7:
            if (!integer(reader, 1, APP_SETTINGS_ALARM_ALL_DAYS_MASK, &number)) return false;
            settings->alarm_weekdays = (uint8_t)number;
            break;
        case 8:
            if (!integer(reader, 0, APP_DAILY_PAGES_ALL_MASK, &number)) return false;
            settings->daily_pages_mask = (uint8_t)number;
            break;
        case 9:
            if (!token(reader, '[')) return false;
            for (size_t page = 0U; page < APP_DAILY_PAGE_COUNT; ++page) {
                if (page != 0U && !token(reader, ',')) return false;
                if (!integer(reader, 1, UINT8_MAX, &number)) return false;
                settings->daily_pages_order[page] = (uint8_t)number;
            }
            if (!token(reader, ']')) return false;
            break;
        case 10:
            if (!string(reader, value, sizeof(value))) return false;
            if (strcmp(value, "sequential") == 0) settings->music_mode = APP_MUSIC_MODE_SEQUENTIAL;
            else if (strcmp(value, "repeat_one") == 0) settings->music_mode = APP_MUSIC_MODE_REPEAT_ONE;
            else if (strcmp(value, "repeat_all") == 0) settings->music_mode = APP_MUSIC_MODE_REPEAT_ALL;
            else return false;
            break;
        case 11:
            if (!integer(reader, APP_SETTINGS_MIN_TIMER_MINUTES, APP_SETTINGS_MAX_TIMER_MINUTES, &number)) return false;
            settings->timer_minutes = (uint8_t)number;
            break;
        default: return false;
        }
    }
    return seen == 0xfffU && token(reader, '}');
}

bool settings_backup_parse(const char *json, size_t length,
                           const app_settings_t *base, app_settings_t *settings)
{
    if (json == NULL || settings == NULL || !app_settings_validate(base) ||
        length == 0U || length > APP_SETTINGS_BACKUP_MAX_LENGTH) return false;
    backup_reader_t reader = {.text = json, .length = length};
    app_settings_t parsed = *base;
    if (!token(&reader, '{')) return false;
    unsigned seen = 0U;
    for (unsigned item = 0U; item < 3U; ++item) {
        char key[32];
        if (item != 0U && !token(&reader, ',')) return false;
        if (!string(&reader, key, sizeof(key)) || !token(&reader, ':')) return false;
        unsigned field = 0U;
        if (strcmp(key, "format") == 0) {
            field = 1U;
            char value[32];
            if (!string(&reader, value, sizeof(value)) || strcmp(value, APP_SETTINGS_BACKUP_FORMAT) != 0) return false;
        } else if (strcmp(key, "version") == 0) {
            field = 2U;
            int version = 0;
            if (!integer(&reader, APP_SETTINGS_BACKUP_VERSION, APP_SETTINGS_BACKUP_VERSION, &version)) return false;
        } else if (strcmp(key, "preferences") == 0) {
            field = 4U;
            if (!preferences(&reader, &parsed)) return false;
        } else return false;
        if ((seen & field) != 0U) return false;
        seen |= field;
    }
    if (seen != 7U || !token(&reader, '}')) return false;
    whitespace(&reader);
    if (reader.offset != reader.length || !app_settings_validate(&parsed)) return false;
    *settings = parsed;
    return true;
}

bool settings_backup_encode(const app_settings_t *settings, char *buffer,
                            size_t capacity, size_t *length)
{
    if (!app_settings_validate(settings) || buffer == NULL || capacity == 0U || length == NULL) return false;
    const char *music = settings->music_mode == APP_MUSIC_MODE_REPEAT_ONE ? "repeat_one"
        : settings->music_mode == APP_MUSIC_MODE_REPEAT_ALL ? "repeat_all" : "sequential";
    const int written = snprintf(buffer, capacity,
        "{\"format\":\"%s\",\"version\":%u,\"preferences\":{"
        "\"utc_offset_minutes\":%d,\"temperature_unit\":\"%s\","
        "\"audio_playback_volume\":%u,\"alarm_volume\":%u,\"alarm_enabled\":%s,"
        "\"alarm_hour\":%u,\"alarm_minute\":%u,\"alarm_weekdays\":%u,"
        "\"daily_pages_mask\":%u,\"daily_pages_order\":[%u,%u,%u,%u,%u,%u,%u,%u],"
        "\"music_mode\":\"%s\",\"timer_minutes\":%u}}\n",
        APP_SETTINGS_BACKUP_FORMAT, APP_SETTINGS_BACKUP_VERSION,
        settings->utc_offset_minutes, settings->temperature_unit == APP_TEMPERATURE_UNIT_CELSIUS ? "c" : "f",
        settings->audio_playback_volume, settings->alarm_volume, settings->alarm_enabled ? "true" : "false",
        settings->alarm_hour, settings->alarm_minute, settings->alarm_weekdays, settings->daily_pages_mask,
        settings->daily_pages_order[0], settings->daily_pages_order[1], settings->daily_pages_order[2], settings->daily_pages_order[3],
        settings->daily_pages_order[4], settings->daily_pages_order[5], settings->daily_pages_order[6], settings->daily_pages_order[7],
        music, settings->timer_minutes);
    if (written < 0 || (size_t)written >= capacity || (size_t)written > APP_SETTINGS_BACKUP_MAX_LENGTH) {
        buffer[0] = '\0';
        return false;
    }
    *length = (size_t)written;
    return true;
}
