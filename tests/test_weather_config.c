#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "weather_config_model.h"

_Static_assert(
    WEATHER_CONFIG_FORM_MAX_LENGTH >=
        128U + 3U * WEATHER_API_HOST_MAX_LENGTH +
            3U * WEATHER_API_KEY_MAX_LENGTH +
            3U * WEATHER_DISTRICT_MAX_LENGTH,
    "weather form capacity must cover maximum encoded valid fields");

static weather_config_update_t make_update(
    bool enabled, const char *api_host, const char *api_key,
    uint32_t province_id, uint32_t city_id, const char *district)
{
    weather_config_update_t update = {
        .enabled = enabled,
        .province_id = province_id,
        .city_id = city_id,
    };
    if (api_host != NULL) {
        const int written = snprintf(update.api_host,
                                     sizeof(update.api_host), "%s",
                                     api_host);
        assert(written >= 0 &&
               (size_t)written < sizeof(update.api_host));
    }
    if (api_key != NULL) {
        const int written = snprintf(update.api_key,
                                     sizeof(update.api_key), "%s",
                                     api_key);
        assert(written >= 0 &&
               (size_t)written < sizeof(update.api_key));
    }
    if (district != NULL) {
        const int written = snprintf(update.district,
                                     sizeof(update.district), "%s",
                                     district);
        assert(written >= 0 &&
               (size_t)written < sizeof(update.district));
    }
    return update;
}

static weather_config_t make_config(const char *key)
{
    weather_config_t current;
    weather_config_defaults(&current);
    const weather_config_update_t update = make_update(
        true, "DEMO123.QWEATHERAPI.COM", key,
        UINT32_C(310000), UINT32_C(310100), "浦东新区");
    weather_config_t config;
    assert(weather_config_apply_update(&current, &update, &config) ==
           WEATHER_CONFIG_RESULT_OK);
    weather_config_reset(&current);
    return config;
}

