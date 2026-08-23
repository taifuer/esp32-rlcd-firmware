#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONLINE_UPDATE_MANIFEST_SCHEMA 1U
#define ONLINE_UPDATE_MANIFEST_MAX_JSON_BYTES 4096U
#define ONLINE_UPDATE_MAX_IMAGE_BYTES (3U * 1024U * 1024U)

#define ONLINE_UPDATE_EXPECTED_PROJECT "esp32-rlcd-firmware"
#define ONLINE_UPDATE_EXPECTED_HARDWARE "waveshare-esp32-s3-rlcd-4.2"
#define ONLINE_UPDATE_ALLOWED_HOST "mcu.taifua.com"

#define ONLINE_UPDATE_CHANNEL_MAX_LENGTH 15U
#define ONLINE_UPDATE_PROJECT_MAX_LENGTH 31U
#define ONLINE_UPDATE_HARDWARE_MAX_LENGTH 47U
#define ONLINE_UPDATE_VERSION_MAX_LENGTH 31U
#define ONLINE_UPDATE_URL_MAX_LENGTH 255U
#define ONLINE_UPDATE_SHA256_HEX_LENGTH 64U

typedef struct {
    uint32_t schema;
    char channel[ONLINE_UPDATE_CHANNEL_MAX_LENGTH + 1U];
    char project[ONLINE_UPDATE_PROJECT_MAX_LENGTH + 1U];
    char hardware[ONLINE_UPDATE_HARDWARE_MAX_LENGTH + 1U];
    char version[ONLINE_UPDATE_VERSION_MAX_LENGTH + 1U];
    char minimum_ota_version[ONLINE_UPDATE_VERSION_MAX_LENGTH + 1U];
    char ota_url[ONLINE_UPDATE_URL_MAX_LENGTH + 1U];
    uint32_t ota_size;
    char ota_sha256_hex[ONLINE_UPDATE_SHA256_HEX_LENGTH + 1U];
    uint8_t ota_sha256[ONLINE_UPDATE_SHA256_HEX_LENGTH / 2U];
} online_update_manifest_t;

typedef enum {
    ONLINE_UPDATE_ERROR_NONE = 0,
    ONLINE_UPDATE_ERROR_INVALID_ARGUMENT,
    ONLINE_UPDATE_ERROR_MANIFEST_EMPTY,
    ONLINE_UPDATE_ERROR_MANIFEST_TOO_LARGE,
    ONLINE_UPDATE_ERROR_MANIFEST_JSON,
    ONLINE_UPDATE_ERROR_MANIFEST_ROOT,
    ONLINE_UPDATE_ERROR_MANIFEST_SCHEMA,
    ONLINE_UPDATE_ERROR_MANIFEST_CHANNEL,
    ONLINE_UPDATE_ERROR_MANIFEST_PROJECT,
    ONLINE_UPDATE_ERROR_MANIFEST_HARDWARE,
    ONLINE_UPDATE_ERROR_MANIFEST_VERSION,
    ONLINE_UPDATE_ERROR_MANIFEST_MINIMUM_VERSION,
    ONLINE_UPDATE_ERROR_MANIFEST_OTA,
    ONLINE_UPDATE_ERROR_MANIFEST_URL,
    ONLINE_UPDATE_ERROR_MANIFEST_SIZE,
    ONLINE_UPDATE_ERROR_MANIFEST_SHA256,
    ONLINE_UPDATE_ERROR_CURRENT_VERSION,
    ONLINE_UPDATE_ERROR_CURRENT_VERSION_TOO_OLD,
    ONLINE_UPDATE_ERROR_SAME_VERSION,
    ONLINE_UPDATE_ERROR_DOWNGRADE,
} online_update_error_t;

typedef enum {
    ONLINE_UPDATE_STATE_IDLE = 0,
    ONLINE_UPDATE_STATE_CHECKING,
    ONLINE_UPDATE_STATE_UP_TO_DATE,
    ONLINE_UPDATE_STATE_AVAILABLE,
    ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION,
    ONLINE_UPDATE_STATE_CONNECTING,
    ONLINE_UPDATE_STATE_DOWNLOADING,
    ONLINE_UPDATE_STATE_VERIFYING,
    ONLINE_UPDATE_STATE_SUCCESS,
    ONLINE_UPDATE_STATE_FAILED,
} online_update_state_t;

typedef enum {
    ONLINE_UPDATE_CHANNEL_STABLE = 0,
    ONLINE_UPDATE_CHANNEL_TESTING,
} online_update_channel_t;

/* Parse a bounded, non-NUL-terminated JSON response into the trusted model. */
online_update_error_t online_update_manifest_parse(
    const char *json,
    size_t json_length,
    online_update_manifest_t *manifest);

/*
 * Validate product policy and decide whether the parsed target is installable.
 * ONLINE_UPDATE_ERROR_NONE is the only result which permits installation.
 */
online_update_error_t online_update_manifest_evaluate(
    const online_update_manifest_t *manifest,
    const char *current_version,
    const char *expected_channel);

/* Parse and evaluate in one call so callers cannot accidentally skip policy. */
online_update_error_t online_update_manifest_check(
    const char *json,
    size_t json_length,
    const char *current_version,
    const char *expected_channel,
    online_update_manifest_t *manifest);

/* SemVer 2.0 comparison. result is -1, 0 or 1 when the inputs are valid. */
online_update_error_t online_update_semver_compare(
    const char *left,
    const char *right,
    int *result);

/* The persisted beta preference is the sole update-channel selection rule. */
online_update_channel_t online_update_select_channel(
    bool beta_updates_enabled);
const char *online_update_channel_name(online_update_channel_t channel);

bool online_update_url_is_allowed(const char *url);
bool online_update_state_is_busy(online_update_state_t state);
bool online_update_state_can_cancel(online_update_state_t state);
bool online_update_state_is_terminal(online_update_state_t state);
const char *online_update_state_name(online_update_state_t state);
const char *online_update_error_name(online_update_error_t error);

#ifdef __cplusplus
}
#endif
