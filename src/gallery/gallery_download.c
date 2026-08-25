#include "gallery_download.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gallery_manifest.h"
#include "network_time.h"
#include "sd_image.h"

enum {
    GALLERY_ONLINE_SESSION_TIMEOUT_MS = 20000,
    GALLERY_OPERATION_TIMEOUT_MS = 120000,
    GALLERY_HTTP_TIMEOUT_MS = 15000,
    GALLERY_HTTP_BUFFER_SIZE = 4096,
    GALLERY_TASK_STACK_BYTES = 12288,
    GALLERY_TASK_PRIORITY = 5,
};

static const char *TAG = "gallery_download";
static const char GALLERY_ORIGIN[] = "https://mcu.taifua.com";
static const char GALLERY_CATALOG_URL[] =
    "https://mcu.taifua.com/gallery/stable.json";

static bool s_initialized;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static gallery_download_status_t s_status = {
    .state = GALLERY_DOWNLOAD_STATE_IDLE,
    .last_error = ESP_OK,
};

static bool operation_deadline_expired(TickType_t started)
{
    return xTaskGetTickCount() - started >=
           pdMS_TO_TICKS(GALLERY_OPERATION_TIMEOUT_MS);
}

static void set_state(gallery_download_state_t state, esp_err_t error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.last_error = error;
    portEXIT_CRITICAL(&s_lock);
}

static void set_catalog_progress(size_t image_count, size_t total_bytes)
{
    portENTER_CRITICAL(&s_lock);
    s_status.image_count = image_count;
    s_status.total_bytes = total_bytes;
    s_status.downloaded_bytes = 0U;
    s_status.image_index = 0U;
    s_status.percent = 0U;
    portEXIT_CRITICAL(&s_lock);
}

