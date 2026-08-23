#include "online_update_policy.h"

#include <limits.h>
#include <string.h>

typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    const char *prerelease;
    size_t prerelease_length;
} parsed_semver_t;

static size_t bounded_string_length(const char *value, size_t limit)
{
    size_t length = 0U;

    if (value == NULL) {
        return 0U;
    }
    while (length < limit && value[length] != '\0') {
        ++length;
    }
    return length;
}

static bool parse_uint32_component(
    const char **cursor,
    const char *end,
    uint32_t *value)
{
    const char *start = *cursor;
    uint32_t parsed = 0U;

    if (start >= end || *start < '0' || *start > '9') {
        return false;
    }
    if (*start == '0' && start + 1 < end && start[1] >= '0' &&
        start[1] <= '9') {
        return false;
    }
    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        const uint32_t digit = (uint32_t)(**cursor - '0');
        if (parsed > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
        ++*cursor;
    }
    *value = parsed;
    return true;
}

static bool identifier_character_is_valid(char character)
{
    return (character >= '0' && character <= '9') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') || character == '-';
}

static bool validate_identifiers(
    const char *value,
    size_t length,
    bool reject_numeric_leading_zero)
{
    size_t offset = 0U;

    if (length == 0U) {
        return false;
    }
    while (offset < length) {
        const size_t start = offset;
        bool numeric = true;

        while (offset < length && value[offset] != '.') {
            if (!identifier_character_is_valid(value[offset])) {
                return false;
            }
            numeric &= value[offset] >= '0' && value[offset] <= '9';
            ++offset;
        }
        if (offset == start) {
            return false;
        }
        if (reject_numeric_leading_zero && numeric &&
            offset - start > 1U && value[start] == '0') {
            return false;
        }
        if (offset < length) {
            ++offset;
            if (offset == length) {
                return false;
            }
        }
    }
    return true;
}

static bool parse_semver(const char *version, parsed_semver_t *parsed)
{
    const size_t length = bounded_string_length(
        version, ONLINE_UPDATE_VERSION_MAX_LENGTH + 1U);
    const char *cursor;
    const char *end;
    const char *metadata;

    if (version == NULL || parsed == NULL || length == 0U ||
        length > ONLINE_UPDATE_VERSION_MAX_LENGTH) {
        return false;
    }

    cursor = version;
    end = version + length;
    if (!parse_uint32_component(&cursor, end, &parsed->major) ||
        cursor >= end || *cursor++ != '.' ||
        !parse_uint32_component(&cursor, end, &parsed->minor) ||
        cursor >= end || *cursor++ != '.' ||
        !parse_uint32_component(&cursor, end, &parsed->patch)) {
        return false;
    }

    parsed->prerelease = NULL;
    parsed->prerelease_length = 0U;
    if (cursor < end && *cursor == '-') {
        const char *prerelease = ++cursor;
        while (cursor < end && *cursor != '+') {
            ++cursor;
        }
        parsed->prerelease = prerelease;
        parsed->prerelease_length = (size_t)(cursor - prerelease);
        if (!validate_identifiers(prerelease, parsed->prerelease_length, true)) {
            return false;
        }
    }

    if (cursor < end && *cursor == '+') {
        metadata = ++cursor;
        if (!validate_identifiers(metadata, (size_t)(end - metadata), false)) {
            return false;
        }
        cursor = end;
    }
    return cursor == end;
}

static int compare_uint32(uint32_t left, uint32_t right)
{
    return left < right ? -1 : (left > right ? 1 : 0);
}

static bool identifier_is_numeric(const char *value, size_t length)
{
    size_t offset;

    for (offset = 0U; offset < length; ++offset) {
        if (value[offset] < '0' || value[offset] > '9') {
            return false;
        }
    }
    return true;
}

static int compare_identifier(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length)
{
    const bool left_numeric = identifier_is_numeric(left, left_length);
    const bool right_numeric = identifier_is_numeric(right, right_length);
    size_t common_length;
    int comparison;

    if (left_numeric != right_numeric) {
        return left_numeric ? -1 : 1;
    }
    if (left_numeric && left_length != right_length) {
        return left_length < right_length ? -1 : 1;
    }

    common_length = left_length < right_length ? left_length : right_length;
    comparison = memcmp(left, right, common_length);
    if (comparison != 0) {
        return comparison < 0 ? -1 : 1;
    }
    return left_length < right_length ? -1 :
           (left_length > right_length ? 1 : 0);
}

