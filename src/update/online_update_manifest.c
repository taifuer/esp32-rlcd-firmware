#include "online_update_policy.h"

#include <string.h>

#include "cJSON.h"

static const cJSON *unique_object_item(const cJSON *object, const char *name)
{
    const cJSON *item;
    const cJSON *match = NULL;

    if (!cJSON_IsObject(object)) {
        return NULL;
    }
    for (item = object->child; item != NULL; item = item->next) {
        if (item->string != NULL && strcmp(item->string, name) == 0) {
            if (match != NULL) {
                return NULL;
            }
            match = item;
        }
    }
    return match;
}

static bool copy_bounded_string(
    const cJSON *item,
    char *destination,
    size_t maximum_length)
{
    size_t length;

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    length = strlen(item->valuestring);
    if (length == 0U || length > maximum_length) {
        return false;
    }
    memcpy(destination, item->valuestring, length + 1U);
    return true;
}

static int hex_nibble(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool parse_sha256(
    const cJSON *item,
    char normalized[ONLINE_UPDATE_SHA256_HEX_LENGTH + 1U],
    uint8_t bytes[ONLINE_UPDATE_SHA256_HEX_LENGTH / 2U])
{
    size_t offset;

    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        strlen(item->valuestring) != ONLINE_UPDATE_SHA256_HEX_LENGTH) {
        return false;
    }
    for (offset = 0U; offset < ONLINE_UPDATE_SHA256_HEX_LENGTH; offset += 2U) {
        const int high = hex_nibble(item->valuestring[offset]);
        const int low = hex_nibble(item->valuestring[offset + 1U]);

        if (high < 0 || low < 0) {
            return false;
        }
        bytes[offset / 2U] = (uint8_t)((high << 4) | low);
        normalized[offset] = "0123456789abcdef"[high];
        normalized[offset + 1U] = "0123456789abcdef"[low];
    }
    normalized[ONLINE_UPDATE_SHA256_HEX_LENGTH] = '\0';
    return true;
}

static online_update_error_t parse_manifest_fields(
    const cJSON *root,
    online_update_manifest_t *manifest)
{
    const cJSON *item;
    const cJSON *ota;
    int comparison;

    item = unique_object_item(root, "schema");
    if (!cJSON_IsNumber(item) || item->valuedouble != 1.0) {
        return ONLINE_UPDATE_ERROR_MANIFEST_SCHEMA;
    }
    manifest->schema = ONLINE_UPDATE_MANIFEST_SCHEMA;

    item = unique_object_item(root, "channel");
    if (!copy_bounded_string(
            item, manifest->channel, ONLINE_UPDATE_CHANNEL_MAX_LENGTH) ||
        (strcmp(manifest->channel, "stable") != 0 &&
         strcmp(manifest->channel, "testing") != 0)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_CHANNEL;
    }
    item = unique_object_item(root, "project");
    if (!copy_bounded_string(
            item, manifest->project, ONLINE_UPDATE_PROJECT_MAX_LENGTH) ||
        strcmp(manifest->project, ONLINE_UPDATE_EXPECTED_PROJECT) != 0) {
        return ONLINE_UPDATE_ERROR_MANIFEST_PROJECT;
    }
    item = unique_object_item(root, "hardware");
    if (!copy_bounded_string(
            item, manifest->hardware, ONLINE_UPDATE_HARDWARE_MAX_LENGTH) ||
        strcmp(manifest->hardware, ONLINE_UPDATE_EXPECTED_HARDWARE) != 0) {
        return ONLINE_UPDATE_ERROR_MANIFEST_HARDWARE;
    }
    item = unique_object_item(root, "version");
    if (!copy_bounded_string(
            item, manifest->version, ONLINE_UPDATE_VERSION_MAX_LENGTH) ||
        online_update_semver_compare(
            manifest->version, manifest->version, &comparison) !=
            ONLINE_UPDATE_ERROR_NONE) {
        return ONLINE_UPDATE_ERROR_MANIFEST_VERSION;
    }
    item = unique_object_item(root, "minimum_ota_version");
    if (!copy_bounded_string(
            item, manifest->minimum_ota_version,
            ONLINE_UPDATE_VERSION_MAX_LENGTH) ||
        online_update_semver_compare(
            manifest->minimum_ota_version,
            manifest->minimum_ota_version, &comparison) !=
            ONLINE_UPDATE_ERROR_NONE ||
        online_update_semver_compare(
            manifest->version, manifest->minimum_ota_version,
            &comparison) != ONLINE_UPDATE_ERROR_NONE ||
        comparison < 0) {
        return ONLINE_UPDATE_ERROR_MANIFEST_MINIMUM_VERSION;
    }

    ota = unique_object_item(root, "ota");
    if (!cJSON_IsObject(ota)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_OTA;
    }
    item = unique_object_item(ota, "url");
    if (!copy_bounded_string(
            item, manifest->ota_url, ONLINE_UPDATE_URL_MAX_LENGTH) ||
        !online_update_url_is_allowed(manifest->ota_url)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_URL;
    }
    item = unique_object_item(ota, "size");
    if (!cJSON_IsNumber(item) || item->valuedouble < 1.0 ||
        item->valuedouble > (double)ONLINE_UPDATE_MAX_IMAGE_BYTES ||
        item->valuedouble != (double)(uint32_t)item->valuedouble) {
        return ONLINE_UPDATE_ERROR_MANIFEST_SIZE;
    }
    manifest->ota_size = (uint32_t)item->valuedouble;

    item = unique_object_item(ota, "sha256");
    if (!parse_sha256(
            item, manifest->ota_sha256_hex, manifest->ota_sha256)) {
        return ONLINE_UPDATE_ERROR_MANIFEST_SHA256;
    }
    return ONLINE_UPDATE_ERROR_NONE;
}

