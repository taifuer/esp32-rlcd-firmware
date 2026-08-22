#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "online_update_policy.h"

#define VALID_SHA256 \
    "697cbef186e9925005e27dfbec1fa89c6e358742f0240588c7c9d32b93d68acd"
#define UPPER_SHA256 \
    "697CBEF186E9925005E27DFBEC1FA89C6E358742F0240588C7C9D32B93D68ACD"

static const char valid_manifest_json[] =
    "{"
    "\"schema\":1,"
    "\"channel\":\"stable\","
    "\"project\":\"esp32-rlcd-firmware\","
    "\"hardware\":\"waveshare-esp32-s3-rlcd-4.2\","
    "\"version\":\"0.10.0\","
    "\"published_at\":\"2026-08-22\","
    "\"minimum_ota_version\":\"0.7.0\","
    "\"ota\":{"
    "\"filename\":\"esp32-rlcd-firmware-v0.10.0-ota.bin\","
    "\"url\":\"https://mcu.taifua.com/esp32-rlcd/firmware/releases/v0.10.0/firmware.bin\","
    "\"size\":1359744,"
    "\"sha256\":\"" VALID_SHA256 "\""
    "},"
    "\"factory\":{}"
    "}";

static void expect_comparison(const char *left, const char *right, int expected)
{
    int result = 99;

    assert(online_update_semver_compare(left, right, &result) ==
           ONLINE_UPDATE_ERROR_NONE);
    assert(result == expected);
}

static void test_semver(void)
{
    int result = 0;

    expect_comparison("0.10.0", "0.9.0", 1);
    expect_comparison("1.0.0-dev", "1.0.0-rc.1", -1);
    expect_comparison("1.0.0-rc.2", "1.0.0-rc.10", -1);
    expect_comparison("1.0.0-rc.1", "1.0.0", -1);
    expect_comparison("1.0.0", "1.0.0-dev", 1);
    expect_comparison("1.0.0+build.1", "1.0.0+build.2", 0);
    expect_comparison("2.1.3-rc.1", "2.1.3-rc.1", 0);

    assert(online_update_semver_compare("01.0.0", "1.0.0", &result) ==
           ONLINE_UPDATE_ERROR_MANIFEST_VERSION);
    assert(online_update_semver_compare("1.0", "1.0.0", &result) ==
           ONLINE_UPDATE_ERROR_MANIFEST_VERSION);
    assert(online_update_semver_compare("1.0.0-01", "1.0.0", &result) ==
           ONLINE_UPDATE_ERROR_MANIFEST_VERSION);
    assert(online_update_semver_compare("1.0.0-", "1.0.0", &result) ==
           ONLINE_UPDATE_ERROR_MANIFEST_VERSION);
    assert(online_update_semver_compare("1.0.0+", "1.0.0", &result) ==
           ONLINE_UPDATE_ERROR_MANIFEST_VERSION);
    assert(online_update_semver_compare(
               "4294967296.0.0", "1.0.0", &result) ==
           ONLINE_UPDATE_ERROR_MANIFEST_VERSION);
    assert(online_update_semver_compare("1.0.0", "1.0.0", NULL) ==
           ONLINE_UPDATE_ERROR_INVALID_ARGUMENT);
}

static void test_urls(void)
{
    assert(online_update_url_is_allowed(
        "https://mcu.taifua.com/firmware/v1.bin"));
    assert(online_update_url_is_allowed(
        "https://mcu.taifua.com:443/firmware/v1.bin?source=device"));
    assert(!online_update_url_is_allowed(
        "http://mcu.taifua.com/firmware/v1.bin"));
    assert(!online_update_url_is_allowed(
        "https://mcu.taifua.com.evil.example/firmware.bin"));
    assert(!online_update_url_is_allowed(
        "https://user@mcu.taifua.com/firmware.bin"));
    assert(!online_update_url_is_allowed(
        "https://mcu.taifua.com:444/firmware.bin"));
    assert(!online_update_url_is_allowed(
        "https://mcu.taifua.com\\@evil.example/firmware.bin"));
    assert(!online_update_url_is_allowed(
        "https://mcu.taifua.com/firmware.bin#fragment"));
    assert(!online_update_url_is_allowed("https://mcu.taifua.com"));
}

