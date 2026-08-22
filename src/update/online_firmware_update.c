#include "online_firmware_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "network_time.h"

#define ONLINE_SESSION_TIMEOUT_MS 20000U
#define ONLINE_HTTP_TIMEOUT_MS 15000U
#define ONLINE_HTTP_BUFFER_SIZE 4096U
#define ONLINE_HTTP_MAX_CONSECUTIVE_TIMEOUTS 3U
#define ONLINE_RESTART_DELAY_MS 1800U

static const char *TAG = "online_update";
static const char STABLE_MANIFEST_URL[] =
    "https://mcu.taifua.com/esp32-rlcd/firmware/stable.json";
static const char TESTING_MANIFEST_URL[] =
    "https://mcu.taifua.com/esp32-rlcd/firmware/testing.json";
static const char EXPECTED_APP_PROJECT[] = "rlcd_firmware";

static bool s_initialized;
static bool s_cancel_requested;
static bool s_manifest_valid;
static online_update_manifest_t s_manifest;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static online_firmware_update_status_t s_status = {
    .state = ONLINE_UPDATE_STATE_IDLE,
    .policy_error = ONLINE_UPDATE_ERROR_NONE,
    .last_error = ESP_OK,
};

static bool is_prerelease(const char *version)
{
    if (version == NULL) {
        return false;
    }
    const char *dash = strchr(version, '-');
    const char *metadata = strchr(version, '+');
    return dash != NULL && (metadata == NULL || dash < metadata);
}

static const char *manifest_url(void)
{
    return is_prerelease(s_status.current_version)
               ? TESTING_MANIFEST_URL
               : STABLE_MANIFEST_URL;
}

static const char *expected_channel(void)
{
    return is_prerelease(s_status.current_version) ? "testing" : "stable";
}

static void set_state(online_update_state_t state, esp_err_t error,
                      online_update_error_t policy_error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.last_error = error;
    s_status.policy_error = policy_error;
    portEXIT_CRITICAL(&s_lock);
}

static bool cancellation_requested(void)
{
    bool cancelled;
    portENTER_CRITICAL(&s_lock);
    cancelled = s_cancel_requested;
    portEXIT_CRITICAL(&s_lock);
    return cancelled;
}

static void checked_now_string(
    char value[ONLINE_FIRMWARE_UPDATE_CHECKED_CAPACITY])
{
    memcpy(value, "JUST NOW", sizeof("JUST NOW"));
    const time_t now = time(NULL);
    struct tm local = {0};
    if (now != (time_t)-1 && localtime_r(&now, &local) != NULL &&
        local.tm_year >= 100 && local.tm_year <= 199) {
        if (strftime(value, ONLINE_FIRMWARE_UPDATE_CHECKED_CAPACITY,
                     "%Y-%m-%d %H:%M", &local) == 0U) {
            memcpy(value, "JUST NOW", sizeof("JUST NOW"));
        }
    }
}

