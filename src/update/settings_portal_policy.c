#include "settings_portal_policy.h"

#include <limits.h>
#include <string.h>

static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool settings_portal_token_encode(
    const uint8_t entropy[SETTINGS_PORTAL_TOKEN_BYTES], char *token,
    size_t capacity)
{
    static const char hexadecimal[] = "0123456789abcdef";
    if (entropy == NULL || token == NULL ||
        capacity < SETTINGS_PORTAL_TOKEN_CAPACITY) {
        return false;
    }
    for (size_t index = 0U; index < SETTINGS_PORTAL_TOKEN_BYTES; ++index) {
        token[index * 2U] = hexadecimal[entropy[index] >> 4U];
        token[index * 2U + 1U] = hexadecimal[entropy[index] & 0x0fU];
    }
    token[SETTINGS_PORTAL_TOKEN_LENGTH] = '\0';
    return true;
}

bool settings_portal_token_matches(const char *expected,
                                   const char *provided)
{
    if (expected == NULL || provided == NULL) {
        return false;
    }
    if (bounded_length(expected, SETTINGS_PORTAL_TOKEN_CAPACITY) !=
            SETTINGS_PORTAL_TOKEN_LENGTH ||
        bounded_length(provided, SETTINGS_PORTAL_TOKEN_CAPACITY) !=
            SETTINGS_PORTAL_TOKEN_LENGTH) {
        return false;
    }

    unsigned char difference = 0U;
    for (size_t index = 0U; index < SETTINGS_PORTAL_TOKEN_LENGTH; ++index) {
        difference |= (unsigned char)expected[index] ^
                      (unsigned char)provided[index];
    }
    return difference == 0U;
}

bool settings_portal_write_is_available(bool session_ready,
                                        bool upload_started,
                                        bool mutation_active,
                                        bool restart_requested)
{
    return session_ready && !upload_started && !mutation_active &&
           !restart_requested;
}

settings_portal_timeout_action_t settings_portal_timeout_action(
    bool upload_started, bool mutation_active, bool restart_requested)
{
    if (restart_requested) {
        return SETTINGS_PORTAL_TIMEOUT_RESTART;
    }
    if (upload_started) {
        return SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_UPLOAD;
    }
    if (mutation_active) {
        return SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_MUTATION;
    }
    return SETTINGS_PORTAL_TIMEOUT_EXPIRE;
}

uint32_t settings_portal_deadline_remaining(uint32_t started_at,
                                            uint32_t now,
                                            uint32_t timeout_ticks)
{
    const uint32_t elapsed = now - started_at;
    return elapsed >= timeout_ticks ? 0U : timeout_ticks - elapsed;
}

bool settings_portal_parse_unix_form(const char *body, size_t length,
                                     int64_t *unix_seconds)
{
    static const char prefix[] = "unix=";
    if (body == NULL || unix_seconds == NULL ||
        length <= sizeof(prefix) - 1U ||
        memcmp(body, prefix, sizeof(prefix) - 1U) != 0) {
        return false;
    }

    int64_t value = 0;
    for (size_t index = sizeof(prefix) - 1U; index < length; ++index) {
        if (body[index] < '0' || body[index] > '9') {
            return false;
        }
        const int64_t digit = body[index] - '0';
        if (value > (INT64_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    *unix_seconds = value;
    return true;
}

bool settings_portal_confirmation_matches(const char *body, size_t length,
                                          const char *expected_word)
{
    static const char prefix[] = "confirm=";
    if (body == NULL || expected_word == NULL) {
        return false;
    }
    const size_t word_length = strlen(expected_word);
    return length == sizeof(prefix) - 1U + word_length &&
           memcmp(body, prefix, sizeof(prefix) - 1U) == 0 &&
           memcmp(body + sizeof(prefix) - 1U, expected_word,
                  word_length) == 0;
}