static online_update_error_t parse_json(
    const char *json,
    online_update_manifest_t *manifest)
{
    return online_update_manifest_parse(json, strlen(json), manifest);
}

static void expect_parse_error(
    const char *json,
    online_update_error_t expected)
{
    online_update_manifest_t manifest;

    memset(&manifest, 0xa5, sizeof(manifest));
    assert(parse_json(json, &manifest) == expected);
    assert(manifest.schema == 0U);
}

static void test_valid_manifest(void)
{
    online_update_manifest_t manifest;
    assert(online_update_manifest_parse(
               valid_manifest_json, sizeof(valid_manifest_json) - 1U,
               &manifest) == ONLINE_UPDATE_ERROR_NONE);
    assert(manifest.schema == 1U);
    assert(strcmp(manifest.channel, "stable") == 0);
    assert(strcmp(manifest.project, ONLINE_UPDATE_EXPECTED_PROJECT) == 0);
    assert(strcmp(manifest.hardware, ONLINE_UPDATE_EXPECTED_HARDWARE) == 0);
    assert(strcmp(manifest.version, "0.10.0") == 0);
    assert(strcmp(manifest.minimum_ota_version, "0.7.0") == 0);
    assert(manifest.ota_size == 1359744U);
    assert(strcmp(manifest.ota_sha256_hex, VALID_SHA256) == 0);

    assert(manifest.ota_sha256[0] == 0x69U);
    assert(manifest.ota_sha256[1] == 0x7cU);
    assert(manifest.ota_sha256[31] == 0xcdU);
    assert(online_update_manifest_evaluate(
               &manifest, "0.9.0", "stable") ==
           ONLINE_UPDATE_ERROR_NONE);
    assert(online_update_manifest_check(
               valid_manifest_json, sizeof(valid_manifest_json) - 1U,
               "0.9.0", "stable", &manifest) ==
           ONLINE_UPDATE_ERROR_NONE);
}

static void test_manifest_bounds_and_json(void)
{
    online_update_manifest_t manifest;
    char oversized[ONLINE_UPDATE_MANIFEST_MAX_JSON_BYTES + 1U];

    memset(oversized, ' ', sizeof(oversized));
    assert(online_update_manifest_parse(NULL, 1U, &manifest) ==
           ONLINE_UPDATE_ERROR_INVALID_ARGUMENT);
    assert(online_update_manifest_parse("{}", 2U, NULL) ==
           ONLINE_UPDATE_ERROR_INVALID_ARGUMENT);
    assert(online_update_manifest_parse("", 0U, &manifest) ==
           ONLINE_UPDATE_ERROR_MANIFEST_EMPTY);
    assert(online_update_manifest_parse(
               oversized, sizeof(oversized), &manifest) ==
           ONLINE_UPDATE_ERROR_MANIFEST_TOO_LARGE);
    expect_parse_error("not-json", ONLINE_UPDATE_ERROR_MANIFEST_JSON);
    expect_parse_error("{} trailing", ONLINE_UPDATE_ERROR_MANIFEST_JSON);
    expect_parse_error("[]", ONLINE_UPDATE_ERROR_MANIFEST_ROOT);
    expect_parse_error("{}", ONLINE_UPDATE_ERROR_MANIFEST_SCHEMA);
    expect_parse_error(
        "{\"schema\":1,\"schema\":1}",
        ONLINE_UPDATE_ERROR_MANIFEST_SCHEMA);
    expect_parse_error(
        "{\"schema\":\"1\"}", ONLINE_UPDATE_ERROR_MANIFEST_SCHEMA);
}