static void set_progress(size_t downloaded, size_t total)
{
    uint8_t percent = 0U;
    if (total > 0U) {
        percent = downloaded >= total
                      ? 100U
                      : (uint8_t)((downloaded * 100U) / total);
    }
    portENTER_CRITICAL(&s_lock);
    s_status.downloaded_bytes = downloaded;
    s_status.total_bytes = total;
    s_status.percent = percent;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t online_firmware_update_get_status(
    online_firmware_update_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static esp_err_t fetch_manifest_json(char **json, size_t *json_length)
{
    if (json == NULL || json_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *json = NULL;
    *json_length = 0U;

    esp_http_client_config_t config = {
        .url = manifest_url(),
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = ONLINE_HTTP_TIMEOUT_MS,
        .buffer_size = ONLINE_HTTP_BUFFER_SIZE,
        .keep_alive_enable = true,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t error = esp_http_client_open(client, 0);
    if (error != ESP_OK) {
        esp_http_client_cleanup(client);
        return error;
    }
    const int64_t announced_length = esp_http_client_fetch_headers(client);
    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        error = status_code == 404 ? ESP_ERR_NOT_FOUND
                                   : ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    if (announced_length > (int64_t)ONLINE_UPDATE_MANIFEST_MAX_JSON_BYTES) {
        error = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    char *buffer = malloc(ONLINE_UPDATE_MANIFEST_MAX_JSON_BYTES + 1U);
    if (buffer == NULL) {
        error = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    size_t received = 0U;
    while (received <= ONLINE_UPDATE_MANIFEST_MAX_JSON_BYTES) {
        if (cancellation_requested()) {
            error = ESP_ERR_NOT_FINISHED;
            break;
        }
        const size_t remaining =
            ONLINE_UPDATE_MANIFEST_MAX_JSON_BYTES + 1U - received;
        const int count = esp_http_client_read(
            client, &buffer[received], (int)remaining);
        if (count < 0) {
            error = ESP_FAIL;
            break;
        }
        if (count == 0) {
            if (!esp_http_client_is_complete_data_received(client)) {
                error = ESP_ERR_INVALID_RESPONSE;
            }
            break;
        }
        received += (size_t)count;
        if (received > ONLINE_UPDATE_MANIFEST_MAX_JSON_BYTES) {
            error = ESP_ERR_INVALID_SIZE;
            break;
        }
    }
    if (error == ESP_OK && received == 0U) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK && announced_length >= 0 &&
        received != (size_t)announced_length) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        buffer[received] = '\0';
        *json = buffer;
        *json_length = received;
        buffer = NULL;
    }
    free(buffer);

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return error;
}

static esp_err_t load_and_evaluate_manifest(
    online_update_manifest_t *manifest,
    online_update_error_t *policy_error)
{
    char *json = NULL;
    size_t json_length = 0U;
    esp_err_t error = fetch_manifest_json(&json, &json_length);
    if (error != ESP_OK) {
        return error;
    }

    const online_update_error_t result = online_update_manifest_check(
        json, json_length, s_status.current_version, expected_channel(),
        manifest);
    free(json);
    if (policy_error != NULL) {
        *policy_error = result;
    }
    return ESP_OK;
}

static void check_task(void *argument)
{
    (void)argument;
    esp_err_t error = network_time_begin_online_session(
        ONLINE_SESSION_TIMEOUT_MS);
    online_update_error_t policy_error = ONLINE_UPDATE_ERROR_NONE;
    online_update_manifest_t manifest = {0};
    if (error == ESP_OK && !cancellation_requested()) {
        error = load_and_evaluate_manifest(&manifest, &policy_error);
    }
    char checked[ONLINE_FIRMWARE_UPDATE_CHECKED_CAPACITY] = {0};
    checked_now_string(checked);

    bool cancelled = false;
    online_update_state_t published_state = ONLINE_UPDATE_STATE_FAILED;
    portENTER_CRITICAL(&s_lock);
    if (s_cancel_requested) {
        cancelled = true;
        s_status.state = s_manifest_valid ? ONLINE_UPDATE_STATE_AVAILABLE
                                          : ONLINE_UPDATE_STATE_IDLE;
        s_status.last_error = ESP_OK;
        s_status.policy_error = ONLINE_UPDATE_ERROR_NONE;
    } else if (error == ESP_OK) {
        memcpy(s_status.last_checked, checked, sizeof(checked));
        if (policy_error == ONLINE_UPDATE_ERROR_NONE) {
            s_manifest = manifest;
            s_manifest_valid = true;
            snprintf(s_status.latest_version,
                     sizeof(s_status.latest_version), "%s",
                     manifest.version);
            s_status.state = ONLINE_UPDATE_STATE_AVAILABLE;
            s_status.last_error = ESP_OK;
            s_status.policy_error = ONLINE_UPDATE_ERROR_NONE;
        } else if (policy_error == ONLINE_UPDATE_ERROR_SAME_VERSION ||
                   policy_error == ONLINE_UPDATE_ERROR_DOWNGRADE) {
            s_manifest_valid = false;
            snprintf(s_status.latest_version,
                     sizeof(s_status.latest_version), "%s",
                     manifest.version);
            s_status.state = ONLINE_UPDATE_STATE_UP_TO_DATE;
            s_status.last_error = ESP_OK;
            s_status.policy_error = policy_error;
        } else {
            s_manifest_valid = false;
            s_status.state = ONLINE_UPDATE_STATE_FAILED;
            s_status.last_error = ESP_OK;
            s_status.policy_error = policy_error;
        }
    } else if (error == ESP_ERR_NOT_FINISHED) {
        cancelled = true;
        s_status.state = s_manifest_valid ? ONLINE_UPDATE_STATE_AVAILABLE
                                          : ONLINE_UPDATE_STATE_IDLE;
        s_status.last_error = ESP_OK;
        s_status.policy_error = ONLINE_UPDATE_ERROR_NONE;
    } else {
        s_manifest_valid = false;
        memcpy(s_status.last_checked, checked, sizeof(checked));
        s_status.state = ONLINE_UPDATE_STATE_FAILED;
        s_status.last_error = error;
        s_status.policy_error = policy_error;
    }
    published_state = s_status.state;
    portEXIT_CRITICAL(&s_lock);

    if (cancelled) {
        ESP_LOGI(TAG, "online update check cancelled");
    } else if (published_state == ONLINE_UPDATE_STATE_AVAILABLE) {
        ESP_LOGI(TAG, "online update available: v%s", manifest.version);
    } else if (published_state == ONLINE_UPDATE_STATE_UP_TO_DATE) {
        ESP_LOGI(TAG, "firmware is up to date (remote v%s)",
                 manifest.version);
    } else if (error == ESP_OK) {
        ESP_LOGW(TAG, "update manifest rejected: %s",
                 online_update_error_name(policy_error));
    } else {
        ESP_LOGW(TAG, "online update check failed: %s",
                 esp_err_to_name(error));
    }
    (void)network_time_end_online_session();
    vTaskDelete(NULL);
}

static bool app_description_matches(const esp_app_desc_t *description,
                                    const online_update_manifest_t *manifest)
{
    return description != NULL && manifest != NULL &&
           strncmp(description->project_name, EXPECTED_APP_PROJECT,
                   sizeof(description->project_name)) == 0 &&
           strncmp(description->version, manifest->version,
                   sizeof(description->version)) == 0;
}

static bool image_header_matches(
    const uint8_t *header,
    size_t header_length,
    const online_update_manifest_t *manifest)
{
    const size_t description_offset =
        sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (header == NULL || manifest == NULL ||
        header_length < description_offset + sizeof(esp_app_desc_t)) {
        return false;
    }
    esp_image_header_t image = {0};
    esp_app_desc_t description = {0};
    memcpy(&image, header, sizeof(image));
    memcpy(&description, header + description_offset, sizeof(description));
    return image.magic == ESP_IMAGE_HEADER_MAGIC &&
           app_description_matches(&description, manifest);
}

static esp_err_t read_http_chunk(
    esp_http_client_handle_t client,
    uint8_t *buffer,
    size_t capacity,
    size_t *length,
    unsigned int *consecutive_timeouts)
{
    if (client == NULL || buffer == NULL || capacity == 0U ||
        length == NULL || consecutive_timeouts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const int count = esp_http_client_read(client, (char *)buffer,
                                           (int)capacity);
    if (count == -ESP_ERR_HTTP_EAGAIN) {
        if (++*consecutive_timeouts >
            ONLINE_HTTP_MAX_CONSECUTIVE_TIMEOUTS) {
            return ESP_ERR_TIMEOUT;
        }
        *length = 0U;
        return ESP_ERR_HTTP_EAGAIN;
    }
    if (count < 0) {
        return ESP_FAIL;
    }
    *consecutive_timeouts = 0U;
    *length = (size_t)count;
    return ESP_OK;
}

static esp_err_t download_update(
    const online_update_manifest_t *manifest,
    online_update_error_t *policy_error)
{
    const size_t required_header_length =
        sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
        sizeof(esp_app_desc_t);
    const esp_partition_t *partition =
        esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || manifest->ota_size == 0U ||
        manifest->ota_size > partition->size ||
        manifest->ota_size < required_header_length) {
        return ESP_ERR_INVALID_SIZE;
    }

    mbedtls_sha256_context digest;
    mbedtls_sha256_init(&digest);
    if (mbedtls_sha256_starts(&digest, 0) != 0) {
        mbedtls_sha256_free(&digest);
        return ESP_FAIL;
    }

    esp_http_client_config_t http_config = {
        .url = manifest->ota_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = ONLINE_HTTP_TIMEOUT_MS,
        .buffer_size = ONLINE_HTTP_BUFFER_SIZE,
        .keep_alive_enable = true,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        mbedtls_sha256_free(&digest);
        return ESP_ERR_NO_MEM;
    }

    esp_ota_handle_t ota_handle = 0;
    bool ota_started = false;
    uint8_t *buffer = NULL;
    esp_err_t error = esp_http_client_open(client, 0);
    if (error != ESP_OK) {
        goto cleanup;
    }
    const int64_t announced_length = esp_http_client_fetch_headers(client);
    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        error = status_code == 404 ? ESP_ERR_NOT_FOUND
                                   : ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    if (announced_length < 0 ||
        (uint64_t)announced_length != manifest->ota_size) {
        error = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    uint8_t header[sizeof(esp_image_header_t) +
                   sizeof(esp_image_segment_header_t) +
                   sizeof(esp_app_desc_t)] = {0};
    size_t header_received = 0U;
    size_t received = 0U;
    unsigned int consecutive_timeouts = 0U;
    while (header_received < sizeof(header)) {
        size_t length = 0U;
        error = read_http_chunk(client, header + header_received,
                                sizeof(header) - header_received, &length,
                                &consecutive_timeouts);
        if (error == ESP_ERR_HTTP_EAGAIN) {
            continue;
        }
        if (error != ESP_OK || length == 0U) {
            error = error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
            goto cleanup;
        }
        if (mbedtls_sha256_update(&digest, header + header_received,
                                  length) != 0) {
            error = ESP_FAIL;
            goto cleanup;
        }
        header_received += length;
        received += length;
    }
    if (!image_header_matches(header, sizeof(header), manifest)) {
        if (policy_error != NULL) {
            *policy_error = ONLINE_UPDATE_ERROR_MANIFEST_VERSION;
        }
        error = ESP_ERR_OTA_VALIDATE_FAILED;
        goto cleanup;
    }

    buffer = malloc(ONLINE_HTTP_BUFFER_SIZE);
    if (buffer == NULL) {
        error = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    error = esp_ota_begin(partition, manifest->ota_size, &ota_handle);
    if (error != ESP_OK) {
        goto cleanup;
    }
    ota_started = true;
    error = esp_ota_write(ota_handle, header, sizeof(header));
    if (error != ESP_OK) {
        goto cleanup;
    }
    set_progress(received, manifest->ota_size);

    while (received < manifest->ota_size) {
        size_t length = 0U;
        error = read_http_chunk(client, buffer, ONLINE_HTTP_BUFFER_SIZE,
                                &length, &consecutive_timeouts);
        if (error == ESP_ERR_HTTP_EAGAIN) {
            continue;
        }
        if (error != ESP_OK || length == 0U) {
            error = error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
            break;
        }
        if (length > manifest->ota_size - received ||
            mbedtls_sha256_update(&digest, buffer, length) != 0) {
            error = ESP_ERR_INVALID_SIZE;
            break;
        }
        error = esp_ota_write(ota_handle, buffer, length);
        if (error != ESP_OK) {
            break;
        }
        received += length;
        set_progress(received, manifest->ota_size);
    }
    free(buffer);
    buffer = NULL;
    if (error != ESP_OK) {
        goto cleanup;
    }
    if (received != manifest->ota_size ||
        !esp_http_client_is_complete_data_received(client)) {
        error = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    set_state(ONLINE_UPDATE_STATE_VERIFYING, ESP_OK,
              ONLINE_UPDATE_ERROR_NONE);
    uint8_t calculated_sha256[32] = {0};
    if (mbedtls_sha256_finish(&digest, calculated_sha256) != 0 ||
        memcmp(calculated_sha256, manifest->ota_sha256,
               sizeof(calculated_sha256)) != 0) {
        if (policy_error != NULL) {
            *policy_error = ONLINE_UPDATE_ERROR_MANIFEST_SHA256;
        }
        memset(calculated_sha256, 0, sizeof(calculated_sha256));
        error = ESP_ERR_OTA_VALIDATE_FAILED;
        goto cleanup;
    }
    memset(calculated_sha256, 0, sizeof(calculated_sha256));
    error = esp_ota_end(ota_handle);
    ota_started = false;
    if (error == ESP_OK) {
        error = esp_ota_set_boot_partition(partition);
    }

cleanup:
    free(buffer);
    if (ota_started) {
        (void)esp_ota_abort(ota_handle);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    mbedtls_sha256_free(&digest);
    return error;
}

static void install_task(void *argument)
{
    (void)argument;
    set_state(ONLINE_UPDATE_STATE_CONNECTING, ESP_OK,
              ONLINE_UPDATE_ERROR_NONE);
    esp_err_t error = network_time_begin_online_session(
        ONLINE_SESSION_TIMEOUT_MS);
    online_update_error_t policy_error = ONLINE_UPDATE_ERROR_NONE;
    online_update_manifest_t fresh = {0};
    if (error == ESP_OK && cancellation_requested()) {
        error = ESP_ERR_NOT_FINISHED;
    }
    if (error == ESP_OK) {
        error = load_and_evaluate_manifest(&fresh, &policy_error);
    }
    bool target_unchanged = false;
    portENTER_CRITICAL(&s_lock);
    if (error == ESP_OK && policy_error == ONLINE_UPDATE_ERROR_NONE &&
        s_manifest_valid) {
        target_unchanged =
            strcmp(fresh.version, s_manifest.version) == 0 &&
            strcmp(fresh.ota_url, s_manifest.ota_url) == 0 &&
            fresh.ota_size == s_manifest.ota_size &&
            memcmp(fresh.ota_sha256, s_manifest.ota_sha256,
                   sizeof(fresh.ota_sha256)) == 0;
    }
    portEXIT_CRITICAL(&s_lock);
    if (error == ESP_OK && policy_error == ONLINE_UPDATE_ERROR_NONE &&
        !target_unchanged) {
        policy_error = ONLINE_UPDATE_ERROR_MANIFEST_OTA;
        error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK && policy_error == ONLINE_UPDATE_ERROR_NONE) {
        bool committed = false;
        portENTER_CRITICAL(&s_lock);
        if (!s_cancel_requested &&
            s_status.state == ONLINE_UPDATE_STATE_CONNECTING) {
            s_status.state = ONLINE_UPDATE_STATE_DOWNLOADING;
            s_status.last_error = ESP_OK;
            s_status.policy_error = ONLINE_UPDATE_ERROR_NONE;
            s_status.downloaded_bytes = 0U;
            s_status.total_bytes = fresh.ota_size;
            s_status.percent = 0U;
            committed = true;
        }
        portEXIT_CRITICAL(&s_lock);
        error = committed ? download_update(&fresh, &policy_error)
                          : ESP_ERR_NOT_FINISHED;
    }

    (void)network_time_end_online_session();
    if (error == ESP_ERR_NOT_FINISHED) {
        set_state(ONLINE_UPDATE_STATE_AVAILABLE, ESP_OK,
                  ONLINE_UPDATE_ERROR_NONE);
        ESP_LOGI(TAG, "online update installation cancelled before writing");
        vTaskDelete(NULL);
        return;
    }
    if (error == ESP_OK) {
        set_progress(fresh.ota_size, fresh.ota_size);
        set_state(ONLINE_UPDATE_STATE_SUCCESS, ESP_OK,
                  ONLINE_UPDATE_ERROR_NONE);
        ESP_LOGI(TAG, "online update v%s verified; restarting", fresh.version);
        vTaskDelay(pdMS_TO_TICKS(ONLINE_RESTART_DELAY_MS));
        esp_restart();
    }

    set_state(ONLINE_UPDATE_STATE_FAILED, error, policy_error);
    ESP_LOGE(TAG, "online update failed: %s / %s",
             esp_err_to_name(error),
             online_update_error_name(policy_error));
    vTaskDelete(NULL);
}

esp_err_t online_firmware_update_init(const char *current_version)
{
    if (current_version == NULL || current_version[0] == '\0' ||
        strlen(current_version) >= ONLINE_FIRMWARE_UPDATE_VERSION_CAPACITY) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized) {
        return ESP_OK;
    }
    int ignored = 0;
    if (online_update_semver_compare(current_version, current_version,
                                     &ignored) !=
        ONLINE_UPDATE_ERROR_NONE) {
        return ESP_ERR_INVALID_VERSION;
    }
    snprintf(s_status.current_version, sizeof(s_status.current_version),
             "%s", current_version);
    s_initialized = true;
    ESP_LOGI(TAG, "online update service ready (%s channel)",
             expected_channel());
    return ESP_OK;
}

esp_err_t online_firmware_update_request_check(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    portENTER_CRITICAL(&s_lock);
    const online_update_state_t state = s_status.state;
    if (online_update_state_is_busy(state) ||
        state == ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        if (state != ONLINE_UPDATE_STATE_AVAILABLE) {
            s_manifest_valid = false;
        }
        s_cancel_requested = false;
        s_status.state = ONLINE_UPDATE_STATE_CHECKING;
        s_status.last_error = ESP_OK;
        s_status.policy_error = ONLINE_UPDATE_ERROR_NONE;
        s_status.downloaded_bytes = 0U;
        s_status.total_bytes = 0U;
        s_status.percent = 0U;
    }
    portEXIT_CRITICAL(&s_lock);
    if (result != ESP_OK) {
        return result;
    }
    if (xTaskCreate(check_task, "online_check", 10240U, NULL, 5U, NULL) !=
        pdPASS) {
        set_state(ONLINE_UPDATE_STATE_FAILED, ESP_ERR_NO_MEM,
                  ONLINE_UPDATE_ERROR_NONE);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t online_firmware_update_request_confirmation(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    if (s_status.state == ONLINE_UPDATE_STATE_AVAILABLE &&
        s_manifest_valid) {
        s_status.state = ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION;
        s_status.last_error = ESP_OK;
        s_status.policy_error = ONLINE_UPDATE_ERROR_NONE;
        result = ESP_OK;
    }
    portEXIT_CRITICAL(&s_lock);
    return result;
}

esp_err_t online_firmware_update_start_install(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    if (s_status.state == ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION &&
        s_manifest_valid) {
        s_cancel_requested = false;
        s_status.state = ONLINE_UPDATE_STATE_CONNECTING;
        s_status.last_error = ESP_OK;
        s_status.policy_error = ONLINE_UPDATE_ERROR_NONE;
        result = ESP_OK;
    }
    portEXIT_CRITICAL(&s_lock);
    if (result != ESP_OK) {
        return result;
    }
    if (xTaskCreate(install_task, "online_install", 12288U, NULL, 6U, NULL) !=
        pdPASS) {
        set_state(ONLINE_UPDATE_STATE_FAILED, ESP_ERR_NO_MEM,
                  ONLINE_UPDATE_ERROR_NONE);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t online_firmware_update_cancel(void)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    if (s_status.state == ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION) {
        s_status.state = ONLINE_UPDATE_STATE_AVAILABLE;
        s_status.last_error = ESP_OK;
        s_status.policy_error = ONLINE_UPDATE_ERROR_NONE;
        result = ESP_OK;
    } else if (s_status.state == ONLINE_UPDATE_STATE_CHECKING ||
               s_status.state == ONLINE_UPDATE_STATE_CONNECTING) {
        s_cancel_requested = true;
        result = ESP_OK;
    }
    portEXIT_CRITICAL(&s_lock);
    return result;
}

const char *online_firmware_update_error_detail(
    const online_firmware_update_status_t *status)
{
    if (status == NULL) {
        return "UNKNOWN ERROR";
    }
    if (status->policy_error == ONLINE_UPDATE_ERROR_CURRENT_VERSION_TOO_OLD) {
        return "USE LOCAL UPDATE";
    }
    if (status->policy_error != ONLINE_UPDATE_ERROR_NONE) {
        return "UPDATE REJECTED";
    }
    switch (status->last_error) {
    case ESP_ERR_INVALID_STATE:
        return "NETWORK BUSY";
    case ESP_ERR_NOT_FOUND:
        return "SERVER UNAVAILABLE";
    case ESP_ERR_TIMEOUT:
        return "NO INTERNET";
    case ESP_ERR_INVALID_RESPONSE:
        return "SERVER RESPONSE ERROR";
    case ESP_ERR_OTA_VALIDATE_FAILED:
        return "VERIFICATION FAILED";
    case ESP_ERR_INVALID_SIZE:
        return "INVALID DOWNLOAD";
    case ESP_ERR_NO_MEM:
        return "NOT ENOUGH MEMORY";
    default:
        return status->last_error == ESP_OK ? "CHECK FAILED"
                                            : "DOWNLOAD FAILED";
    }
}
