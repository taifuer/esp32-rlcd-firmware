#include "network_credentials.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const char SETUP_PASSWORD_ALPHABET[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

_Static_assert(sizeof(SETUP_PASSWORD_ALPHABET) - 1U == 32U,
               "setup password alphabet must contain exactly 32 characters");

static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0U;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool printable_bytes(const char *text, size_t length, bool ascii_only)
{
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t byte = (uint8_t)text[index];
        if (byte < 0x20U || byte == 0x7fU || (ascii_only && byte > 0x7eU)) {
            return false;
        }
    }
    return true;
}

bool network_setup_ssid_build(const char *base_name, bool append_device_id,
                              const uint8_t mac[6], char *ssid,
                              size_t ssid_capacity)
{
    if (base_name == NULL || ssid == NULL || ssid_capacity == 0U ||
        (append_device_id && mac == NULL)) {
        return false;
    }

    const size_t base_length = bounded_length(base_name, NETWORK_SSID_MAX_LENGTH + 1U);
    const size_t suffix_length = append_device_id
                                     ? 1U + NETWORK_SETUP_DEVICE_ID_LENGTH
                                     : 0U;
    if (base_length == 0U || base_length > NETWORK_SSID_MAX_LENGTH ||
        base_length + suffix_length > NETWORK_SSID_MAX_LENGTH ||
        !printable_bytes(base_name, base_length, true) ||
        ssid_capacity <= base_length + suffix_length) {
        return false;
    }

    int written;
    if (append_device_id) {
        written = snprintf(ssid, ssid_capacity, "%s-%02X%02X%02X", base_name,
                           mac[3], mac[4], mac[5]);
    } else {
        written = snprintf(ssid, ssid_capacity, "%s", base_name);
    }
    return written > 0 && (size_t)written == base_length + suffix_length;
}

bool network_setup_password_is_valid(const char *password)
{
    if (password == NULL) {
        return false;
    }
    const size_t length = bounded_length(password, NETWORK_PASSWORD_MAX_LENGTH + 1U);
    return length <= NETWORK_PASSWORD_MAX_LENGTH &&
           (length == 0U || length >= 8U) && printable_bytes(password, length, true);
}

bool network_credentials_are_valid(const network_credentials_t *credentials)
{
    if (credentials == NULL) {
        return false;
    }

    const size_t ssid_length =
        bounded_length(credentials->ssid, NETWORK_SSID_MAX_LENGTH + 1U);
    if (ssid_length == 0U || ssid_length > NETWORK_SSID_MAX_LENGTH ||
        !printable_bytes(credentials->ssid, ssid_length, false)) {
        return false;
    }
    if (!network_setup_password_is_valid(credentials->password)) {
        return false;
    }
    return true;
}

bool network_setup_password_from_entropy(uint64_t entropy, char *password,
                                         size_t password_capacity)
{
    if (password == NULL || password_capacity <= NETWORK_SETUP_PASSWORD_LENGTH) {
        return false;
    }

    for (size_t index = 0U; index < NETWORK_SETUP_PASSWORD_LENGTH; ++index) {
        password[index] = SETUP_PASSWORD_ALPHABET[entropy & 0x1fU];
        entropy >>= 5U;
    }
    password[NETWORK_SETUP_PASSWORD_LENGTH] = '\0';
    return true;
}

typedef struct {
    char *text;
    size_t capacity;
    size_t length;
} qr_payload_writer_t;

static bool qr_payload_append_char(qr_payload_writer_t *writer, char value)
{
    if (writer->length + 1U >= writer->capacity) {
        return false;
    }
    writer->text[writer->length++] = value;
    writer->text[writer->length] = '\0';
    return true;
}

static bool qr_payload_append_text(qr_payload_writer_t *writer, const char *text)
{
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (!qr_payload_append_char(writer, text[index])) {
            return false;
        }
    }
    return true;
}

static bool qr_payload_append_escaped(qr_payload_writer_t *writer, const char *text)
{
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        const char value = text[index];
        if (value == '\\' || value == ';' || value == ',' || value == '"' ||
            value == ':') {
            if (!qr_payload_append_char(writer, '\\')) {
                return false;
            }
        }
        if (!qr_payload_append_char(writer, value)) {
            return false;
        }
    }
    return true;
}

bool network_setup_wifi_qr_payload(const char *ssid, const char *password,
                                   char *payload, size_t payload_capacity)
{
    if (ssid == NULL || password == NULL || payload == NULL || payload_capacity == 0U) {
        return false;
    }
    payload[0] = '\0';

    const size_t ssid_length = bounded_length(ssid, NETWORK_SSID_MAX_LENGTH + 1U);
    const size_t password_length =
        bounded_length(password, NETWORK_PASSWORD_MAX_LENGTH + 1U);
    if (ssid_length == 0U || ssid_length > NETWORK_SSID_MAX_LENGTH ||
        !printable_bytes(ssid, ssid_length, true) || password_length < 8U ||
        password_length > NETWORK_PASSWORD_MAX_LENGTH ||
        !printable_bytes(password, password_length, true)) {
        return false;
    }

    qr_payload_writer_t writer = {
        .text = payload,
        .capacity = payload_capacity,
        .length = 0U,
    };
    /*
     * Keep values unquoted. Quotes in the ZXing Wi-Fi syntax represent
     * literal value bytes for some Android vendor parsers, so wrapping every
     * ASCII value can make the phone look for an SSID that includes quotes.
     * Reserved value bytes are escaped individually by the writer above.
     */
    const bool valid =
        qr_payload_append_text(&writer, "WIFI:T:WPA;S:") &&
        qr_payload_append_escaped(&writer, ssid) &&
        qr_payload_append_text(&writer, ";P:") &&
        qr_payload_append_escaped(&writer, password) &&
        qr_payload_append_text(&writer, ";;");
    if (!valid) {
        payload[0] = '\0';
    }
    return valid;
}