static void test_manifest_field_validation(void)
{
    online_update_manifest_t manifest;
    char json[1024];
    int written;

    expect_parse_error(
        "{\"schema\":1,\"channel\":\"nightly\"}",
        ONLINE_UPDATE_ERROR_MANIFEST_CHANNEL);
    expect_parse_error(
        "{\"schema\":1,\"channel\":\"stable\","
        "\"project\":\"another-project\"}",
        ONLINE_UPDATE_ERROR_MANIFEST_PROJECT);
    expect_parse_error(
        "{\"schema\":1,\"channel\":\"stable\","
        "\"project\":\"esp32-rlcd-firmware\","
        "\"hardware\":\"another-board\"}",
        ONLINE_UPDATE_ERROR_MANIFEST_HARDWARE);

    written = snprintf(
        json, sizeof(json),
        "{\"schema\":1,\"channel\":\"stable\","
        "\"project\":\"esp32-rlcd-firmware\","
        "\"hardware\":\"waveshare-esp32-s3-rlcd-4.2\","
        "\"version\":\"0.10.0\",\"minimum_ota_version\":\"0.7.0\","
        "\"ota\":{\"url\":\"%s\",\"size\":%s,\"sha256\":\"%s\"}}",
        "http://mcu.taifua.com/file.bin", "1", VALID_SHA256);
    assert(written > 0 && (size_t)written < sizeof(json));
    expect_parse_error(json, ONLINE_UPDATE_ERROR_MANIFEST_URL);

    written = snprintf(
        json, sizeof(json),
        "{\"schema\":1,\"channel\":\"stable\","
        "\"project\":\"esp32-rlcd-firmware\","
        "\"hardware\":\"waveshare-esp32-s3-rlcd-4.2\","
        "\"version\":\"0.10.0\",\"minimum_ota_version\":\"0.7.0\","
        "\"ota\":{\"url\":\"https://mcu.taifua.com/file.bin\","
        "\"size\":%s,\"sha256\":\"%s\"}}",
        "0", VALID_SHA256);
    assert(written > 0 && (size_t)written < sizeof(json));
    expect_parse_error(json, ONLINE_UPDATE_ERROR_MANIFEST_SIZE);

    written = snprintf(
        json, sizeof(json),
        "{\"schema\":1,\"channel\":\"stable\","
        "\"project\":\"esp32-rlcd-firmware\","
        "\"hardware\":\"waveshare-esp32-s3-rlcd-4.2\","
        "\"version\":\"0.10.0\",\"minimum_ota_version\":\"0.7.0\","
        "\"ota\":{\"url\":\"https://mcu.taifua.com/file.bin\","
        "\"size\":1.5,\"sha256\":\"%s\"}}",
        VALID_SHA256);
    assert(written > 0 && (size_t)written < sizeof(json));
    expect_parse_error(json, ONLINE_UPDATE_ERROR_MANIFEST_SIZE);

    written = snprintf(
        json, sizeof(json),
        "{\"schema\":1,\"channel\":\"stable\","
        "\"project\":\"esp32-rlcd-firmware\","
        "\"hardware\":\"waveshare-esp32-s3-rlcd-4.2\","
        "\"version\":\"0.10.0\",\"minimum_ota_version\":\"0.7.0\","
        "\"ota\":{\"url\":\"https://mcu.taifua.com/file.bin\","
        "\"size\":3145729,\"sha256\":\"%s\"}}",
        VALID_SHA256);
    assert(written > 0 && (size_t)written < sizeof(json));
    expect_parse_error(json, ONLINE_UPDATE_ERROR_MANIFEST_SIZE);

    written = snprintf(
        json, sizeof(json),
        "{\"schema\":1,\"channel\":\"stable\","
        "\"project\":\"esp32-rlcd-firmware\","
        "\"hardware\":\"waveshare-esp32-s3-rlcd-4.2\","
        "\"version\":\"0.10.0\",\"minimum_ota_version\":\"0.7.0\","
        "\"ota\":{\"url\":\"https://mcu.taifua.com/file.bin\","
        "\"size\":1,\"sha256\":\"%s\"}}",
        UPPER_SHA256);
    assert(written > 0 && (size_t)written < sizeof(json));
    assert(parse_json(json, &manifest) == ONLINE_UPDATE_ERROR_NONE);
    assert(strcmp(manifest.ota_sha256_hex, VALID_SHA256) == 0);

    json[written - 4] = 'g';
    expect_parse_error(json, ONLINE_UPDATE_ERROR_MANIFEST_SHA256);
}