static int compare_prerelease(
    const parsed_semver_t *left,
    const parsed_semver_t *right)
{
    size_t left_offset = 0U;
    size_t right_offset = 0U;

    if (left->prerelease == NULL || right->prerelease == NULL) {
        if (left->prerelease == right->prerelease) {
            return 0;
        }
        return left->prerelease == NULL ? 1 : -1;
    }

    while (left_offset < left->prerelease_length &&
           right_offset < right->prerelease_length) {
        const size_t left_start = left_offset;
        const size_t right_start = right_offset;
        int comparison;

        while (left_offset < left->prerelease_length &&
               left->prerelease[left_offset] != '.') {
            ++left_offset;
        }
        while (right_offset < right->prerelease_length &&
               right->prerelease[right_offset] != '.') {
            ++right_offset;
        }
        comparison = compare_identifier(
            left->prerelease + left_start, left_offset - left_start,
            right->prerelease + right_start, right_offset - right_start);
        if (comparison != 0) {
            return comparison;
        }
        if (left_offset < left->prerelease_length) {
            ++left_offset;
        }
        if (right_offset < right->prerelease_length) {
            ++right_offset;
        }
    }
    return left_offset < left->prerelease_length ? 1 :
           (right_offset < right->prerelease_length ? -1 : 0);
}

online_update_error_t online_update_semver_compare(
    const char *left,
    const char *right,
    int *result)
{
    parsed_semver_t parsed_left;
    parsed_semver_t parsed_right;
    int comparison;

    if (result == NULL) {
        return ONLINE_UPDATE_ERROR_INVALID_ARGUMENT;
    }
    if (!parse_semver(left, &parsed_left) || !parse_semver(right, &parsed_right)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_VERSION;
    }

    comparison = compare_uint32(parsed_left.major, parsed_right.major);
    if (comparison == 0) {
        comparison = compare_uint32(parsed_left.minor, parsed_right.minor);
    }
    if (comparison == 0) {
        comparison = compare_uint32(parsed_left.patch, parsed_right.patch);
    }
    if (comparison == 0) {
        comparison = compare_prerelease(&parsed_left, &parsed_right);
    }
    *result = comparison;
    return ONLINE_UPDATE_ERROR_NONE;
}

online_update_channel_t online_update_select_channel(
    bool beta_updates_enabled)
{
    return beta_updates_enabled ? ONLINE_UPDATE_CHANNEL_TESTING
                                : ONLINE_UPDATE_CHANNEL_STABLE;
}

const char *online_update_channel_name(online_update_channel_t channel)
{
    switch (channel) {
    case ONLINE_UPDATE_CHANNEL_STABLE:
        return "stable";
    case ONLINE_UPDATE_CHANNEL_TESTING:
        return "testing";
    default:
        return NULL;
    }
}

static bool string_equals(const char *left, const char *right, size_t maximum)
{
    const size_t left_length = bounded_string_length(left, maximum + 1U);
    const size_t right_length = bounded_string_length(right, maximum + 1U);

    return left_length <= maximum && left_length == right_length &&
           memcmp(left, right, left_length) == 0;
}

static bool channel_is_supported(const char *channel)
{
    return string_equals(channel, "stable", ONLINE_UPDATE_CHANNEL_MAX_LENGTH) ||
           string_equals(channel, "testing", ONLINE_UPDATE_CHANNEL_MAX_LENGTH);
}

bool online_update_url_is_allowed(const char *url)
{
    static const char scheme[] = "https://";
    static const char host[] = ONLINE_UPDATE_ALLOWED_HOST;
    static const char host_with_port[] = ONLINE_UPDATE_ALLOWED_HOST ":443";
    const size_t length = bounded_string_length(
        url, ONLINE_UPDATE_URL_MAX_LENGTH + 1U);
    const char *authority;
    const char *path;
    size_t authority_length;
    size_t offset;

    if (url == NULL || length <= sizeof(scheme) - 1U ||
        length > ONLINE_UPDATE_URL_MAX_LENGTH ||
        memcmp(url, scheme, sizeof(scheme) - 1U) != 0) {
        return false;
    }

    authority = url + sizeof(scheme) - 1U;
    path = authority;
    while ((size_t)(path - url) < length && *path != '/' && *path != '?' &&
           *path != '#') {
        ++path;
    }
    authority_length = (size_t)(path - authority);
    if (!((authority_length == sizeof(host) - 1U &&
           memcmp(authority, host, sizeof(host) - 1U) == 0) ||
          (authority_length == sizeof(host_with_port) - 1U &&
           memcmp(authority, host_with_port,
                  sizeof(host_with_port) - 1U) == 0))) {
        return false;
    }
    if ((size_t)(path - url) >= length || *path != '/') {
        return false;
    }
    for (offset = 0U; offset < length; ++offset) {
        const unsigned char character = (unsigned char)url[offset];
        if (character < 0x21U || character > 0x7eU || character == '\\' ||
            character == '#') {
            return false;
        }
    }
    return true;
}

