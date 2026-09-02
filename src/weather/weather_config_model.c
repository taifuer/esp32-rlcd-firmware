#include "weather_config_model.h"

#include <limits.h>
#include <string.h>

static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0U;
    if (text == NULL) {
        return limit;
    }
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool copy_bounded_string(char *destination, size_t capacity,
                                const char *source)
{
    if (destination == NULL || capacity == 0U || source == NULL) {
        return false;
    }
    const size_t length = bounded_length(source, capacity);
    if (length >= capacity) {
        return false;
    }
    memcpy(destination, source, length + 1U);
    return true;
}

static uint8_t ascii_lower(uint8_t value)
{
    return value >= (uint8_t)'A' && value <= (uint8_t)'Z'
               ? (uint8_t)(value + ((uint8_t)'a' - (uint8_t)'A'))
               : value;
}

static bool copy_lower_ascii(char *destination, size_t capacity,
                             const char *source)
{
    if (destination == NULL || capacity == 0U || source == NULL) {
        return false;
    }
    const size_t length = bounded_length(source, capacity);
    if (length >= capacity) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        destination[index] = (char)ascii_lower((uint8_t)source[index]);
    }
    destination[length] = '\0';
    return true;
}

static bool ascii_equal_case_insensitive(const char *left,
                                         const char *right,
                                         size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        if (ascii_lower((uint8_t)left[index]) !=
            ascii_lower((uint8_t)right[index])) {
            return false;
        }
    }
    return true;
}

static bool visible_ascii_string(const char *text, size_t max_length,
                                 bool allow_empty)
{
    if (text == NULL) {
        return false;
    }
    const size_t length = bounded_length(text, max_length + 1U);
    if (length > max_length || (!allow_empty && length == 0U)) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t value = (uint8_t)text[index];
        if (value < 0x21U || value > 0x7eU) {
            return false;
        }
    }
    return true;
}

static bool utf8_continuation(uint8_t value)
{
    return value >= 0x80U && value <= 0xbfU;
}

static bool optional_utf8_text(const char *text, size_t max_length)
{
    if (text == NULL) {
        return false;
    }
    const size_t length = bounded_length(text, max_length + 1U);
    if (length > max_length) {
        return false;
    }
    if (length > 0U &&
        (text[0] == ' ' || text[length - 1U] == ' ')) {
        return false;
    }

    size_t index = 0U;
    while (index < length) {
        const uint8_t lead = (uint8_t)text[index];
        if (lead <= 0x7fU) {
            if (lead < 0x20U || lead == 0x7fU) {
                return false;
            }
            ++index;
            continue;
        }
        if (lead >= 0xc2U && lead <= 0xdfU) {
            if (index + 1U >= length ||
                !utf8_continuation((uint8_t)text[index + 1U])) {
                return false;
            }
            index += 2U;
            continue;
        }
        if (lead >= 0xe0U && lead <= 0xefU) {
            if (index + 2U >= length) {
                return false;
            }
            const uint8_t second = (uint8_t)text[index + 1U];
            const uint8_t third = (uint8_t)text[index + 2U];
            if (!utf8_continuation(third) ||
                (lead == 0xe0U && (second < 0xa0U || second > 0xbfU)) ||
                (lead == 0xedU && (second < 0x80U || second > 0x9fU)) ||
                ((lead != 0xe0U && lead != 0xedU) &&
                 !utf8_continuation(second))) {
                return false;
            }
            index += 3U;
            continue;
        }
        if (lead >= 0xf0U && lead <= 0xf4U) {
            if (index + 3U >= length) {
                return false;
            }
            const uint8_t second = (uint8_t)text[index + 1U];
            if ((lead == 0xf0U &&
                 (second < 0x90U || second > 0xbfU)) ||
                (lead == 0xf4U &&
                 (second < 0x80U || second > 0x8fU)) ||
                ((lead != 0xf0U && lead != 0xf4U) &&
                 !utf8_continuation(second)) ||
                !utf8_continuation((uint8_t)text[index + 2U]) ||
                !utf8_continuation((uint8_t)text[index + 3U])) {
                return false;
            }
            index += 4U;
            continue;
        }
        return false;
    }
    return true;
}

static bool dns_label_is_valid(const char *label, size_t length)
{
    if (label == NULL || length == 0U || length > 63U ||
        label[0] == '-' || label[length - 1U] == '-') {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t value = ascii_lower((uint8_t)label[index]);
        const bool alpha = value >= (uint8_t)'a' &&
                           value <= (uint8_t)'z';
        const bool digit = value >= (uint8_t)'0' &&
                           value <= (uint8_t)'9';
        if (!alpha && !digit && value != (uint8_t)'-') {
            return false;
        }
    }
    return true;
}