online_update_error_t online_update_manifest_parse(
    const char *json,
    size_t json_length,
    online_update_manifest_t *manifest)
{
    const char *parse_end = NULL;
    const cJSON *root;
    online_update_error_t error;

    if (json == NULL || manifest == NULL) {
        return ONLINE_UPDATE_ERROR_INVALID_ARGUMENT;
    }
    if (json_length == 0U) {
        return ONLINE_UPDATE_ERROR_MANIFEST_EMPTY;
    }
    if (json_length > ONLINE_UPDATE_MANIFEST_MAX_JSON_BYTES) {
        return ONLINE_UPDATE_ERROR_MANIFEST_TOO_LARGE;
    }

    memset(manifest, 0, sizeof(*manifest));
    root = cJSON_ParseWithLengthOpts(json, json_length, &parse_end, false);
    if (root == NULL) {
        return ONLINE_UPDATE_ERROR_MANIFEST_JSON;
    }
    while (parse_end < json + json_length &&
           (*parse_end == ' ' || *parse_end == '\t' ||
            *parse_end == '\r' || *parse_end == '\n')) {
        ++parse_end;
    }
    if (parse_end != json + json_length) {
        cJSON_Delete((cJSON *)root);
        return ONLINE_UPDATE_ERROR_MANIFEST_JSON;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete((cJSON *)root);
        return ONLINE_UPDATE_ERROR_MANIFEST_ROOT;
    }

    error = parse_manifest_fields(root, manifest);
    cJSON_Delete((cJSON *)root);
    if (error != ONLINE_UPDATE_ERROR_NONE) {
        memset(manifest, 0, sizeof(*manifest));
    }
    return error;
}

online_update_error_t online_update_manifest_check(
    const char *json,
    size_t json_length,
    const char *current_version,
    const char *expected_channel,
    online_update_manifest_t *manifest)
{
    online_update_error_t error = online_update_manifest_parse(
        json, json_length, manifest);

    if (error != ONLINE_UPDATE_ERROR_NONE) {
        return error;
    }
    return online_update_manifest_evaluate(
        manifest, current_version, expected_channel);
}
