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

uint32_t settings_portal_clock_remaining(const settings_portal_clock_t *clock,
                                         uint32_t now_ms, bool recovery)
{
    if (clock == NULL) return 0U;
    /* An admitted transaction finishes within its own finite deadline, even
     * if it crossed the idle/absolute admission deadline. */
    if (clock->transaction_active) {
        return settings_portal_deadline_remaining(clock->transaction_ms, now_ms,
                                                   SETTINGS_PORTAL_TRANSACTION_MS);
    }
    if (recovery) return UINT32_MAX;
    const uint32_t idle = settings_portal_deadline_remaining(
        clock->activity_ms, now_ms, SETTINGS_PORTAL_IDLE_MS);
    const uint32_t absolute = settings_portal_deadline_remaining(
        clock->started_ms, now_ms, SETTINGS_PORTAL_MAX_MS);
    return idle < absolute ? idle : absolute;
}

bool settings_portal_lan_post_allowed(const char *uri)
{
    static const char *const allowed[] = {
        "/api/settings", "/api/time", "/api/hotspot", "/api/alarm/preview", "/api/activity",
        "/api/images/select", "/api/images/delete", "/api/images/upload",
        "/api/music/play", "/api/music/stop", "/api/music/delete", "/api/music/upload",
    };
    if (uri == NULL) return false;
    const size_t size = strcspn(uri, "?");
    for (size_t i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        if (strlen(allowed[i]) == size && memcmp(uri, allowed[i], size) == 0) return true;
    }
    return false;
}

bool settings_portal_host_matches(const char *url, const char *host)
{
    if (url == NULL || host == NULL || strncmp(url, "http://", 7U) != 0 || url[7] == '\0') return false;
    const size_t size = strcspn(host, ":");
    return strlen(url + 7U) == size && memcmp(host, url + 7U, size) == 0 &&
        (host[size] == '\0' || strcmp(host + size, ":80") == 0);
}

bool settings_portal_pair_code_matches(const char *expected,
                                       const char *body, size_t length)
{
    if (expected == NULL || strlen(expected) != 8U || body == NULL ||
        length != 13U || memcmp(body, "code=", 5U) != 0) return false;
    unsigned difference = 0U;
    for (size_t i = 0U; i < 8U; ++i) difference |= (unsigned char)expected[i] ^ (unsigned char)body[5U + i];
    return difference == 0U;
}

bool settings_portal_parse_volume_form(const char *body, size_t length,
                                       uint8_t *volume)
{
    if (body == NULL || volume == NULL || length < 8U || length > 10U ||
        memcmp(body, "volume=", 7U) != 0) return false;
    unsigned value = 0U;
    for (size_t i = 7U; i < length; ++i) {
        if (body[i] < '0' || body[i] > '9') return false;
        value = value * 10U + (unsigned)(body[i] - '0');
    }
    if (value > 100U) return false;
    *volume = (uint8_t)value;
    return true;
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

bool settings_portal_json_escape(const char *text, char *escaped,
                                 size_t capacity)
{
    static const char hexadecimal[] = "0123456789abcdef";
    if (text == NULL || escaped == NULL || capacity == 0U) {
        return false;
    }

    size_t output = 0U;
    for (size_t input = 0U; text[input] != '\0'; ++input) {
        const unsigned char value = (unsigned char)text[input];
        if (value == '"' || value == '\\') {
            if (output + 2U >= capacity) {
                escaped[0] = '\0';
                return false;
            }
            escaped[output++] = '\\';
            escaped[output++] = (char)value;
        } else if (value < 0x20U || value == 0x7fU) {
            if (output + 6U >= capacity) {
                escaped[0] = '\0';
                return false;
            }
            escaped[output++] = '\\';
            escaped[output++] = 'u';
            escaped[output++] = '0';
            escaped[output++] = '0';
            escaped[output++] = hexadecimal[value >> 4U];
            escaped[output++] = hexadecimal[value & 0x0fU];
        } else {
            if (output + 1U >= capacity) {
                escaped[0] = '\0';
                return false;
            }
            escaped[output++] = (char)value;
        }
    }
    escaped[output] = '\0';
    return true;
}