bool weather_api_host_is_allowed(const char *api_host)
{
    static const char suffix[] = ".qweatherapi.com";
    if (api_host == NULL) {
        return false;
    }
    const size_t length = bounded_length(
        api_host, WEATHER_API_HOST_MAX_LENGTH + 1U);
    const size_t suffix_length = sizeof(suffix) - 1U;
    if (length <= suffix_length ||
        length > WEATHER_API_HOST_MAX_LENGTH ||
        !ascii_equal_case_insensitive(
            api_host + length - suffix_length, suffix, suffix_length)) {
        return false;
    }

    size_t label_start = 0U;
    while (label_start < length) {
        size_t label_end = label_start;
        while (label_end < length && api_host[label_end] != '.') {
            ++label_end;
        }
        if (!dns_label_is_valid(api_host + label_start,
                                label_end - label_start)) {
            return false;
        }
        label_start = label_end + 1U;
    }
    return true;
}

void weather_config_clear_sensitive(void *memory, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    while (bytes != NULL && size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

void weather_config_defaults(weather_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->schema_version = WEATHER_CONFIG_SCHEMA_VERSION;
}

void weather_config_reset(weather_config_t *config)
{
    if (config == NULL) {
        return;
    }
    weather_config_clear_sensitive(config, sizeof(*config));
    weather_config_defaults(config);
}

bool weather_config_is_configured(const weather_config_t *config)
{
    return config != NULL &&
           config->schema_version == WEATHER_CONFIG_SCHEMA_VERSION &&
           weather_api_host_is_allowed(config->api_host) &&
           visible_ascii_string(config->api_key,
                                WEATHER_API_KEY_MAX_LENGTH, false) &&
           config->province_id != 0U && config->city_id != 0U &&
           optional_utf8_text(config->district,
                              WEATHER_DISTRICT_MAX_LENGTH);
}

weather_config_result_t weather_config_validate(
    const weather_config_t *config)
{
    if (config == NULL ||
        config->schema_version != WEATHER_CONFIG_SCHEMA_VERSION) {
        return WEATHER_CONFIG_RESULT_INVALID_ARGUMENT;
    }
    if (config->api_host[0] != '\0' &&
        !weather_api_host_is_allowed(config->api_host)) {
        return WEATHER_CONFIG_RESULT_INVALID_API_HOST;
    }
    if (!visible_ascii_string(config->api_key,
                              WEATHER_API_KEY_MAX_LENGTH, true)) {
        return WEATHER_CONFIG_RESULT_INVALID_API_KEY;
    }
    if (config->province_id == 0U && config->city_id != 0U) {
        return WEATHER_CONFIG_RESULT_INVALID_LOCATION;
    }
    if (!optional_utf8_text(config->district,
                            WEATHER_DISTRICT_MAX_LENGTH)) {
        return WEATHER_CONFIG_RESULT_INVALID_DISTRICT;
    }
    if (!config->enabled) {
        return WEATHER_CONFIG_RESULT_OK;
    }
    if (config->api_host[0] == '\0') {
        return WEATHER_CONFIG_RESULT_INVALID_API_HOST;
    }
    if (config->api_key[0] == '\0') {
        return WEATHER_CONFIG_RESULT_API_KEY_REQUIRED;
    }
    if (config->province_id == 0U || config->city_id == 0U) {
        return WEATHER_CONFIG_RESULT_LOCATION_REQUIRED;
    }
    return WEATHER_CONFIG_RESULT_OK;
}

weather_config_result_t weather_config_apply_update(
    const weather_config_t *current,
    const weather_config_update_t *update,
    weather_config_t *candidate)
{
    if (update == NULL || candidate == NULL) {
        return WEATHER_CONFIG_RESULT_INVALID_ARGUMENT;
    }

    weather_config_result_t result = WEATHER_CONFIG_RESULT_OK;
    const size_t submitted_key_length = bounded_length(
        update->api_key, WEATHER_API_KEY_MAX_LENGTH + 1U);
    if (submitted_key_length > WEATHER_API_KEY_MAX_LENGTH ||
        (submitted_key_length > 0U &&
         !visible_ascii_string(update->api_key,
                               WEATHER_API_KEY_MAX_LENGTH, false))) {
        result = WEATHER_CONFIG_RESULT_INVALID_API_KEY;
    } else if (update->api_host[0] != '\0' &&
               !weather_api_host_is_allowed(update->api_host)) {
        result = WEATHER_CONFIG_RESULT_INVALID_API_HOST;
    }

    const char *resolved_key = update->api_key;
    if (result == WEATHER_CONFIG_RESULT_OK &&
        submitted_key_length == 0U) {
        if (current != NULL &&
            visible_ascii_string(current->api_key,
                                 WEATHER_API_KEY_MAX_LENGTH, false)) {
            resolved_key = current->api_key;
        } else {
            resolved_key = "";
        }
    }

    weather_config_t resolved;
    weather_config_defaults(&resolved);
    if (result == WEATHER_CONFIG_RESULT_OK) {
        resolved.enabled = update->enabled;
        resolved.province_id = update->province_id;
        resolved.city_id = update->city_id;
        if (!copy_lower_ascii(resolved.api_host,
                              sizeof(resolved.api_host),
                              update->api_host) ||
            !copy_bounded_string(resolved.api_key,
                                 sizeof(resolved.api_key),
                                 resolved_key)) {
            result = WEATHER_CONFIG_RESULT_INVALID_ARGUMENT;
        }
    }
    if (result == WEATHER_CONFIG_RESULT_OK) {
        result = weather_config_validate(&resolved);
    }

    if (result == WEATHER_CONFIG_RESULT_OK) {
        *candidate = resolved;
    } else if (candidate != current) {
        weather_config_reset(candidate);
    }
    weather_config_clear_sensitive(&resolved, sizeof(resolved));
    return result;
}

typedef enum {
    FORM_DECODE_OK = 0,
    FORM_DECODE_INVALID,
    FORM_DECODE_TOO_LONG,
} form_decode_result_t;

static int hex_value(uint8_t value)
{
    if (value >= (uint8_t)'0' && value <= (uint8_t)'9') {
        return value - (uint8_t)'0';
    }
    if (value >= (uint8_t)'a' && value <= (uint8_t)'f') {
        return value - (uint8_t)'a' + 10;
    }
    if (value >= (uint8_t)'A' && value <= (uint8_t)'F') {
        return value - (uint8_t)'A' + 10;
    }
    return -1;
}

static form_decode_result_t decode_form_component(
    const char *encoded, size_t encoded_length, char *decoded,
    size_t decoded_capacity)
{
    if (encoded == NULL || decoded == NULL || decoded_capacity == 0U) {
        return FORM_DECODE_INVALID;
    }
    size_t output_length = 0U;
    for (size_t index = 0U; index < encoded_length; ++index) {
        uint8_t value = (uint8_t)encoded[index];
        if (value == (uint8_t)'+') {
            value = (uint8_t)' ';
        } else if (value == (uint8_t)'%') {
            if (index + 2U >= encoded_length) {
                return FORM_DECODE_INVALID;
            }
            const int high = hex_value((uint8_t)encoded[index + 1U]);
            const int low = hex_value((uint8_t)encoded[index + 2U]);
            if (high < 0 || low < 0) {
                return FORM_DECODE_INVALID;
            }
            value = (uint8_t)(((uint8_t)high << 4U) | (uint8_t)low);
            index += 2U;
        }
        if (value == 0U) {
            return FORM_DECODE_INVALID;
        }
        if (output_length + 1U >= decoded_capacity) {
            return FORM_DECODE_TOO_LONG;
        }
        decoded[output_length++] = (char)value;
    }
    decoded[output_length] = '\0';
    return FORM_DECODE_OK;
}

static bool parse_u32_decimal(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }
    uint32_t parsed = 0U;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        const uint32_t digit = (uint32_t)(text[index] - '0');
        if (parsed > (UINT32_MAX - digit) / UINT32_C(10)) {
            return false;
        }
        parsed = parsed * UINT32_C(10) + digit;
    }
    *value = parsed;
    return true;
}

