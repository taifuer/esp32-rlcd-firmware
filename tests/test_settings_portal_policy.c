#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "settings_portal_policy.h"

int main(void)
{
    settings_portal_clock_t clock = {0};
    assert(settings_portal_clock_remaining(&clock, 0U, false) == SETTINGS_PORTAL_IDLE_MS);
    assert(settings_portal_clock_remaining(&clock, SETTINGS_PORTAL_IDLE_MS, false) == 0U);
    clock.activity_ms = SETTINGS_PORTAL_MAX_MS - 1U;
    assert(settings_portal_clock_remaining(&clock, SETTINGS_PORTAL_MAX_MS - 1U, false) == 1U);
    assert(settings_portal_clock_remaining(&clock, SETTINGS_PORTAL_MAX_MS, false) == 0U);
    clock.transaction_ms = SETTINGS_PORTAL_MAX_MS - 1U;
    clock.transaction_active = true;
    assert(settings_portal_clock_remaining(&clock, SETTINGS_PORTAL_MAX_MS, false) == SETTINGS_PORTAL_TRANSACTION_MS - 1U);
    assert(settings_portal_clock_remaining(&clock, clock.transaction_ms + SETTINGS_PORTAL_TRANSACTION_MS, true) == 0U);
    clock.transaction_active = false;
    assert(settings_portal_clock_remaining(&clock, SETTINGS_PORTAL_MAX_MS, true) == UINT32_MAX);
    clock.started_ms = UINT32_MAX - 9U;
    clock.activity_ms = clock.started_ms;
    assert(settings_portal_clock_remaining(&clock, 10U, false) == SETTINGS_PORTAL_IDLE_MS - 20U);
    assert(settings_portal_clock_remaining(NULL, 0U, false) == 0U);
    assert(settings_portal_pair_code_matches("ABC12345", "code=ABC12345", 13U));
    assert(settings_portal_host_matches("http://192.168.1.23", "192.168.1.23"));
    assert(settings_portal_host_matches("http://192.168.1.23", "192.168.1.23:80"));
    assert(!settings_portal_host_matches("http://192.168.1.23", "evil.example"));
    assert(!settings_portal_host_matches("http://192.168.1.23", "192.168.1.23:8080"));
    assert(!settings_portal_host_matches("http://192.168.1.23", "192.168.1.23.evil.example"));
    assert(!settings_portal_host_matches("http://192.168.1.23", "192.168.1.23:80:1"));
    assert(!settings_portal_host_matches("http://", ""));
    assert(!settings_portal_pair_code_matches("ABC12345", "code=abc12345", 13U));
    assert(!settings_portal_pair_code_matches("ABC12345", "code=ABC12345&x=1", 17U));
    assert(!settings_portal_pair_code_matches(NULL, "code=ABC12345", 13U));
    const char *allowed[] = {"/api/settings", "/api/time", "/api/hotspot", "/api/alarm/preview", "/api/music/upload?name=a.mp3"};
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) assert(settings_portal_lan_post_allowed(allowed[i]));
    const char *denied[] = {"/api/wifi/change", "/api/conversation/config", "/api/weather/config", "/api/images/starter", "/update", "/api/settings/", "/api/music/upload/../update", "/api/settings%00", NULL};
    for (size_t i = 0; i < sizeof(denied) / sizeof(denied[0]); ++i) assert(!settings_portal_lan_post_allowed(denied[i]));
    uint8_t volume = 42U;
    assert(settings_portal_parse_volume_form("volume=0", 8U, &volume) && volume == 0U);
    assert(settings_portal_parse_volume_form("volume=100", 10U, &volume) && volume == 100U);
    assert(!settings_portal_parse_volume_form("volume=101", 10U, &volume) && volume == 100U);
    assert(!settings_portal_parse_volume_form("volume=-1", 9U, &volume));
    assert(!settings_portal_parse_volume_form("volume=1&x", 10U, &volume));
    assert(!settings_portal_parse_volume_form("volume=", 7U, &volume));
    const uint8_t entropy[SETTINGS_PORTAL_TOKEN_BYTES] = {
        0x00U, 0x12U, 0x34U, 0x56U, 0x78U, 0x9aU, 0xbcU, 0xdeU,
        0xffU, 0xedU, 0xcbU, 0xa9U, 0x87U, 0x65U, 0x43U, 0x21U,
    };
    char token[SETTINGS_PORTAL_TOKEN_CAPACITY] = {0};
    assert(settings_portal_token_encode(entropy, token, sizeof(token)));
    assert(strcmp(token, "00123456789abcdeffedcba987654321") == 0);
    assert(!settings_portal_token_encode(entropy, token,
                                         SETTINGS_PORTAL_TOKEN_LENGTH));
    assert(!settings_portal_token_encode(NULL, token, sizeof(token)));

    assert(settings_portal_token_matches(
        "00123456789abcdeffedcba987654321",
        "00123456789abcdeffedcba987654321"));
    assert(!settings_portal_token_matches(
        "00123456789abcdeffedcba987654321",
        "10123456789abcdeffedcba987654321"));
    assert(!settings_portal_token_matches(
        "00123456789abcdeffedcba987654321",
        "00123456789abcdeffedcba98765432"));
    assert(!settings_portal_token_matches(NULL, token));

    assert(settings_portal_write_is_available(true, false, false, false));
    assert(!settings_portal_write_is_available(false, false, false, false));
    assert(!settings_portal_write_is_available(true, true, false, false));
    assert(!settings_portal_write_is_available(true, false, true, false));
    assert(!settings_portal_write_is_available(true, false, false, true));

    assert(settings_portal_timeout_action(false, false, false) ==
           SETTINGS_PORTAL_TIMEOUT_EXPIRE);
    assert(settings_portal_timeout_action(false, true, false) ==
           SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_MUTATION);
    assert(settings_portal_timeout_action(true, false, false) ==
           SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_UPLOAD);
    assert(settings_portal_timeout_action(true, true, false) ==
           SETTINGS_PORTAL_TIMEOUT_WAIT_FOR_UPLOAD);
    assert(settings_portal_timeout_action(true, true, true) ==
           SETTINGS_PORTAL_TIMEOUT_RESTART);

    assert(settings_portal_deadline_remaining(100U, 100U, 300U) == 300U);
    assert(settings_portal_deadline_remaining(100U, 399U, 300U) == 1U);
    assert(settings_portal_deadline_remaining(100U, 400U, 300U) == 0U);
    assert(settings_portal_deadline_remaining(100U, 450U, 300U) == 0U);
    assert(settings_portal_deadline_remaining(UINT32_MAX - 9U, 5U, 20U) ==
           5U);
    assert(settings_portal_deadline_remaining(UINT32_MAX - 9U, 10U, 20U) ==
           0U);
    assert(settings_portal_deadline_remaining(7U, 7U, 0U) == 0U);

    int64_t unix_seconds = 0;
    assert(settings_portal_parse_unix_form("unix=1704067200", 15U,
                                           &unix_seconds));
    assert(unix_seconds == INT64_C(1704067200));
    assert(settings_portal_parse_unix_form("unix=0", 6U, &unix_seconds));
    assert(unix_seconds == 0);
    assert(!settings_portal_parse_unix_form("unix=-1", 7U,
                                            &unix_seconds));
    assert(!settings_portal_parse_unix_form("time=1704067200", 15U,
                                            &unix_seconds));
    assert(!settings_portal_parse_unix_form(
        "unix=9223372036854775808", 24U, &unix_seconds));

    assert(settings_portal_confirmation_matches(
        "confirm=DEFAULTS", 16U, "DEFAULTS"));
    assert(settings_portal_confirmation_matches(
        "confirm=FORGET", 14U, "FORGET"));
    assert(!settings_portal_confirmation_matches(
        "confirm=forget", 14U, "FORGET"));
    assert(!settings_portal_confirmation_matches(
        "confirm=FORGET&yes=1", 20U, "FORGET"));

    char escaped[64] = {0};
    assert(settings_portal_json_escape("Home Wi-Fi", escaped,
                                       sizeof(escaped)));
    assert(strcmp(escaped, "Home Wi-Fi") == 0);
    assert(settings_portal_json_escape("A\"B\\C", escaped,
                                       sizeof(escaped)));
    assert(strcmp(escaped, "A\\\"B\\\\C") == 0);
    assert(settings_portal_json_escape("家里", escaped, sizeof(escaped)));
    assert(strcmp(escaped, "家里") == 0);
    assert(settings_portal_json_escape("A\nB", escaped, sizeof(escaped)));
    assert(strcmp(escaped, "A\\u000aB") == 0);
    assert(!settings_portal_json_escape("abcdef", escaped, 4U));
    assert(escaped[0] == '\0');
    assert(!settings_portal_json_escape(NULL, escaped, sizeof(escaped)));

    puts("settings portal policy tests passed");
    return 0;
}