static bool memory_contains(const void *memory, size_t memory_size,
                            const char *needle)
{
    const uint8_t *bytes = memory;
    const size_t needle_size = strlen(needle);
    if (needle_size == 0U || needle_size > memory_size) {
        return false;
    }
    for (size_t index = 0U; index + needle_size <= memory_size; ++index) {
        if (memcmp(bytes + index, needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

static void test_defaults_and_secret_free_status(void)
{
    weather_config_t config;
    weather_config_defaults(&config);
    assert(config.schema_version == WEATHER_CONFIG_SCHEMA_VERSION);
    assert(!config.enabled);
    assert(config.api_host[0] == '\0');
    assert(config.api_key[0] == '\0');
    assert(config.province_id == 0U && config.city_id == 0U);
    assert(config.district[0] == '\0');
    assert(weather_config_validate(&config) == WEATHER_CONFIG_RESULT_OK);
    assert(!weather_config_is_configured(&config));

    weather_config_status_t status;
    weather_config_make_status(&config, 7U, &status);
    assert(!status.enabled && !status.configured && !status.key_saved);
    assert(status.generation == 7U);

    config = make_config("key-must-never-appear-in-status");
    weather_config_make_status(&config, 12U, &status);
    assert(status.enabled && status.configured && status.key_saved);
    assert(status.generation == 12U);
    assert(strcmp(status.api_host, "demo123.qweatherapi.com") == 0);
    assert(status.province_id == UINT32_C(310000));
    assert(status.city_id == UINT32_C(310100));
    assert(status.district[0] == '\0');
    assert(!memory_contains(&status, sizeof(status),
                            "key-must-never-appear-in-status"));
    weather_config_reset(&config);
}

static void test_api_host_policy(void)
{
    static const char *const valid[] = {
        "abc.qweatherapi.com",
        "ABC.QWEATHERAPI.COM",
        "project-7.cn.qweatherapi.com",
        "1.a-b.qweatherapi.com",
    };
    for (size_t index = 0U; index < sizeof(valid) / sizeof(valid[0]);
         ++index) {
        assert(weather_api_host_is_allowed(valid[index]));
    }

    static const char *const invalid[] = {
        "",
        "qweatherapi.com",
        ".qweatherapi.com",
        "https://abc.qweatherapi.com",
        "abc.qweatherapi.com/path",
        "abc.qweatherapi.com:443",
        "key@abc.qweatherapi.com",
        "abc.qweatherapi.com.evil.invalid",
        "evilqweatherapi.com",
        "127.0.0.1",
        "[::1]",
        "-bad.qweatherapi.com",
        "bad-.qweatherapi.com",
        "bad_name.qweatherapi.com",
        "two..labels.qweatherapi.com",
        "bad\n.qweatherapi.com",
    };
    for (size_t index = 0U;
         index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        assert(!weather_api_host_is_allowed(invalid[index]));
    }
    assert(!weather_api_host_is_allowed(NULL));

    char long_label[WEATHER_API_HOST_MAX_LENGTH + 1U];
    memset(long_label, 'a', 64U);
    strcpy(long_label + 64U, ".qweatherapi.com");
    assert(!weather_api_host_is_allowed(long_label));
}

static void test_update_preserves_key_and_allows_disabled_state(void)
{
    weather_config_t current;
    weather_config_defaults(&current);
    weather_config_t candidate;

    weather_config_update_t update = make_update(
        false, "", "", 0U, 0U, "");
    assert(weather_config_apply_update(&current, &update, &candidate) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(!candidate.enabled && !weather_config_is_configured(&candidate));

    update = make_update(true, "demo.qweatherapi.com", "",
                         31U, 3101U, "浦东");
    assert(weather_config_apply_update(&current, &update, &candidate) ==
           WEATHER_CONFIG_RESULT_API_KEY_REQUIRED);

    update = make_update(true, "DEMO.QWEATHERAPI.COM", "api-key-1",
                         31U, 3101U, "浦东");
    assert(weather_config_apply_update(&current, &update, &candidate) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(candidate.enabled);
    assert(strcmp(candidate.api_host, "demo.qweatherapi.com") == 0);
    assert(strcmp(candidate.api_key, "api-key-1") == 0);
    assert(candidate.district[0] == '\0');

    weather_config_update_t replacement = make_update(
        false, "OTHER.QWEATHERAPI.COM", "", 32U, 3201U, "姑苏区");
    weather_config_t replaced;
    assert(weather_config_apply_update(&candidate, &replacement,
                                       &replaced) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(!replaced.enabled);
    assert(strcmp(replaced.api_key, "api-key-1") == 0);
    assert(strcmp(replaced.api_host, "other.qweatherapi.com") == 0);
    assert(replaced.province_id == 32U && replaced.city_id == 3201U);
    assert(replaced.district[0] == '\0');

    replacement = make_update(false, "", "", 32U, 0U, "");
    assert(weather_config_apply_update(&replaced, &replacement,
                                       &current) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(!weather_config_is_configured(&current));
    assert(strcmp(current.api_key, "api-key-1") == 0);

    replacement = make_update(false, "", "", 0U, 3101U, "");
    assert(weather_config_apply_update(&replaced, &replacement,
                                       &current) ==
           WEATHER_CONFIG_RESULT_INVALID_LOCATION);
    replacement = make_update(true, "demo.qweatherapi.com", "key", 31U,
                              0U, "");
    assert(weather_config_apply_update(&replaced, &replacement,
                                       &current) ==
           WEATHER_CONFIG_RESULT_LOCATION_REQUIRED);
    replacement = make_update(false, "example.com", "key", 0U, 0U,
                              "");
    assert(weather_config_apply_update(&replaced, &replacement,
                                       &current) ==
           WEATHER_CONFIG_RESULT_INVALID_API_HOST);
    replacement = make_update(false, "", "key with spaces", 0U, 0U,
                              "");
    assert(weather_config_apply_update(&replaced, &replacement,
                                       &current) ==
           WEATHER_CONFIG_RESULT_INVALID_API_KEY);

    weather_config_reset(&current);
    weather_config_reset(&candidate);
    weather_config_reset(&replaced);
}

static void test_utf8_and_field_bounds(void)
{
    weather_config_t current = make_config("original-secret");
    weather_config_t candidate;
    weather_config_update_t update = make_update(
        false, "demo.qweatherapi.com", "new-key", 31U, 3101U, "");

    memset(update.api_host, 'h', sizeof(update.api_host));
    assert(weather_config_apply_update(&current, &update, &candidate) ==
           WEATHER_CONFIG_RESULT_INVALID_API_HOST);

    update = make_update(false, "demo.qweatherapi.com", "new-key",
                         31U, 3101U, "");
    memset(update.api_key, 'K', sizeof(update.api_key));
    assert(weather_config_apply_update(&current, &update, &candidate) ==
           WEATHER_CONFIG_RESULT_INVALID_API_KEY);

    update = make_update(false, "demo.qweatherapi.com", "new-key",
                         31U, 3101U, "");
    memset(update.district, 'd', sizeof(update.district));
    assert(weather_config_apply_update(&current, &update, &candidate) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(candidate.district[0] == '\0');

    weather_config_reset(&current);
    assert(!memory_contains(&current, sizeof(current), "original-secret"));
    weather_config_reset(&candidate);
}

static void test_portal_form_parser(void)
{
    static const char valid_form[] =
        "province=310000&city=310100&district=%E6%B5%A6%E4%B8%9C&"
        "enabled=on&api_key=dev%2Bkey%3D1&"
        "api_host=DEMO.QWEATHERAPI.COM";
    weather_config_update_t update;
    assert(weather_config_parse_form(valid_form, strlen(valid_form),
                                     &update) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(update.enabled);
    assert(strcmp(update.api_host, "DEMO.QWEATHERAPI.COM") == 0);
    assert(strcmp(update.api_key, "dev+key=1") == 0);
    assert(update.province_id == UINT32_C(310000));
    assert(update.city_id == UINT32_C(310100));
    assert(update.district[0] == '\0');

    static const char canonical_form[] =
        "enabled=on&api_host=demo.qweatherapi.com&api_key=key&"
        "province=310000&city=310100";
    assert(weather_config_parse_form(canonical_form,
                                     strlen(canonical_form), &update) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(update.district[0] == '\0');

    weather_config_t current = make_config("preserved-key");
    static const char preserve_form[] =
        "enabled=off&api_host=demo.qweatherapi.com&api_key=&"
        "province=310000&city=310100&district=";
    assert(weather_config_parse_form(preserve_form,
                                     strlen(preserve_form), &update) ==
           WEATHER_CONFIG_RESULT_OK);
    weather_config_t candidate;
    assert(weather_config_apply_update(&current, &update, &candidate) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(strcmp(candidate.api_key, "preserved-key") == 0);

    static const char disabled_empty_form[] =
        "enabled=off&api_host=&api_key=&province=0&city=0&district=";
    assert(weather_config_parse_form(disabled_empty_form,
                                     strlen(disabled_empty_form),
                                     &update) ==
           WEATHER_CONFIG_RESULT_OK);

    static const char duplicate_form[] =
        "enabled=off&enabled=on&api_host=&api_key=&province=0&city=0&"
        "district=";
    assert(weather_config_parse_form(duplicate_form,
                                     strlen(duplicate_form), &update) ==
           WEATHER_CONFIG_RESULT_DUPLICATE_FIELD);

    static const char missing_form[] =
        "enabled=off&api_host=&api_key=&province=0&district=";
    assert(weather_config_parse_form(missing_form, strlen(missing_form),
                                     &update) ==
           WEATHER_CONFIG_RESULT_MISSING_FIELD);

    static const char unexpected_form[] =
        "enabled=off&api_host=&api_key=&province=0&city=0&district=&"
        "url=https%3A%2F%2Fevil.invalid";
    assert(weather_config_parse_form(unexpected_form,
                                     strlen(unexpected_form), &update) ==
           WEATHER_CONFIG_RESULT_UNEXPECTED_FIELD);

    static const char bad_encoding_form[] =
        "enabled=off&api_host=&api_key=&province=0&city=0&district=%GG";
    assert(weather_config_parse_form(bad_encoding_form,
                                     strlen(bad_encoding_form), &update) ==
           WEATHER_CONFIG_RESULT_INVALID_ENCODING);

    static const char nul_encoding_form[] =
        "enabled=off&api_host=&api_key=key%00hidden&province=0&city=0&"
        "district=";
    assert(weather_config_parse_form(nul_encoding_form,
                                     strlen(nul_encoding_form), &update) ==
           WEATHER_CONFIG_RESULT_INVALID_ENCODING);

    static const char malformed_utf8_form[] =
        "enabled=off&api_host=&api_key=&province=0&city=0&"
        "district=%C0%AF";
    assert(weather_config_parse_form(malformed_utf8_form,
                                     strlen(malformed_utf8_form),
                                     &update) ==
           WEATHER_CONFIG_RESULT_INVALID_DISTRICT);

    static const char second_equals_form[] =
        "enabled=off&api_host=&api_key=raw=value&province=0&city=0&"
        "district=";
    assert(weather_config_parse_form(second_equals_form,
                                     strlen(second_equals_form),
                                     &update) ==
           WEATHER_CONFIG_RESULT_INVALID_FORM);

    static const char bad_host_form[] =
        "enabled=off&api_host=example.com&api_key=&province=0&city=0&"
        "district=";
    assert(weather_config_parse_form(bad_host_form,
                                     strlen(bad_host_form), &update) ==
           WEATHER_CONFIG_RESULT_INVALID_API_HOST);

    static const char bad_enabled_form[] =
        "enabled=yes&api_host=demo.qweatherapi.com&api_key=key&"
        "province=31&city=3101&district=";
    assert(weather_config_parse_form(bad_enabled_form,
                                     strlen(bad_enabled_form), &update) ==
           WEATHER_CONFIG_RESULT_INVALID_FORM);

    static const char overflow_location_form[] =
        "enabled=off&api_host=&api_key=&province=4294967296&city=0&"
        "district=";
    assert(weather_config_parse_form(overflow_location_form,
                                     strlen(overflow_location_form),
                                     &update) ==
           WEATHER_CONFIG_RESULT_INVALID_LOCATION);

    static const char location_required_form[] =
        "enabled=on&api_host=demo.qweatherapi.com&api_key=key&"
        "province=31&city=0&district=";
    assert(weather_config_parse_form(location_required_form,
                                     strlen(location_required_form),
                                     &update) ==
           WEATHER_CONFIG_RESULT_LOCATION_REQUIRED);

    char encoded_key_form[WEATHER_CONFIG_FORM_MAX_LENGTH + 1U] = {0};
    const int prefix = snprintf(
        encoded_key_form, sizeof(encoded_key_form),
        "enabled=on&api_host=demo.qweatherapi.com&api_key=");
    assert(prefix > 0 && (size_t)prefix < sizeof(encoded_key_form));
    size_t encoded_length = (size_t)prefix;
    for (size_t index = 0U; index < WEATHER_API_KEY_MAX_LENGTH; ++index) {
        assert(encoded_length + 3U < sizeof(encoded_key_form));
        memcpy(encoded_key_form + encoded_length, "%3D", 3U);
        encoded_length += 3U;
    }
    static const char suffix[] = "&province=31&city=3101&district=";
    assert(encoded_length + sizeof(suffix) <= sizeof(encoded_key_form));
    memcpy(encoded_key_form + encoded_length, suffix, sizeof(suffix));
    encoded_length += sizeof(suffix) - 1U;
    assert(weather_config_parse_form(encoded_key_form, encoded_length,
                                     &update) ==
           WEATHER_CONFIG_RESULT_OK);
    assert(strlen(update.api_key) == WEATHER_API_KEY_MAX_LENGTH);

    char oversized[WEATHER_CONFIG_FORM_MAX_LENGTH + 1U];
    memset(oversized, 'x', sizeof(oversized));
    memset(&update, 0x5a, sizeof(update));
    assert(weather_config_parse_form(oversized, sizeof(oversized),
                                     &update) ==
           WEATHER_CONFIG_RESULT_INVALID_FORM);
    const uint8_t *cleared = (const uint8_t *)&update;
    for (size_t index = 0U; index < sizeof(update); ++index) {
        assert(cleared[index] == 0U);
    }

    weather_config_reset(&candidate);
    weather_config_reset(&current);
    weather_config_clear_sensitive(&update, sizeof(update));
}

static void test_result_names(void)
{
    assert(strcmp(weather_config_result_name(WEATHER_CONFIG_RESULT_OK),
                  "ok") == 0);
    assert(strcmp(weather_config_result_name(
                      WEATHER_CONFIG_RESULT_API_KEY_REQUIRED),
                  "API key required") == 0);
    assert(strcmp(weather_config_result_name(
                      (weather_config_result_t)999),
                  "unknown error") == 0);
}

int main(void)
{
    test_defaults_and_secret_free_status();
    test_api_host_policy();
    test_update_preserves_key_and_allows_disabled_state();
    test_utf8_and_field_bounds();
    test_portal_form_parser();
    test_result_names();
    puts("weather config tests passed");
    return 0;
}