weather_config_result_t weather_config_parse_form(
    const char *form, size_t form_length,
    weather_config_update_t *update)
{
    if (form == NULL || update == NULL || form_length == 0U ||
        form_length > WEATHER_CONFIG_FORM_MAX_LENGTH ||
        form[form_length - 1U] == '&') {
        if (update != NULL) {
            weather_config_clear_sensitive(update, sizeof(*update));
        }
        return WEATHER_CONFIG_RESULT_INVALID_FORM;
    }

    enum {
        FIELD_ENABLED = 1U << 0,
        FIELD_API_HOST = 1U << 1,
        FIELD_API_KEY = 1U << 2,
        FIELD_PROVINCE = 1U << 3,
        FIELD_CITY = 1U << 4,
        FIELD_DISTRICT = 1U << 5,
        FIELD_REQUIRED = FIELD_ENABLED | FIELD_API_HOST | FIELD_API_KEY |
                         FIELD_PROVINCE | FIELD_CITY,
    };
    weather_config_update_t parsed = {0};
    unsigned seen = 0U;
    weather_config_result_t result = WEATHER_CONFIG_RESULT_OK;

    size_t position = 0U;
    while (position < form_length && result == WEATHER_CONFIG_RESULT_OK) {
        size_t field_end = position;
        while (field_end < form_length && form[field_end] != '&') {
            ++field_end;
        }
        size_t equals = position;
        while (equals < field_end && form[equals] != '=') {
            ++equals;
        }
        if (field_end == position || equals == position ||
            equals == field_end) {
            result = WEATHER_CONFIG_RESULT_INVALID_FORM;
            break;
        }
        for (size_t index = equals + 1U; index < field_end; ++index) {
            if (form[index] == '=') {
                result = WEATHER_CONFIG_RESULT_INVALID_FORM;
                break;
            }
        }
        if (result != WEATHER_CONFIG_RESULT_OK) {
            break;
        }

        char key[16] = {0};
        const form_decode_result_t key_decode = decode_form_component(
            form + position, equals - position, key, sizeof(key));
        if (key_decode != FORM_DECODE_OK) {
            result = key_decode == FORM_DECODE_INVALID
                         ? WEATHER_CONFIG_RESULT_INVALID_ENCODING
                         : WEATHER_CONFIG_RESULT_UNEXPECTED_FIELD;
            break;
        }

        const char *encoded_value = form + equals + 1U;
        const size_t encoded_value_length = field_end - equals - 1U;
        unsigned field = 0U;
        char *destination = NULL;
        size_t capacity = 0U;
        weather_config_result_t too_long_result =
            WEATHER_CONFIG_RESULT_INVALID_FORM;
        char small_value[16] = {0};

        if (strcmp(key, "enabled") == 0) {
            field = FIELD_ENABLED;
            destination = small_value;
            capacity = sizeof(small_value);
        } else if (strcmp(key, "api_host") == 0) {
            field = FIELD_API_HOST;
            destination = parsed.api_host;
            capacity = sizeof(parsed.api_host);
            too_long_result = WEATHER_CONFIG_RESULT_INVALID_API_HOST;
        } else if (strcmp(key, "api_key") == 0) {
            field = FIELD_API_KEY;
            destination = parsed.api_key;
            capacity = sizeof(parsed.api_key);
            too_long_result = WEATHER_CONFIG_RESULT_INVALID_API_KEY;
        } else if (strcmp(key, "province") == 0) {
            field = FIELD_PROVINCE;
            destination = small_value;
            capacity = sizeof(small_value);
            too_long_result = WEATHER_CONFIG_RESULT_INVALID_LOCATION;
        } else if (strcmp(key, "city") == 0) {
            field = FIELD_CITY;
            destination = small_value;
            capacity = sizeof(small_value);
            too_long_result = WEATHER_CONFIG_RESULT_INVALID_LOCATION;
        } else if (strcmp(key, "district") == 0) {
            field = FIELD_DISTRICT;
            destination = parsed.district;
            capacity = sizeof(parsed.district);
            too_long_result = WEATHER_CONFIG_RESULT_INVALID_DISTRICT;
        } else {
            result = WEATHER_CONFIG_RESULT_UNEXPECTED_FIELD;
        }

        if (result == WEATHER_CONFIG_RESULT_OK && (seen & field) != 0U) {
            result = WEATHER_CONFIG_RESULT_DUPLICATE_FIELD;
        }
        form_decode_result_t decode = FORM_DECODE_OK;
        if (result == WEATHER_CONFIG_RESULT_OK) {
            decode = decode_form_component(encoded_value,
                                           encoded_value_length,
                                           destination, capacity);
            if (decode == FORM_DECODE_INVALID) {
                result = WEATHER_CONFIG_RESULT_INVALID_ENCODING;
            } else if (decode == FORM_DECODE_TOO_LONG) {
                result = too_long_result;
            }
        }
        if (result == WEATHER_CONFIG_RESULT_OK) {
            seen |= field;
            if (field == FIELD_ENABLED) {
                if (strcmp(small_value, "on") == 0) {
                    parsed.enabled = true;
                } else if (strcmp(small_value, "off") == 0) {
                    parsed.enabled = false;
                } else {
                    result = WEATHER_CONFIG_RESULT_INVALID_FORM;
                }
            } else if (field == FIELD_PROVINCE &&
                       !parse_u32_decimal(small_value,
                                          &parsed.province_id)) {
                result = WEATHER_CONFIG_RESULT_INVALID_LOCATION;
            } else if (field == FIELD_CITY &&
                       !parse_u32_decimal(small_value, &parsed.city_id)) {
                result = WEATHER_CONFIG_RESULT_INVALID_LOCATION;
            }
        }
        weather_config_clear_sensitive(small_value, sizeof(small_value));
        position = field_end + 1U;
    }

    if (result == WEATHER_CONFIG_RESULT_OK &&
        (seen & FIELD_REQUIRED) != FIELD_REQUIRED) {
        result = WEATHER_CONFIG_RESULT_MISSING_FIELD;
    }
    if (result == WEATHER_CONFIG_RESULT_OK &&
        parsed.api_host[0] != '\0' &&
        !weather_api_host_is_allowed(parsed.api_host)) {
        result = WEATHER_CONFIG_RESULT_INVALID_API_HOST;
    }
    if (result == WEATHER_CONFIG_RESULT_OK &&
        parsed.api_key[0] != '\0' &&
        !visible_ascii_string(parsed.api_key,
                              WEATHER_API_KEY_MAX_LENGTH, false)) {
        result = WEATHER_CONFIG_RESULT_INVALID_API_KEY;
    }
    if (result == WEATHER_CONFIG_RESULT_OK &&
        !optional_utf8_text(parsed.district,
                            WEATHER_DISTRICT_MAX_LENGTH)) {
        result = WEATHER_CONFIG_RESULT_INVALID_DISTRICT;
    }
    if (result == WEATHER_CONFIG_RESULT_OK &&
        parsed.province_id == 0U && parsed.city_id != 0U) {
        result = WEATHER_CONFIG_RESULT_INVALID_LOCATION;
    }
    if (result == WEATHER_CONFIG_RESULT_OK && parsed.enabled &&
        parsed.api_host[0] == '\0') {
        result = WEATHER_CONFIG_RESULT_INVALID_API_HOST;
    }
    if (result == WEATHER_CONFIG_RESULT_OK && parsed.enabled &&
        (parsed.province_id == 0U || parsed.city_id == 0U)) {
        result = WEATHER_CONFIG_RESULT_LOCATION_REQUIRED;
    }

    if (result == WEATHER_CONFIG_RESULT_OK) {
        /* Accept the old field so a portal loaded before an OTA can still
         * submit, but all newly saved configurations are city-level. */
        weather_config_clear_sensitive(parsed.district,
                                       sizeof(parsed.district));
        *update = parsed;
    } else {
        weather_config_clear_sensitive(update, sizeof(*update));
    }
    weather_config_clear_sensitive(&parsed, sizeof(parsed));
    return result;
}