static int hex_value(char value)
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

typedef enum {
    DECODE_COMPONENT_OK = 0,
    DECODE_COMPONENT_INVALID,
    DECODE_COMPONENT_TOO_LONG,
} decode_component_result_t;

static decode_component_result_t decode_component(
    const char *encoded, size_t encoded_length, char *decoded, size_t decoded_capacity)
{
    size_t output_length = 0U;
    for (size_t index = 0U; index < encoded_length; ++index) {
        uint8_t byte = (uint8_t)encoded[index];
        if (byte == '+') {
            byte = ' ';
        } else if (byte == '%') {
            if (index + 2U >= encoded_length) {
                return DECODE_COMPONENT_INVALID;
            }
            const int high = hex_value(encoded[index + 1U]);
            const int low = hex_value(encoded[index + 2U]);
            if (high < 0 || low < 0) {
                return DECODE_COMPONENT_INVALID;
            }
            byte = (uint8_t)((uint8_t)high << 4U) | (uint8_t)low;
            index += 2U;
        }
        if (byte == 0U) {
            return DECODE_COMPONENT_INVALID;
        }
        if (output_length + 1U >= decoded_capacity) {
            return DECODE_COMPONENT_TOO_LONG;
        }
        decoded[output_length++] = (char)byte;
    }
    decoded[output_length] = '\0';
    return DECODE_COMPONENT_OK;
}

network_credentials_result_t network_credentials_parse_form(
    const char *form, size_t form_length, network_credentials_t *credentials)
{
    if (form == NULL || credentials == NULL || form_length == 0U) {
        return NETWORK_CREDENTIALS_INVALID_FORM;
    }

    memset(credentials, 0, sizeof(*credentials));
    bool ssid_seen = false;
    bool password_seen = false;
    size_t position = 0U;

    while (position < form_length) {
        size_t field_end = position;
        while (field_end < form_length && form[field_end] != '&') {
            ++field_end;
        }
        size_t equals = position;
        while (equals < field_end && form[equals] != '=') {
            ++equals;
        }
        if (equals == field_end) {
            return NETWORK_CREDENTIALS_INVALID_FORM;
        }

        char key[16] = {0};
        if (decode_component(&form[position], equals - position, key, sizeof(key)) !=
            DECODE_COMPONENT_OK) {
            return NETWORK_CREDENTIALS_INVALID_ENCODING;
        }
        const char *encoded_value = &form[equals + 1U];
        const size_t encoded_value_length = field_end - equals - 1U;

        if (strcmp(key, "ssid") == 0) {
            if (ssid_seen) {
                return NETWORK_CREDENTIALS_DUPLICATE_FIELD;
            }
            ssid_seen = true;
            const decode_component_result_t decode_result =
                decode_component(encoded_value, encoded_value_length,
                                 credentials->ssid, sizeof(credentials->ssid));
            if (decode_result == DECODE_COMPONENT_TOO_LONG) {
                return NETWORK_CREDENTIALS_INVALID_SSID;
            }
            if (decode_result != DECODE_COMPONENT_OK) {
                return NETWORK_CREDENTIALS_INVALID_ENCODING;
            }
        } else if (strcmp(key, "password") == 0) {
            if (password_seen) {
                return NETWORK_CREDENTIALS_DUPLICATE_FIELD;
            }
            password_seen = true;
            const decode_component_result_t decode_result =
                decode_component(encoded_value, encoded_value_length,
                                 credentials->password, sizeof(credentials->password));
            if (decode_result == DECODE_COMPONENT_TOO_LONG) {
                return NETWORK_CREDENTIALS_INVALID_PASSWORD;
            }
            if (decode_result != DECODE_COMPONENT_OK) {
                return NETWORK_CREDENTIALS_INVALID_ENCODING;
            }
        }

        position = field_end + 1U;
    }

    if (!ssid_seen || !password_seen) {
        return NETWORK_CREDENTIALS_MISSING_FIELD;
    }

    const size_t ssid_length =
        bounded_length(credentials->ssid, NETWORK_SSID_MAX_LENGTH + 1U);
    if (ssid_length == 0U || ssid_length > NETWORK_SSID_MAX_LENGTH ||
        !printable_bytes(credentials->ssid, ssid_length, false)) {
        return NETWORK_CREDENTIALS_INVALID_SSID;
    }
    const size_t password_length =
        bounded_length(credentials->password, NETWORK_PASSWORD_MAX_LENGTH + 1U);
    if (password_length > NETWORK_PASSWORD_MAX_LENGTH ||
        (password_length > 0U && password_length < 8U) ||
        !printable_bytes(credentials->password, password_length, true)) {
        return NETWORK_CREDENTIALS_INVALID_PASSWORD;
    }
    return NETWORK_CREDENTIALS_OK;
}

const char *network_credentials_result_name(network_credentials_result_t result)
{
    switch (result) {
    case NETWORK_CREDENTIALS_OK:
        return "ok";
    case NETWORK_CREDENTIALS_INVALID_FORM:
        return "invalid form";
    case NETWORK_CREDENTIALS_INVALID_ENCODING:
        return "invalid form encoding";
    case NETWORK_CREDENTIALS_MISSING_FIELD:
        return "missing field";
    case NETWORK_CREDENTIALS_DUPLICATE_FIELD:
        return "duplicate field";
    case NETWORK_CREDENTIALS_INVALID_SSID:
        return "SSID must contain 1 to 32 printable bytes";
    case NETWORK_CREDENTIALS_INVALID_PASSWORD:
        return "password must be empty or contain 8 to 63 printable ASCII characters";
    default:
        return "unknown error";
    }
}
