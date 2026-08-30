#include "conversation_config_model.h"

#include <stdio.h>
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

static bool visible_ascii_string(const char *text, size_t max_length)
{
    if (text == NULL) {
        return false;
    }
    const size_t length = bounded_length(text, max_length + 1U);
    if (length == 0U || length > max_length) {
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

static uint8_t ascii_lower(uint8_t value)
{
    return value >= (uint8_t)'A' && value <= (uint8_t)'Z'
               ? (uint8_t)(value + ((uint8_t)'a' - (uint8_t)'A'))
               : value;
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

static bool copy_lower_ascii(char *destination, size_t capacity,
                             const char *source)
{
    const size_t length = bounded_length(source, capacity);
    if (destination == NULL || source == NULL || capacity == 0U ||
        length >= capacity) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        destination[index] = (char)ascii_lower((uint8_t)source[index]);
    }
    destination[length] = '\0';
    return true;
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

void conversation_config_clear_sensitive(void *memory, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    while (bytes != NULL && size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

void conversation_config_defaults(conversation_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->schema_version = CONVERSATION_CONFIG_SCHEMA_VERSION;
    config->service = CONVERSATION_SERVICE_ALIYUN_REALTIME;
    config->model = CONVERSATION_DEFAULT_MODEL;
    config->enabled = false;
}

void conversation_config_reset(conversation_config_t *config)
{
    if (config == NULL) {
        return;
    }
    conversation_config_clear_sensitive(config, sizeof(*config));
    conversation_config_defaults(config);
}

bool conversation_service_is_supported(conversation_service_t service)
{
    return service == CONVERSATION_SERVICE_ALIYUN_REALTIME;
}

bool conversation_model_is_supported(conversation_model_t model)
{
    return model == CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME ||
           model == CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH;
}

const char *conversation_model_name(conversation_model_t model)
{
    switch (model) {
    case CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME:
        return "qwen3-omni-flash-realtime";
    case CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH:
        return "qwen-audio-3.0-realtime-flash";
    case CONVERSATION_MODEL_COUNT:
    default:
        return NULL;
    }
}

bool conversation_model_from_name(const char *name,
                                  conversation_model_t *model)
{
    if (name == NULL || model == NULL) {
        return false;
    }
    for (int value = 0; value < (int)CONVERSATION_MODEL_COUNT; ++value) {
        const conversation_model_t candidate =
            (conversation_model_t)value;
        const char *candidate_name = conversation_model_name(candidate);
        if (candidate_name != NULL && strcmp(name, candidate_name) == 0) {
            *model = candidate;
            return true;
        }
    }
    return false;
}

static bool workspace_label_is_valid(const char *label, size_t length)
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

bool conversation_api_host_is_allowed(const char *api_host)
{
    static const char shared_beijing[] = "dashscope.aliyuncs.com";
    static const char shared_singapore[] =
        "dashscope-intl.aliyuncs.com";
    static const char suffix_beijing[] =
        ".cn-beijing.maas.aliyuncs.com";
    static const char suffix_singapore[] =
        ".ap-southeast-1.maas.aliyuncs.com";

    if (api_host == NULL) {
        return false;
    }
    const size_t length = bounded_length(
        api_host, CONVERSATION_API_HOST_MAX_LENGTH + 1U);
    if (length == 0U) {
        return true;
    }
    if (length > CONVERSATION_API_HOST_MAX_LENGTH) {
        return false;
    }
    if ((length == sizeof(shared_beijing) - 1U &&
         ascii_equal_case_insensitive(api_host, shared_beijing, length)) ||
        (length == sizeof(shared_singapore) - 1U &&
         ascii_equal_case_insensitive(api_host, shared_singapore,
                                      length))) {
        return true;
    }

    size_t suffix_length = 0U;
    if (length > sizeof(suffix_beijing) - 1U &&
        ascii_equal_case_insensitive(
            api_host + length - (sizeof(suffix_beijing) - 1U),
            suffix_beijing, sizeof(suffix_beijing) - 1U)) {
        suffix_length = sizeof(suffix_beijing) - 1U;
    } else if (length > sizeof(suffix_singapore) - 1U &&
               ascii_equal_case_insensitive(
                   api_host + length -
                       (sizeof(suffix_singapore) - 1U),
                   suffix_singapore,
                   sizeof(suffix_singapore) - 1U)) {
        suffix_length = sizeof(suffix_singapore) - 1U;
    }
    return suffix_length > 0U &&
           workspace_label_is_valid(api_host, length - suffix_length);
}

bool conversation_config_build_endpoint(const char *api_host,
                                        conversation_model_t model,
                                        char *endpoint, size_t capacity)
{
    const char *name = conversation_model_name(model);
    if (endpoint == NULL || capacity == 0U || name == NULL ||
        !conversation_api_host_is_allowed(api_host)) {
        if (endpoint != NULL && capacity > 0U) {
            endpoint[0] = '\0';
        }
        return false;
    }
    const char *effective_host = api_host[0] != '\0'
                                     ? api_host
                                     : CONVERSATION_DEFAULT_API_HOST;
    const int written = snprintf(endpoint, capacity, "wss://%s%s%s",
                                 effective_host,
                                 CONVERSATION_REALTIME_PATH, name);
    if (written < 0 || (size_t)written >= capacity) {
        endpoint[0] = '\0';
        return false;
    }
    return true;
}

conversation_config_result_t conversation_config_validate(
    const conversation_config_t *config)
{
    if (config == NULL ||
        config->schema_version != CONVERSATION_CONFIG_SCHEMA_VERSION) {
        return CONVERSATION_CONFIG_RESULT_INVALID_ARGUMENT;
    }
    if (!conversation_service_is_supported(config->service)) {
        return CONVERSATION_CONFIG_RESULT_UNSUPPORTED_SERVICE;
    }
    if (!conversation_model_is_supported(config->model)) {
        return CONVERSATION_CONFIG_RESULT_INVALID_MODEL;
    }
    if (!conversation_api_host_is_allowed(config->api_host)) {
        return CONVERSATION_CONFIG_RESULT_INVALID_API_HOST;
    }
    if (config->api_key[0] == '\0') {
        return CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED;
    }
    if (!visible_ascii_string(config->api_key,
                              CONVERSATION_API_KEY_MAX_LENGTH)) {
        return CONVERSATION_CONFIG_RESULT_INVALID_API_KEY;
    }
    return CONVERSATION_CONFIG_RESULT_OK;
}

conversation_config_result_t conversation_config_apply_update(
    const conversation_config_t *current,
    const conversation_config_update_t *update,
    conversation_config_t *candidate)
{
    if (update == NULL || candidate == NULL) {
        return CONVERSATION_CONFIG_RESULT_INVALID_ARGUMENT;
    }

    conversation_config_result_t result = CONVERSATION_CONFIG_RESULT_OK;
    const size_t submitted_key_length = bounded_length(
        update->api_key, CONVERSATION_API_KEY_MAX_LENGTH + 1U);
    if (!conversation_service_is_supported(update->service)) {
        result = CONVERSATION_CONFIG_RESULT_UNSUPPORTED_SERVICE;
    } else if (!conversation_model_is_supported(update->model)) {
        result = CONVERSATION_CONFIG_RESULT_INVALID_MODEL;
    } else if (submitted_key_length > CONVERSATION_API_KEY_MAX_LENGTH ||
               (submitted_key_length > 0U &&
                !visible_ascii_string(update->api_key,
                                      CONVERSATION_API_KEY_MAX_LENGTH))) {
        result = CONVERSATION_CONFIG_RESULT_INVALID_API_KEY;
    }

    const char *resolved_key = update->api_key;
    if (result == CONVERSATION_CONFIG_RESULT_OK &&
        submitted_key_length == 0U) {
        if (current == NULL ||
            !visible_ascii_string(current->api_key,
                                  CONVERSATION_API_KEY_MAX_LENGTH)) {
            result = CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED;
        } else {
            resolved_key = current->api_key;
        }
    }

    conversation_config_t resolved;
    conversation_config_defaults(&resolved);
    if (result == CONVERSATION_CONFIG_RESULT_OK) {
        resolved.service = update->service;
        resolved.model = update->model;
        resolved.enabled = update->enabled;
        if (!conversation_api_host_is_allowed(update->api_host)) {
            result = CONVERSATION_CONFIG_RESULT_INVALID_API_HOST;
        } else if (!copy_bounded_string(resolved.api_key,
                                        sizeof(resolved.api_key),
                                        resolved_key) ||
                   !copy_lower_ascii(resolved.api_host,
                                     sizeof(resolved.api_host),
                                     update->api_host)) {
            result = CONVERSATION_CONFIG_RESULT_INVALID_API_KEY;
        }
    }
    if (result == CONVERSATION_CONFIG_RESULT_OK) {
        result = conversation_config_validate(&resolved);
    }

    if (result == CONVERSATION_CONFIG_RESULT_OK) {
        *candidate = resolved;
    } else if (candidate != current) {
        conversation_config_reset(candidate);
    }
    conversation_config_clear_sensitive(&resolved, sizeof(resolved));
    return result;
}

conversation_config_result_t conversation_config_parse_form(
    const char *form, size_t form_length,
    conversation_config_update_t *update)
{
    if (form == NULL || update == NULL || form_length == 0U ||
        form_length > CONVERSATION_CONFIG_FORM_MAX_LENGTH ||
        form[form_length - 1U] == '&') {
        if (update != NULL) {
            conversation_config_clear_sensitive(update, sizeof(*update));
        }
        return CONVERSATION_CONFIG_RESULT_INVALID_FORM;
    }

    conversation_config_update_t parsed = {
        .service = CONVERSATION_SERVICE_ALIYUN_REALTIME,
        .model = CONVERSATION_DEFAULT_MODEL,
        .enabled = false,
    };
    bool service_seen = false;
    bool model_seen = false;
    bool enabled_seen = false;
    bool api_key_seen = false;
    bool api_host_seen = false;
    conversation_config_result_t result = CONVERSATION_CONFIG_RESULT_OK;

    size_t position = 0U;
    while (position < form_length &&
           result == CONVERSATION_CONFIG_RESULT_OK) {
        size_t field_end = position;
        while (field_end < form_length && form[field_end] != '&') {
            ++field_end;
        }
        size_t equals = position;
        while (equals < field_end && form[equals] != '=') {
            ++equals;
        }
        if (equals == position || equals == field_end) {
            result = CONVERSATION_CONFIG_RESULT_INVALID_FORM;
            break;
        }

        char key[16] = {0};
        char small_value[CONVERSATION_MODEL_NAME_MAX_LENGTH + 1U] = {0};
        const form_decode_result_t key_decode = decode_form_component(
            form + position, equals - position, key, sizeof(key));
        if (key_decode != FORM_DECODE_OK) {
            result = key_decode == FORM_DECODE_INVALID
                         ? CONVERSATION_CONFIG_RESULT_INVALID_ENCODING
                         : CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD;
            break;
        }

        const char *encoded_value = form + equals + 1U;
        const size_t encoded_value_length = field_end - equals - 1U;
        form_decode_result_t decode = FORM_DECODE_OK;
        if (strcmp(key, "service") == 0) {
            if (service_seen) {
                result = CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD;
            } else {
                service_seen = true;
                decode = decode_form_component(
                    encoded_value, encoded_value_length, small_value,
                    sizeof(small_value));
                if (decode == FORM_DECODE_TOO_LONG ||
                    (decode == FORM_DECODE_OK &&
                     strcmp(small_value, "aliyun_realtime") != 0)) {
                    result =
                        CONVERSATION_CONFIG_RESULT_UNSUPPORTED_SERVICE;
                }
            }
        } else if (strcmp(key, "model") == 0) {
            if (model_seen) {
                result = CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD;
            } else {
                model_seen = true;
                decode = decode_form_component(
                    encoded_value, encoded_value_length, small_value,
                    sizeof(small_value));
                if (decode == FORM_DECODE_TOO_LONG ||
                    (decode == FORM_DECODE_OK &&
                     !conversation_model_from_name(small_value,
                                                   &parsed.model))) {
                    result = CONVERSATION_CONFIG_RESULT_INVALID_MODEL;
                }
            }
        } else if (strcmp(key, "enabled") == 0) {
            if (enabled_seen) {
                result = CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD;
            } else {
                enabled_seen = true;
                decode = decode_form_component(
                    encoded_value, encoded_value_length, small_value,
                    sizeof(small_value));
                if (decode == FORM_DECODE_TOO_LONG) {
                    result = CONVERSATION_CONFIG_RESULT_INVALID_FORM;
                } else if (decode == FORM_DECODE_OK &&
                           strcmp(small_value, "on") == 0) {
                    parsed.enabled = true;
                } else if (decode == FORM_DECODE_OK &&
                           strcmp(small_value, "off") == 0) {
                    parsed.enabled = false;
                } else if (decode == FORM_DECODE_OK) {
                    result = CONVERSATION_CONFIG_RESULT_INVALID_FORM;
                }
            }
        } else if (strcmp(key, "api_key") == 0) {
            if (api_key_seen) {
                result = CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD;
            } else {
                api_key_seen = true;
                decode = decode_form_component(
                    encoded_value, encoded_value_length, parsed.api_key,
                    sizeof(parsed.api_key));
                if (decode == FORM_DECODE_TOO_LONG) {
                    result = CONVERSATION_CONFIG_RESULT_INVALID_API_KEY;
                }
            }
        } else if (strcmp(key, "api_host") == 0) {
            if (api_host_seen) {
                result = CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD;
            } else {
                api_host_seen = true;
                decode = decode_form_component(
                    encoded_value, encoded_value_length,
                    parsed.api_host, sizeof(parsed.api_host));
                if (decode == FORM_DECODE_TOO_LONG ||
                    (decode == FORM_DECODE_OK &&
                     !conversation_api_host_is_allowed(parsed.api_host))) {
                    result = CONVERSATION_CONFIG_RESULT_INVALID_API_HOST;
                }
            }
        } else {
            result = CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD;
        }

        if (result == CONVERSATION_CONFIG_RESULT_OK &&
            decode == FORM_DECODE_INVALID) {
            result = CONVERSATION_CONFIG_RESULT_INVALID_ENCODING;
        }
        position = field_end + 1U;
        conversation_config_clear_sensitive(small_value,
                                            sizeof(small_value));
    }

    if (result == CONVERSATION_CONFIG_RESULT_OK &&
        (!service_seen || !model_seen || !enabled_seen || !api_key_seen ||
         !api_host_seen)) {
        result = CONVERSATION_CONFIG_RESULT_MISSING_FIELD;
    }
    if (result == CONVERSATION_CONFIG_RESULT_OK &&
        parsed.api_key[0] != '\0' &&
        !visible_ascii_string(parsed.api_key,
                              CONVERSATION_API_KEY_MAX_LENGTH)) {
        result = CONVERSATION_CONFIG_RESULT_INVALID_API_KEY;
    }

    if (result == CONVERSATION_CONFIG_RESULT_OK) {
        *update = parsed;
    } else {
        conversation_config_clear_sensitive(update, sizeof(*update));
    }
    conversation_config_clear_sensitive(&parsed, sizeof(parsed));
    return result;
}

void conversation_config_make_status(
    const conversation_config_t *config,
    conversation_config_status_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->service = CONVERSATION_SERVICE_ALIYUN_REALTIME;
    status->model = CONVERSATION_DEFAULT_MODEL;
    (void)copy_bounded_string(status->api_host,
                              sizeof(status->api_host),
                              CONVERSATION_DEFAULT_API_HOST);
    status->shared_endpoint = true;
    if (config == NULL) {
        return;
    }
    if (conversation_service_is_supported(config->service)) {
        status->service = config->service;
    }
    if (conversation_model_is_supported(config->model)) {
        status->model = config->model;
    }
    status->enabled = config->enabled;
    status->configured = conversation_config_validate(config) ==
                         CONVERSATION_CONFIG_RESULT_OK;
    const char *effective_host = config->api_host[0] != '\0'
                                     ? config->api_host
                                     : CONVERSATION_DEFAULT_API_HOST;
    if (conversation_api_host_is_allowed(config->api_host)) {
        (void)copy_lower_ascii(status->api_host,
                               sizeof(status->api_host), effective_host);
        status->shared_endpoint =
            strcmp(status->api_host, CONVERSATION_DEFAULT_API_HOST) == 0 ||
            strcmp(status->api_host,
                   "dashscope-intl.aliyuncs.com") == 0;
    }
}

const char *conversation_config_result_name(
    conversation_config_result_t result)
{
    switch (result) {
    case CONVERSATION_CONFIG_RESULT_OK:
        return "ok";
    case CONVERSATION_CONFIG_RESULT_INVALID_ARGUMENT:
        return "invalid argument";
    case CONVERSATION_CONFIG_RESULT_UNSUPPORTED_SERVICE:
        return "unsupported service";
    case CONVERSATION_CONFIG_RESULT_INVALID_MODEL:
        return "invalid model";
    case CONVERSATION_CONFIG_RESULT_INVALID_API_HOST:
        return "invalid API host";
    case CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED:
        return "API key required";
    case CONVERSATION_CONFIG_RESULT_INVALID_API_KEY:
        return "invalid API key";
    case CONVERSATION_CONFIG_RESULT_INVALID_FORM:
        return "invalid form";
    case CONVERSATION_CONFIG_RESULT_INVALID_ENCODING:
        return "invalid form encoding";
    case CONVERSATION_CONFIG_RESULT_MISSING_FIELD:
        return "missing field";
    case CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD:
        return "duplicate field";
    case CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD:
        return "unexpected field";
    default:
        return "unknown error";
    }
}