void weather_config_make_status(
    const weather_config_t *config, uint32_t generation,
    weather_config_status_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->generation = generation;
    if (config == NULL) {
        return;
    }

    status->enabled = config->enabled;
    status->configured = weather_config_is_configured(config);
    status->key_saved = visible_ascii_string(
        config->api_key, WEATHER_API_KEY_MAX_LENGTH, false);
    if (config->api_host[0] != '\0' &&
        weather_api_host_is_allowed(config->api_host)) {
        (void)copy_lower_ascii(status->api_host,
                               sizeof(status->api_host),
                               config->api_host);
    }
    status->province_id = config->province_id;
    status->city_id = config->city_id;
}

const char *weather_config_result_name(weather_config_result_t result)
{
    switch (result) {
    case WEATHER_CONFIG_RESULT_OK:
        return "ok";
    case WEATHER_CONFIG_RESULT_INVALID_ARGUMENT:
        return "invalid argument";
    case WEATHER_CONFIG_RESULT_INVALID_API_HOST:
        return "invalid API host";
    case WEATHER_CONFIG_RESULT_API_KEY_REQUIRED:
        return "API key required";
    case WEATHER_CONFIG_RESULT_INVALID_API_KEY:
        return "invalid API key";
    case WEATHER_CONFIG_RESULT_LOCATION_REQUIRED:
        return "location required";
    case WEATHER_CONFIG_RESULT_INVALID_LOCATION:
        return "invalid location";
    case WEATHER_CONFIG_RESULT_INVALID_DISTRICT:
        return "invalid district";
    case WEATHER_CONFIG_RESULT_INVALID_FORM:
        return "invalid form";
    case WEATHER_CONFIG_RESULT_INVALID_ENCODING:
        return "invalid form encoding";
    case WEATHER_CONFIG_RESULT_MISSING_FIELD:
        return "missing field";
    case WEATHER_CONFIG_RESULT_DUPLICATE_FIELD:
        return "duplicate field";
    case WEATHER_CONFIG_RESULT_UNEXPECTED_FIELD:
        return "unexpected field";
    default:
        return "unknown error";
    }
}
