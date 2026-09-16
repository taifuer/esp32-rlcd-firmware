#include "market_portal_model.h"

#include <string.h>

static int hex_digit(unsigned char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode(const char *text, size_t length, char *output,
                   size_t capacity)
{
    size_t count = 0U;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char value = (unsigned char)text[index];
        if (value == '%') {
            if (length - index < 3U) return false;
            const int high = hex_digit((unsigned char)text[++index]);
            const int low = hex_digit((unsigned char)text[++index]);
            if (high < 0 || low < 0) return false;
            value = (unsigned char)((unsigned)high * 16U + (unsigned)low);
        } else if (value == '+') {
            value = ' ';
        }
        if (value < 0x21U || value > 0x7eU || count + 1U >= capacity) return false;
        output[count++] = (char)value;
    }
    output[count] = '\0';
    return count > 0U;
}

static bool parse_ids(const char *text, market_config_t *config)
{
    const char *cursor = text;
    while (*cursor != '\0') {
        if (config->count >= MARKET_MAX_ROWS || *cursor < '0' || *cursor > '9') return false;
        const char first = *cursor;
        unsigned id = 0U;
        size_t digits = 0U;
        while (*cursor >= '0' && *cursor <= '9') {
            if (++digits > 2U) return false;
            id = id * 10U + (unsigned)(*cursor++ - '0');
        }
        if ((digits > 1U && first == '0') || id >= MARKET_PRESET_COUNT) return false;
        config->ids[config->count++] = (uint8_t)id;
        if (*cursor == '\0') break;
        if (*cursor++ != ',' || *cursor == '\0') return false;
    }
    return config->count > 0U;
}

bool market_portal_parse_form(const char *body, size_t length,
                             market_config_t *config)
{
    if (body == NULL || config == NULL || length == 0U ||
        length > MARKET_PORTAL_FORM_MAX_LENGTH ||
        memchr(body, '\0', length) != NULL) return false;
    market_config_t parsed = {0};
    bool enabled_seen = false;
    bool ids_seen = false;
    size_t offset = 0U;
    while (offset < length) {
        const char *const field = body + offset;
        const char *const delimiter = memchr(field, '&', length - offset);
        const size_t size = delimiter == NULL ? length - offset : (size_t)(delimiter - field);
        const char *const equals = memchr(field, '=', size);
        if (equals == NULL) return false;
        char key[16] = {0};
        char value[32] = {0};
        const size_t key_length = (size_t)(equals - field);
        if (!decode(field, key_length, key, sizeof(key)) ||
            !decode(equals + 1U, size - key_length - 1U, value, sizeof(value))) return false;
        if (strcmp(key, "enabled") == 0 && !enabled_seen) {
            if (strcmp(value, "on") != 0 && strcmp(value, "off") != 0) return false;
            parsed.enabled = strcmp(value, "on") == 0;
            enabled_seen = true;
        } else if (strcmp(key, "ids") == 0 && !ids_seen) {
            if (!parse_ids(value, &parsed)) return false;
            ids_seen = true;
        } else {
            return false;
        }
        offset += size;
        if (offset < length && ++offset == length) return false;
    }
    if (!enabled_seen || !ids_seen || !market_config_valid(&parsed)) return false;
    *config = parsed;
    return true;
}