static void test_policy_rejections(void)
{
    online_update_manifest_t manifest;

    assert(parse_json(valid_manifest_json, &manifest) ==
           ONLINE_UPDATE_ERROR_NONE);
    assert(online_update_manifest_evaluate(
               &manifest, "0.10.0", "stable") ==
           ONLINE_UPDATE_ERROR_SAME_VERSION);
    assert(online_update_manifest_evaluate(
               &manifest, "0.11.0", "stable") ==
           ONLINE_UPDATE_ERROR_DOWNGRADE);
    assert(online_update_manifest_evaluate(
               &manifest, "0.6.9", "stable") ==
           ONLINE_UPDATE_ERROR_CURRENT_VERSION_TOO_OLD);
    assert(online_update_manifest_evaluate(
               &manifest, "not-semver", "stable") ==
           ONLINE_UPDATE_ERROR_CURRENT_VERSION);
    assert(online_update_manifest_evaluate(
               &manifest, "0.9.0", "testing") ==
           ONLINE_UPDATE_ERROR_MANIFEST_CHANNEL);

    strcpy(manifest.project, "another-project");
    assert(online_update_manifest_evaluate(
               &manifest, "0.9.0", "stable") ==
           ONLINE_UPDATE_ERROR_MANIFEST_PROJECT);
    strcpy(manifest.project, ONLINE_UPDATE_EXPECTED_PROJECT);
    strcpy(manifest.hardware, "another-board");
    assert(online_update_manifest_evaluate(
               &manifest, "0.9.0", "stable") ==
           ONLINE_UPDATE_ERROR_MANIFEST_HARDWARE);
}

static void test_state_and_error_mapping(void)
{
    assert(!online_update_state_is_busy(ONLINE_UPDATE_STATE_IDLE));
    assert(online_update_state_is_busy(ONLINE_UPDATE_STATE_CHECKING));
    assert(online_update_state_is_busy(ONLINE_UPDATE_STATE_DOWNLOADING));
    assert(online_update_state_is_busy(ONLINE_UPDATE_STATE_VERIFYING));
    assert(online_update_state_can_cancel(ONLINE_UPDATE_STATE_CHECKING));
    assert(!online_update_state_can_cancel(ONLINE_UPDATE_STATE_AVAILABLE));
    assert(online_update_state_can_cancel(
        ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION));
    assert(online_update_state_is_busy(ONLINE_UPDATE_STATE_CONNECTING));
    assert(online_update_state_can_cancel(ONLINE_UPDATE_STATE_CONNECTING));
    assert(!online_update_state_can_cancel(ONLINE_UPDATE_STATE_DOWNLOADING));
    assert(online_update_state_is_terminal(ONLINE_UPDATE_STATE_UP_TO_DATE));
    assert(online_update_state_is_terminal(ONLINE_UPDATE_STATE_SUCCESS));
    assert(online_update_state_is_terminal(ONLINE_UPDATE_STATE_FAILED));
    assert(strcmp(
               online_update_state_name(ONLINE_UPDATE_STATE_AVAILABLE),
               "available") == 0);
    assert(strcmp(
               online_update_state_name((online_update_state_t)99),
               "unknown") == 0);
    assert(strcmp(
               online_update_error_name(ONLINE_UPDATE_ERROR_DOWNGRADE),
               "downgrade") == 0);
    assert(strcmp(
               online_update_error_name((online_update_error_t)99),
               "unknown") == 0);
}

int main(void)
{
    test_semver();
    test_urls();
    test_valid_manifest();
    test_manifest_bounds_and_json();
    test_manifest_field_validation();
    test_policy_rejections();
    test_state_and_error_mapping();

    puts("online update policy tests passed");
    return 0;
}