static bool sha256_model_is_valid(const online_update_manifest_t *manifest)
{
    size_t offset;

    if (bounded_string_length(
            manifest->ota_sha256_hex,
            ONLINE_UPDATE_SHA256_HEX_LENGTH + 1U) !=
        ONLINE_UPDATE_SHA256_HEX_LENGTH) {
        return false;
    }
    for (offset = 0U; offset < ONLINE_UPDATE_SHA256_HEX_LENGTH; ++offset) {
        const char character = manifest->ota_sha256_hex[offset];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

online_update_error_t online_update_manifest_evaluate(
    const online_update_manifest_t *manifest,
    const char *current_version,
    const char *expected_channel)
{
    parsed_semver_t target_version;
    int comparison;

    if (manifest == NULL || current_version == NULL ||
        expected_channel == NULL) {
        return ONLINE_UPDATE_ERROR_INVALID_ARGUMENT;
    }
    if (manifest->schema != ONLINE_UPDATE_MANIFEST_SCHEMA) {
        return ONLINE_UPDATE_ERROR_MANIFEST_SCHEMA;
    }
    if (!channel_is_supported(expected_channel) ||
        !string_equals(
            manifest->channel, expected_channel,
            ONLINE_UPDATE_CHANNEL_MAX_LENGTH)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_CHANNEL;
    }
    if (!string_equals(
            manifest->project, ONLINE_UPDATE_EXPECTED_PROJECT,
            ONLINE_UPDATE_PROJECT_MAX_LENGTH)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_PROJECT;
    }
    if (!string_equals(
            manifest->hardware, ONLINE_UPDATE_EXPECTED_HARDWARE,
            ONLINE_UPDATE_HARDWARE_MAX_LENGTH)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_HARDWARE;
    }
    if (!parse_semver(manifest->version, &target_version)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_VERSION;
    }
    if (string_equals(expected_channel, "stable",
                      ONLINE_UPDATE_CHANNEL_MAX_LENGTH) &&
        target_version.prerelease != NULL) {
        return ONLINE_UPDATE_ERROR_MANIFEST_CHANNEL;
    }
    if (online_update_semver_compare(
            manifest->minimum_ota_version,
            manifest->minimum_ota_version, &comparison) !=
        ONLINE_UPDATE_ERROR_NONE ||
        online_update_semver_compare(
            manifest->version, manifest->minimum_ota_version,
            &comparison) != ONLINE_UPDATE_ERROR_NONE ||
        comparison < 0) {
        return ONLINE_UPDATE_ERROR_MANIFEST_MINIMUM_VERSION;
    }
    if (!online_update_url_is_allowed(manifest->ota_url)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_URL;
    }
    if (manifest->ota_size == 0U ||
        manifest->ota_size > ONLINE_UPDATE_MAX_IMAGE_BYTES) {
        return ONLINE_UPDATE_ERROR_MANIFEST_SIZE;
    }
    if (!sha256_model_is_valid(manifest)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_SHA256;
    }
    if (!parse_semver(current_version, &(parsed_semver_t){0})) {
        return ONLINE_UPDATE_ERROR_CURRENT_VERSION;
    }
    if (online_update_semver_compare(
            current_version, manifest->minimum_ota_version,
            &comparison) != ONLINE_UPDATE_ERROR_NONE) {
        return ONLINE_UPDATE_ERROR_CURRENT_VERSION;
    }
    if (comparison < 0) {
        return ONLINE_UPDATE_ERROR_CURRENT_VERSION_TOO_OLD;
    }
    (void)online_update_semver_compare(
        manifest->version, current_version, &comparison);
    if (comparison == 0) {
        return ONLINE_UPDATE_ERROR_SAME_VERSION;
    }
    if (comparison < 0) {
        return ONLINE_UPDATE_ERROR_DOWNGRADE;
    }
    return ONLINE_UPDATE_ERROR_NONE;
}

bool online_update_state_is_busy(online_update_state_t state)
{
    return state == ONLINE_UPDATE_STATE_CHECKING ||
           state == ONLINE_UPDATE_STATE_CONNECTING ||
           state == ONLINE_UPDATE_STATE_DOWNLOADING ||
           state == ONLINE_UPDATE_STATE_VERIFYING;
}

bool online_update_state_can_cancel(online_update_state_t state)
{
    return state == ONLINE_UPDATE_STATE_CHECKING ||
           state == ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION ||
           state == ONLINE_UPDATE_STATE_CONNECTING;
}

bool online_update_state_is_terminal(online_update_state_t state)
{
    return state == ONLINE_UPDATE_STATE_UP_TO_DATE ||
           state == ONLINE_UPDATE_STATE_SUCCESS ||
           state == ONLINE_UPDATE_STATE_FAILED;
}

const char *online_update_state_name(online_update_state_t state)
{
    static const char *const names[] = {
        "idle",
        "checking",
        "up_to_date",
        "available",
        "awaiting_confirmation",
        "connecting",
        "downloading",
        "verifying",
        "success",
        "failed",
    };

    if ((unsigned int)state >= sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[state];
}

const char *online_update_error_name(online_update_error_t error)
{
    static const char *const names[] = {
        "none",
        "invalid_argument",
        "manifest_empty",
        "manifest_too_large",
        "manifest_json",
        "manifest_root",
        "manifest_schema",
        "manifest_channel",
        "manifest_project",
        "manifest_hardware",
        "manifest_version",
        "manifest_minimum_version",
        "manifest_ota",
        "manifest_url",
        "manifest_size",
        "manifest_sha256",
        "current_version",
        "current_version_too_old",
        "same_version",
        "downgrade",
    };

    if ((unsigned int)error >= sizeof(names) / sizeof(names[0])) {
        return "unknown";
    }
    return names[error];
}