static void set_download_progress(size_t image_index, size_t downloaded,
                                  size_t total)
{
    const uint8_t percent =
        total == 0U
            ? 0U
            : (downloaded >= total
                   ? 100U
                   : (uint8_t)((downloaded * 100U) / total));
    portENTER_CRITICAL(&s_lock);
    s_status.state = GALLERY_DOWNLOAD_STATE_DOWNLOADING;
    s_status.image_index = image_index;
    s_status.downloaded_bytes = downloaded;
    s_status.total_bytes = total;
    s_status.percent = percent;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t gallery_download_get_status(gallery_download_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

bool gallery_download_state_is_active(gallery_download_state_t state)
{
    return state != GALLERY_DOWNLOAD_STATE_IDLE;
}

static esp_err_t read_client_chunk(esp_http_client_handle_t client,
                                   uint8_t *buffer, size_t capacity,
                                   size_t *length,
                                   unsigned int *timeouts,
                                   TickType_t operation_started)
{
    if (client == NULL || buffer == NULL || capacity == 0U ||
        length == NULL || timeouts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (operation_deadline_expired(operation_started)) {
        return ESP_ERR_TIMEOUT;
    }
    const int received = esp_http_client_read(
        client, (char *)buffer, (int)capacity);
    if (operation_deadline_expired(operation_started)) {
        return ESP_ERR_TIMEOUT;
    }
    if (received == -ESP_ERR_HTTP_EAGAIN) {
        if (++*timeouts > 3U) {
            return ESP_ERR_TIMEOUT;
        }
        *length = 0U;
        return ESP_ERR_HTTP_EAGAIN;
    }
    if (received < 0) {
        return ESP_FAIL;
    }
    *timeouts = 0U;
    *length = (size_t)received;
    return ESP_OK;
}

static esp_err_t fetch_json(const char *url, char **json, size_t *length,
                            TickType_t operation_started)
{
    if (url == NULL || json == NULL || length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *json = NULL;
    *length = 0U;
    if (operation_deadline_expired(operation_started)) {
        return ESP_ERR_TIMEOUT;
    }

    const esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = GALLERY_HTTP_TIMEOUT_MS,
        .buffer_size = GALLERY_HTTP_BUFFER_SIZE,
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
    if (operation_deadline_expired(operation_started)) {
        error = ESP_ERR_TIMEOUT;
        goto cleanup;
    }
    const int64_t announced_length = esp_http_client_fetch_headers(client);
    if (operation_deadline_expired(operation_started)) {
        error = ESP_ERR_TIMEOUT;
        goto cleanup;
    }
    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        error = status_code == 404 ? ESP_ERR_NOT_FOUND
                                   : ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    if (announced_length < 1 ||
        announced_length > (int64_t)GALLERY_MANIFEST_MAX_JSON_BYTES) {
        error = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    char *buffer = malloc((size_t)announced_length + 1U);
    if (buffer == NULL) {
        error = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    size_t received_total = 0U;
    unsigned int timeouts = 0U;
    while (received_total < (size_t)announced_length) {
        size_t received = 0U;
        error = read_client_chunk(
            client, (uint8_t *)buffer + received_total,
            (size_t)announced_length - received_total, &received,
            &timeouts, operation_started);
        if (error == ESP_ERR_HTTP_EAGAIN) {
            continue;
        }
        if (error != ESP_OK || received == 0U) {
            error = error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
            break;
        }
        received_total += received;
    }
    if (error == ESP_OK &&
        (received_total != (size_t)announced_length ||
         !esp_http_client_is_complete_data_received(client))) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        buffer[received_total] = '\0';
        *json = buffer;
        *length = received_total;
        buffer = NULL;
    }
    free(buffer);

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return error;
}

static bool build_url(const char *path, char *url, size_t capacity)
{
    if (path == NULL || path[0] != '/' || url == NULL || capacity == 0U) {
        return false;
    }
    const int written = snprintf(url, capacity, "%s%s", GALLERY_ORIGIN,
                                 path);
    return written > 0 && (size_t)written < capacity;
}

static esp_err_t download_image(const gallery_manifest_image_t *image,
                                size_t image_index, size_t completed_bytes,
                                size_t total_bytes,
                                TickType_t operation_started)
{
    if (image == NULL || image->size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (operation_deadline_expired(operation_started)) {
        return ESP_ERR_TIMEOUT;
    }
    sd_image_import_options_t options = {
        .expected_size = image->size,
        .verify_sha256 = true,
    };
    memcpy(options.expected_sha256, image->sha256,
           sizeof(options.expected_sha256));

    sd_image_import_t *import = NULL;
    esp_err_t error = sd_image_import_begin(&options, &import);
    if (error != ESP_OK) {
        return error;
    }
    bool import_open = true;
    char url[192];
    if (!build_url(image->path, url, sizeof(url))) {
        error = ESP_ERR_INVALID_ARG;
        goto cleanup_import;
    }
    const esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = GALLERY_HTTP_TIMEOUT_MS,
        .buffer_size = GALLERY_HTTP_BUFFER_SIZE,
        .keep_alive_enable = true,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        error = ESP_ERR_NO_MEM;
        goto cleanup_import;
    }
    error = esp_http_client_open(client, 0);
    if (error != ESP_OK) {
        goto cleanup_client;
    }
    if (operation_deadline_expired(operation_started)) {
        error = ESP_ERR_TIMEOUT;
        goto cleanup_client;
    }
    const int64_t announced_length = esp_http_client_fetch_headers(client);
    if (operation_deadline_expired(operation_started)) {
        error = ESP_ERR_TIMEOUT;
        goto cleanup_client;
    }
    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        error = status_code == 404 ? ESP_ERR_NOT_FOUND
                                   : ESP_ERR_INVALID_RESPONSE;
        goto cleanup_client;
    }
    if (announced_length < 0 ||
        (uint64_t)announced_length != image->size) {
        error = ESP_ERR_INVALID_SIZE;
        goto cleanup_client;
    }

    uint8_t *buffer = malloc(GALLERY_HTTP_BUFFER_SIZE);
    if (buffer == NULL) {
        error = ESP_ERR_NO_MEM;
        goto cleanup_client;
    }
    size_t received_total = 0U;
    unsigned int timeouts = 0U;
    set_download_progress(image_index, completed_bytes, total_bytes);
    while (received_total < image->size) {
        const size_t remaining = image->size - received_total;
        size_t received = 0U;
        error = read_client_chunk(
            client, buffer,
            remaining < GALLERY_HTTP_BUFFER_SIZE
                ? remaining
                : GALLERY_HTTP_BUFFER_SIZE,
            &received, &timeouts, operation_started);
        if (error == ESP_ERR_HTTP_EAGAIN) {
            continue;
        }
        if (error != ESP_OK || received == 0U) {
            error = error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
            break;
        }
        error = sd_image_import_write(import, buffer, received);
        if (error != ESP_OK) {
            break;
        }
        received_total += received;
        set_download_progress(image_index,
                              completed_bytes + received_total,
                              total_bytes);
    }
    free(buffer);
    if (error == ESP_OK &&
        (received_total != image->size ||
         !esp_http_client_is_complete_data_received(client))) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        set_state(GALLERY_DOWNLOAD_STATE_VERIFYING, ESP_OK);
        sd_image_import_result_t result = {0};
        error = sd_image_import_commit(import, &result);
        import_open = false;
        if (error == ESP_OK) {
            ESP_LOGI(TAG, "gallery image %s %s as %s", image->id,
                     result.duplicate ? "already present" : "installed",
                     result.filename);
        }
    }

cleanup_client:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
cleanup_import:
    if (import_open) {
        const esp_err_t abort_error = sd_image_import_abort(import);
        if (error == ESP_OK) {
            error = abort_error;
        }
    }
    return error;
}

static esp_err_t load_gallery_manifest(gallery_manifest_t *manifest,
                                       TickType_t operation_started)
{
    char *json = NULL;
    size_t length = 0U;
    esp_err_t error = fetch_json(GALLERY_CATALOG_URL, &json, &length,
                                 operation_started);
    if (error != ESP_OK) {
        return error;
    }
    gallery_catalog_pointer_t pointer = {0};
    gallery_manifest_result_t parse_result =
        gallery_catalog_pointer_parse(json, length, &pointer);
    free(json);
    if (parse_result != GALLERY_MANIFEST_OK) {
        ESP_LOGW(TAG, "gallery catalog rejected: %s",
                 gallery_manifest_result_name(parse_result));
        return ESP_ERR_INVALID_RESPONSE;
    }

    char manifest_url[192];
    if (!build_url(pointer.manifest_path, manifest_url,
                   sizeof(manifest_url))) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    json = NULL;
    length = 0U;
    error = fetch_json(manifest_url, &json, &length,
                       operation_started);
    if (error != ESP_OK) {
        return error;
    }
    parse_result = gallery_manifest_parse(json, length, manifest);
    free(json);
    if (parse_result != GALLERY_MANIFEST_OK ||
        !gallery_manifest_matches_pointer(manifest, &pointer)) {
        ESP_LOGW(TAG, "gallery manifest rejected: %s",
                 gallery_manifest_result_name(parse_result));
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static bool cached_filename_exists(const char *filename)
{
    const size_t count = sd_image_store_count();
    for (size_t index = 0U; index < count; ++index) {
        char cached[SD_IMAGE_FILENAME_CAPACITY] = {0};
        if (sd_image_store_filename_at(index, cached, sizeof(cached)) &&
            strcmp(cached, filename) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t gallery_has_capacity(const gallery_manifest_t *manifest)
{
    if (manifest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t new_images = 0U;
    for (size_t index = 0U; index < manifest->image_count; ++index) {
        char filename[SD_IMAGE_FILENAME_CAPACITY];
        if (!sd_image_import_build_filename(
                manifest->images[index].sha256, filename,
                sizeof(filename))) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (!cached_filename_exists(filename)) {
            ++new_images;
        }
    }
    const size_t current = sd_image_store_count();
    return current <= SD_IMAGE_MAX_IMAGES &&
                   new_images <= SD_IMAGE_MAX_IMAGES - current
               ? ESP_OK
               : ESP_ERR_INVALID_SIZE;
}

static void gallery_task(void *argument)
{
    (void)argument;
    const TickType_t operation_started = xTaskGetTickCount();
    bool online_session_open = false;
    esp_err_t error =
        network_time_begin_online_session_from_maintenance(
            GALLERY_ONLINE_SESSION_TIMEOUT_MS);
    if (error == ESP_OK) {
        online_session_open = true;
        set_state(GALLERY_DOWNLOAD_STATE_FETCHING_CATALOG, ESP_OK);
    } else {
        /* If the atomic handoff was rejected before changing ownership,
         * release the old maintenance owner as a final cleanup. */
        network_time_end_maintenance();
    }

    gallery_manifest_t *manifest = NULL;
    if (error == ESP_OK) {
        manifest = calloc(1U, sizeof(*manifest));
        error = manifest == NULL ? ESP_ERR_NO_MEM
                                 : load_gallery_manifest(
                                       manifest, operation_started);
    }
    if (error == ESP_OK) {
        error = gallery_has_capacity(manifest);
    }
    if (error == ESP_OK) {
        set_catalog_progress(manifest->image_count, manifest->total_size);
        size_t completed = 0U;
        for (size_t index = 0U; index < manifest->image_count; ++index) {
            if (operation_deadline_expired(operation_started)) {
                error = ESP_ERR_TIMEOUT;
                break;
            }
            error = download_image(&manifest->images[index], index + 1U,
                                   completed, manifest->total_size,
                                   operation_started);
            if (error != ESP_OK) {
                break;
            }
            completed += manifest->images[index].size;
        }
        if (error == ESP_OK) {
            set_download_progress(manifest->image_count,
                                  manifest->total_size,
                                  manifest->total_size);
            char preferred[SD_IMAGE_FILENAME_CAPACITY] = {0};
            if (sd_image_import_build_filename(
                    manifest->images[0].sha256, preferred,
                    sizeof(preferred))) {
                const esp_err_t preferred_error =
                    sd_image_store_select_preferred(preferred);
                if (preferred_error != ESP_OK) {
                    ESP_LOGW(TAG,
                             "could not select gallery image: %s",
                             esp_err_to_name(preferred_error));
                }
            }
        }
    }
    free(manifest);
    if (online_session_open) {
        (void)network_time_end_online_session();
    }

    if (error == ESP_OK) {
        set_state(GALLERY_DOWNLOAD_STATE_SUCCESS, ESP_OK);
        ESP_LOGI(TAG,
                 "starter gallery installed and published without restart");
        vTaskDelete(NULL);
        return;
    }
    set_state(GALLERY_DOWNLOAD_STATE_FAILED, error);
    ESP_LOGW(TAG, "gallery installation failed: %s",
             esp_err_to_name(error));
    vTaskDelete(NULL);
}

esp_err_t gallery_download_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t gallery_download_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    sd_image_status_t sd_status = {0};
    sd_image_store_get_status(&sd_status);
    if (sd_status.state == SD_IMAGE_STATE_NOT_INITIALIZED ||
        sd_status.state == SD_IMAGE_STATE_LOADING) {
        set_state(GALLERY_DOWNLOAD_STATE_FAILED, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    if (sd_status.card_capacity_bytes == 0U) {
        set_state(GALLERY_DOWNLOAD_STATE_FAILED, ESP_ERR_NOT_FOUND);
        return ESP_ERR_NOT_FOUND;
    }

    bool accepted = false;
    portENTER_CRITICAL(&s_lock);
    if (s_status.state == GALLERY_DOWNLOAD_STATE_IDLE) {
        memset(&s_status, 0, sizeof(s_status));
        s_status.state = GALLERY_DOWNLOAD_STATE_CONNECTING;
        s_status.last_error = ESP_OK;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(gallery_task, "gallery", GALLERY_TASK_STACK_BYTES,
                    NULL, GALLERY_TASK_PRIORITY, NULL) != pdPASS) {
        set_state(GALLERY_DOWNLOAD_STATE_FAILED, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t gallery_download_dismiss_result(void)
{
    bool dismissed = false;
    portENTER_CRITICAL(&s_lock);
    if (s_status.state == GALLERY_DOWNLOAD_STATE_FAILED ||
        s_status.state == GALLERY_DOWNLOAD_STATE_SUCCESS) {
        memset(&s_status, 0, sizeof(s_status));
        s_status.state = GALLERY_DOWNLOAD_STATE_IDLE;
        s_status.last_error = ESP_OK;
        dismissed = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return dismissed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

const char *gallery_download_error_detail(esp_err_t error)
{
    switch (error) {
    case ESP_ERR_NOT_FOUND:
        return "INSERT MICROSD OR CHECK SERVER";
    case ESP_ERR_INVALID_STATE:
        return "NETWORK OR STORAGE BUSY";
    case ESP_ERR_TIMEOUT:
        return "NO INTERNET";
    case ESP_ERR_INVALID_SIZE:
    case ESP_ERR_INVALID_RESPONSE:
        return "GALLERY DATA INVALID";
    case ESP_ERR_NO_MEM:
        return "NOT ENOUGH MEMORY";
    default:
        return "INSTALL FAILED";
    }
}
