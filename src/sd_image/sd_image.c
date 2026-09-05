#include "sd_image.h"
#include "sd_media.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "image_catalog.h"
#include "image_delete_policy.h"
#include "image_import_policy.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_image";

enum {
    SD_IMAGE_TASK_STACK_BYTES = 8192,
    SD_IMAGE_TASK_PRIORITY = 1,
    SD_IMAGE_DELETE_TASK_STACK_BYTES = 6144,
    SD_IMAGE_DELETE_TASK_PRIORITY = 2,
    SD_IMAGE_PATH_CAPACITY = 160,
    SD_IMAGE_IMPORT_TEMP_ATTEMPTS = 8,
    SD_IMAGE_IMPORT_FREE_SPACE_MARGIN = 4096,
};

static const char *const SD_IMAGE_MOUNT_PATH = "/sdcard";
static const char *const SD_IMAGE_ROOT_DIRECTORY = "/sdcard/rlcd";
static const char *const SD_IMAGE_DIRECTORY =
    "/sdcard" SD_IMAGE_CARD_DIRECTORY;
static const char *const SD_IMAGE_NVS_NAMESPACE = "rlcd_image";
static const char *const SD_IMAGE_NVS_PREFERRED_KEY = "preferred";

_Static_assert(SD_IMAGE_MAX_IMAGES == SD_IMAGE_CATALOG_MAX_FILES,
               "public image limit and catalog limit must match");

typedef struct cached_image {
    uint8_t *bitmap;
    mono_image_format_t format;
    char filename[SD_IMAGE_FILENAME_CAPACITY];
} cached_image_t;

struct sd_image_import {
    TaskHandle_t owner;
    sdmmc_card_t *card;
    FILE *file;
    size_t expected_size;
    size_t received_size;
    bool verify_sha256;
    bool digest_started;
    bool temp_exists;
    esp_err_t write_error;
    uint8_t expected_sha256[SD_IMAGE_SHA256_BYTES];
    mbedtls_sha256_context digest;
    char temp_path[SD_IMAGE_PATH_CAPACITY];
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static sd_image_status_t s_status = {
    .state = SD_IMAGE_STATE_NOT_INITIALIZED,
};
static sd_image_delete_status_t s_delete_status = {
    .state = SD_IMAGE_DELETE_STATE_IDLE,
    .last_error = ESP_OK,
};
/* The active table may move pointers, but published entries never move. */
static cached_image_t **s_images;
static size_t s_image_count;
static size_t s_selected_index;
static SemaphoreHandle_t s_io_mutex;
static sd_image_import_t *s_active_import;

static size_t preferred_image_index(cached_image_t *const *images,
                                    size_t image_count)
{
    if (images == NULL || image_count == 0U) {
        return 0U;
    }
    nvs_handle_t handle = 0;
    if (nvs_open(SD_IMAGE_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return 0U;
    }
    char preferred[SD_IMAGE_FILENAME_CAPACITY] = {0};
    size_t size = sizeof(preferred);
    const esp_err_t error = nvs_get_str(
        handle, SD_IMAGE_NVS_PREFERRED_KEY, preferred, &size);
    nvs_close(handle);
    if (error != ESP_OK || size == 0U ||
        preferred[sizeof(preferred) - 1U] != '\0') {
        return 0U;
    }
    for (size_t index = 0U; index < image_count; ++index) {
        if (strcmp(images[index]->filename, preferred) == 0) {
            return index;
        }
    }
    return 0U;
}

static void publish_result(const sd_image_status_t *status,
                           cached_image_t **images,
                           size_t image_count)
{
    sd_image_status_t published = *status;
    published.image_count = image_count;
    const size_t selected_index =
        preferred_image_index(images, image_count);
    published.selected_index = selected_index;
    if (image_count > 0U) {
        published.format = images[selected_index]->format;
        memcpy(published.filename, images[selected_index]->filename,
               sizeof(published.filename));
    }
    taskENTER_CRITICAL(&s_lock);
    published.revision = s_status.revision + 1U;
    s_status = published;
    s_images = images;
    s_image_count = image_count;
    s_selected_index = selected_index;
    taskEXIT_CRITICAL(&s_lock);
}

static void free_cache(cached_image_t **images, size_t count)
{
    if (images == NULL) {
        return;
    }
    for (size_t index = 0U; index < count; ++index) {
        if (images[index] != NULL) {
            heap_caps_free(images[index]->bitmap);
            heap_caps_free(images[index]);
        }
    }
    heap_caps_free(images);
}

const char *sd_image_state_name(sd_image_state_t state)
{
    switch (state) {
    case SD_IMAGE_STATE_NOT_INITIALIZED:
        return "not initialized";
    case SD_IMAGE_STATE_LOADING:
        return "loading";
    case SD_IMAGE_STATE_CARD_UNAVAILABLE:
        return "card unavailable";
    case SD_IMAGE_STATE_DIRECTORY_MISSING:
        return "directory missing";
    case SD_IMAGE_STATE_NO_SUPPORTED_FILE:
        return "no supported file";
    case SD_IMAGE_STATE_TOO_MANY_FILES:
        return "too many files";
    case SD_IMAGE_STATE_NO_VALID_IMAGE:
        return "no valid image";
    case SD_IMAGE_STATE_READY:
        return "ready";
    case SD_IMAGE_STATE_NO_MEMORY:
        return "no memory";
    case SD_IMAGE_STATE_INTERNAL_ERROR:
    default:
        return "internal error";
    }
}

static sdmmc_slot_config_t make_slot_config(void)
{
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SDMMC_CLK_GPIO;
    slot.cmd = BOARD_SDMMC_CMD_GPIO;
    slot.d0 = BOARD_SDMMC_D0_GPIO;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.d4 = GPIO_NUM_NC;
    slot.d5 = GPIO_NUM_NC;
    slot.d6 = GPIO_NUM_NC;
    slot.d7 = GPIO_NUM_NC;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.flags = 0;
    return slot;
}

static esp_err_t mount_card(sdmmc_card_t **card)
{
    if (card == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *card = NULL;
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 0,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = make_slot_config();
    return esp_vfs_fat_sdmmc_mount(SD_IMAGE_MOUNT_PATH, &host, &slot,
                                   &mount_config, card);
}

struct sd_media_read {
    TaskHandle_t owner;
    sdmmc_card_t *card;
};

esp_err_t sd_media_begin_read(sd_media_read_t **session, uint32_t wait_ms)
{
    if (session == NULL) return ESP_ERR_INVALID_ARG;
    *session = NULL;
    if (s_io_mutex == NULL ||
        xSemaphoreTake(s_io_mutex, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    sd_media_read_t *lease = calloc(1, sizeof(*lease));
    esp_err_t error = lease == NULL ? ESP_ERR_NO_MEM : mount_card(&lease->card);
    if (error != ESP_OK) {
        free(lease);
        xSemaphoreGive(s_io_mutex);
        return error;
    }
    lease->owner = xTaskGetCurrentTaskHandle();
    *session = lease;
    return ESP_OK;
}

esp_err_t sd_media_end_read(sd_media_read_t *session)
{
    if (session == NULL || session->owner != xTaskGetCurrentTaskHandle()) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t error = esp_vfs_fat_sdcard_unmount(
        SD_IMAGE_MOUNT_PATH, session->card);
    free(session);
    xSemaphoreGive(s_io_mutex);
    return error;
}

static bool read_and_decode(const char *path,
                            mono_image_format_t expected_format,
                            uint8_t *bitmap,
                            esp_err_t *io_error,
                            mono_image_result_t *decode_error)
{
    if (io_error != NULL) {
        *io_error = ESP_OK;
    }
    if (decode_error != NULL) {
        *decode_error = MONO_IMAGE_OK;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        if (io_error != NULL) {
            *io_error = ESP_FAIL;
        }
        return false;
    }

    struct stat file_info;
    if (fstat(fileno(file), &file_info) != 0 ||
        !S_ISREG(file_info.st_mode)) {
        if (io_error != NULL) {
            *io_error = ESP_ERR_INVALID_RESPONSE;
        }
        fclose(file);
        return false;
    }
    if (file_info.st_size <= 0 ||
        (uint64_t)file_info.st_size > MONO_IMAGE_MAX_FILE_BYTES) {
        if (decode_error != NULL) {
            *decode_error = file_info.st_size <= 0
                                ? MONO_IMAGE_EMPTY
                                : MONO_IMAGE_TOO_LARGE;
        }
        fclose(file);
        return false;
    }

    const size_t source_size = (size_t)file_info.st_size;
    uint8_t *source = heap_caps_malloc(
        source_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (source == NULL) {
        if (io_error != NULL) {
            *io_error = ESP_ERR_NO_MEM;
        }
        fclose(file);
        return false;
    }

    size_t total_read = 0U;
    while (total_read < source_size) {
        const size_t read_count = fread(
            source + total_read, 1U, source_size - total_read, file);
        if (read_count == 0U) {
            break;
        }
        total_read += read_count;
    }
    const bool read_ok = total_read == source_size && ferror(file) == 0;
    fclose(file);
    if (!read_ok) {
        if (io_error != NULL) {
            *io_error = ESP_FAIL;
        }
        heap_caps_free(source);
        return false;
    }

    mono_image_info_t image_info = {0};
    const mono_image_result_t result = mono_image_decode(
        source, source_size, expected_format, bitmap,
        MONO_IMAGE_BITMAP_BYTES, &image_info);
    heap_caps_free(source);
    if (decode_error != NULL) {
        *decode_error = result;
    }
    return result == MONO_IMAGE_OK;
}

static void sort_cache(cached_image_t **images, size_t count)
{
    for (size_t index = 1U; index < count; ++index) {
        cached_image_t *const current = images[index];
        size_t position = index;
        while (position > 0U &&
               sd_image_catalog_compare_names(
                   current->filename,
                   images[position - 1U]->filename) < 0) {
            images[position] = images[position - 1U];
            --position;
        }
        images[position] = current;
    }
}

static cached_image_t *create_cache_entry(uint8_t *bitmap,
                                          mono_image_format_t format,
                                          const char *filename)
{
    if (bitmap == NULL || filename == NULL) {
        return NULL;
    }
    cached_image_t *entry = heap_caps_calloc(
        1U, sizeof(*entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (entry == NULL) {
        return NULL;
    }
    entry->bitmap = bitmap;
    entry->format = format;
    const size_t filename_length = strnlen(
        filename, sizeof(entry->filename) - 1U);
    memcpy(entry->filename, filename, filename_length);
    entry->filename[filename_length] = '\0';
    return entry;
}

static void free_cache_entry(cached_image_t *entry)
{
    if (entry != NULL) {
        heap_caps_free(entry->bitmap);
        heap_caps_free(entry);
    }
}

static size_t largest_cache_entry(cached_image_t *const *images,
                                  size_t count)
{
    size_t largest = 0U;
    for (size_t index = 1U; index < count; ++index) {
        if (sd_image_catalog_compare_names(images[index]->filename,
                                           images[largest]->filename) > 0) {
            largest = index;
        }
    }
    return largest;
}

static void sd_image_task(void *argument)
{
    (void)argument;
    sd_image_status_t result = {
        .state = SD_IMAGE_STATE_CARD_UNAVAILABLE,
        .last_io_error = ESP_OK,
        .last_decode_error = MONO_IMAGE_OK,
    };
    sdmmc_card_t *card = NULL;
    cached_image_t **images = NULL;
    size_t image_count = 0U;

    if (xSemaphoreTake(s_io_mutex, portMAX_DELAY) != pdTRUE) {
        result.state = SD_IMAGE_STATE_INTERNAL_ERROR;
        result.last_io_error = ESP_ERR_INVALID_STATE;
        publish_result(&result, NULL, 0U);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t error = mount_card(&card);
    if (error != ESP_OK) {
        result.last_io_error = error;
        ESP_LOGI(TAG, "microSD unavailable: %s", esp_err_to_name(error));
        publish_result(&result, NULL, 0U);
        xSemaphoreGive(s_io_mutex);
        vTaskDelete(NULL);
        return;
    }

    result.card_capacity_bytes =
        (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;
    ESP_LOGI(TAG, "microSD mounted: %llu MiB",
             (unsigned long long)(result.card_capacity_bytes /
                                  (1024ULL * 1024ULL)));

    images = heap_caps_calloc(SD_IMAGE_MAX_IMAGES, sizeof(*images),
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (images == NULL) {
        result.state = SD_IMAGE_STATE_NO_MEMORY;
        result.last_io_error = ESP_ERR_NO_MEM;
        goto finish;
    }

    DIR *directory = opendir(SD_IMAGE_DIRECTORY);
    if (directory == NULL) {
        result.state = errno == ENOENT
                           ? SD_IMAGE_STATE_DIRECTORY_MISSING
                           : SD_IMAGE_STATE_INTERNAL_ERROR;
        result.last_io_error = errno == ENOENT
                                   ? ESP_ERR_NOT_FOUND
                                   : ESP_FAIL;
        goto finish;
    }

    bool saw_supported_file = false;
    while (true) {
        errno = 0;
        const struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                result.state = SD_IMAGE_STATE_INTERNAL_ERROR;
                result.last_io_error = ESP_FAIL;
                /* Keep any images already decoded available for display, but
                 * mark the catalog incomplete so later write transactions do
                 * not infer free capacity from a partial directory scan. */
                result.catalog_truncated = true;
            }
            break;
        }

        mono_image_format_t expected_format;
        char path[SD_IMAGE_PATH_CAPACITY];
        if (!sd_image_catalog_expected_format(entry->d_name,
                                              &expected_format) ||
            !sd_image_catalog_build_path(SD_IMAGE_DIRECTORY,
                                         entry->d_name, path,
                                         sizeof(path))) {
            continue;
        }
        saw_supported_file = true;

        uint8_t *bitmap = heap_caps_malloc(
            MONO_IMAGE_BITMAP_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (bitmap == NULL) {
            result.last_io_error = ESP_ERR_NO_MEM;
            result.catalog_truncated = true;
            if (image_count == 0U) {
                result.state = SD_IMAGE_STATE_NO_MEMORY;
            }
            break;
        }

        esp_err_t io_error = ESP_OK;
        mono_image_result_t decode_error = MONO_IMAGE_OK;
        if (!read_and_decode(path, expected_format, bitmap, &io_error,
                             &decode_error)) {
            heap_caps_free(bitmap);
            result.last_io_error = io_error;
            result.last_decode_error = decode_error;
            if (io_error == ESP_ERR_NO_MEM) {
                result.catalog_truncated = true;
                if (image_count == 0U) {
                    result.state = SD_IMAGE_STATE_NO_MEMORY;
                }
                break;
            }
            if (io_error != ESP_OK) {
                ESP_LOGW(TAG, "skipping unreadable image %s: %s",
                         entry->d_name, esp_err_to_name(io_error));
            } else {
                ESP_LOGW(TAG, "skipping invalid image %s: %s",
                         entry->d_name,
                         mono_image_result_name(decode_error));
            }
            continue;
        }

        cached_image_t *cache_entry = create_cache_entry(
            bitmap, expected_format, entry->d_name);
        if (cache_entry == NULL) {
            heap_caps_free(bitmap);
            result.last_io_error = ESP_ERR_NO_MEM;
            result.catalog_truncated = true;
            if (image_count == 0U) {
                result.state = SD_IMAGE_STATE_NO_MEMORY;
            }
            break;
        }

        if (image_count < SD_IMAGE_MAX_IMAGES) {
            images[image_count] = cache_entry;
            ++image_count;
        } else {
            result.catalog_truncated = true;
            const size_t largest = largest_cache_entry(images,
                                                        image_count);
            if (sd_image_catalog_compare_names(
                    entry->d_name, images[largest]->filename) < 0) {
                free_cache_entry(images[largest]);
                images[largest] = cache_entry;
            } else {
                free_cache_entry(cache_entry);
            }
        }
        result.last_io_error = ESP_OK;
        result.last_decode_error = MONO_IMAGE_OK;
    }
    closedir(directory);

    if (result.state != SD_IMAGE_STATE_INTERNAL_ERROR &&
        result.state != SD_IMAGE_STATE_NO_MEMORY) {
        if (image_count > 0U) {
            sort_cache(images, image_count);
            result.state = SD_IMAGE_STATE_READY;
        } else {
            result.state = saw_supported_file
                               ? SD_IMAGE_STATE_NO_VALID_IMAGE
                               : SD_IMAGE_STATE_NO_SUPPORTED_FILE;
        }
    } else if (image_count > 0U) {
        sort_cache(images, image_count);
        result.state = SD_IMAGE_STATE_READY;
    }

finish:
    error = esp_vfs_fat_sdcard_unmount(SD_IMAGE_MOUNT_PATH, card);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "microSD unmount failed: %s", esp_err_to_name(error));
        if (result.last_io_error == ESP_OK) {
            result.last_io_error = error;
        }
    }
    const bool keep_empty_runtime_cache =
        images != NULL &&
        (result.state == SD_IMAGE_STATE_DIRECTORY_MISSING ||
         result.state == SD_IMAGE_STATE_NO_SUPPORTED_FILE ||
         result.state == SD_IMAGE_STATE_NO_VALID_IMAGE);
    if (result.state != SD_IMAGE_STATE_READY &&
        !keep_empty_runtime_cache) {
        free_cache(images, image_count);
        images = NULL;
        image_count = 0U;
    }
    ESP_LOGI(TAG, "microSD image state: %s, images=%u%s",
             sd_image_state_name(result.state), (unsigned)image_count,
             result.catalog_truncated ? " (truncated)" : "");
    publish_result(&result, images, image_count);
    xSemaphoreGive(s_io_mutex);
    vTaskDelete(NULL);
}

esp_err_t sd_image_store_init(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_status.state != SD_IMAGE_STATE_NOT_INITIALIZED) {
        taskEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = SD_IMAGE_STATE_LOADING;
    taskEXIT_CRITICAL(&s_lock);

    s_io_mutex = xSemaphoreCreateMutex();
    if (s_io_mutex == NULL) {
        sd_image_status_t failure = {
            .state = SD_IMAGE_STATE_INTERNAL_ERROR,
            .last_io_error = ESP_ERR_NO_MEM,
        };
        publish_result(&failure, NULL, 0U);
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(sd_image_task, "sd_image", SD_IMAGE_TASK_STACK_BYTES,
                    NULL, SD_IMAGE_TASK_PRIORITY, NULL) != pdPASS) {
        vSemaphoreDelete(s_io_mutex);
        s_io_mutex = NULL;
        sd_image_status_t failure = {
            .state = SD_IMAGE_STATE_INTERNAL_ERROR,
            .last_io_error = ESP_ERR_NO_MEM,
        };
        publish_result(&failure, NULL, 0U);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void sd_image_store_get_status(sd_image_status_t *status)
{
    if (status == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_lock);
}

size_t sd_image_store_count(void)
{
    taskENTER_CRITICAL(&s_lock);
    const size_t count = s_image_count;
    taskEXIT_CRITICAL(&s_lock);
    return count;
}

bool sd_image_store_catalog_snapshot(sd_image_catalog_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    taskENTER_CRITICAL(&s_lock);
    const bool valid = s_image_count <= SD_IMAGE_MAX_IMAGES &&
                       (s_image_count == 0U || s_images != NULL);
    if (valid) {
        snapshot->count = s_image_count;
        snapshot->selected_index = s_image_count > 0U &&
                                           s_selected_index < s_image_count
                                       ? s_selected_index
                                       : 0U;
        for (size_t index = 0U; index < s_image_count; ++index) {
            memcpy(snapshot->filenames[index], s_images[index]->filename,
                   sizeof(snapshot->filenames[index]));
        }
    }
    taskEXIT_CRITICAL(&s_lock);
    return valid;
}

bool sd_image_store_filename_at(size_t index, char *filename,
                                size_t capacity)
{
    if (filename == NULL || capacity < SD_IMAGE_FILENAME_CAPACITY) {
        return false;
    }
    filename[0] = '\0';
    taskENTER_CRITICAL(&s_lock);
    const bool valid = s_images != NULL && index < s_image_count;
    if (valid) {
        memcpy(filename, s_images[index]->filename,
               SD_IMAGE_FILENAME_CAPACITY);
    }
    taskEXIT_CRITICAL(&s_lock);
    return valid;
}

bool sd_image_store_copy_selected(uint8_t *bitmap, size_t capacity,
                                  size_t *selected_index,
                                  size_t *image_count)
{
    if (bitmap == NULL || capacity < MONO_IMAGE_BITMAP_BYTES ||
        selected_index == NULL || image_count == NULL) {
        return false;
    }
    *selected_index = 0U;
    *image_count = 0U;
    taskENTER_CRITICAL(&s_lock);
    const bool valid = s_status.state == SD_IMAGE_STATE_READY &&
                       s_images != NULL &&
                       s_selected_index < s_image_count;
    if (valid) {
        memcpy(bitmap, s_images[s_selected_index]->bitmap,
               MONO_IMAGE_BITMAP_BYTES);
        *selected_index = s_selected_index;
        *image_count = s_image_count;
    }
    taskEXIT_CRITICAL(&s_lock);
    return valid;
}

esp_err_t sd_image_store_copy_bitmap(const char *filename,
                                     uint8_t *bitmap, size_t capacity)
{
    mono_image_format_t ignored_format;
    char ignored_path[SD_IMAGE_PATH_CAPACITY];
    if (filename == NULL || bitmap == NULL ||
        capacity < MONO_IMAGE_BITMAP_BYTES ||
        !sd_image_catalog_expected_format(filename, &ignored_format) ||
        !sd_image_catalog_build_path(SD_IMAGE_DIRECTORY, filename,
                                     ignored_path, sizeof(ignored_path))) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;
    taskENTER_CRITICAL(&s_lock);
    if (s_status.state == SD_IMAGE_STATE_READY && s_images != NULL) {
        for (size_t index = 0U; index < s_image_count; ++index) {
            if (strcmp(s_images[index]->filename, filename) != 0) {
                continue;
            }
            memcpy(bitmap, s_images[index]->bitmap,
                   MONO_IMAGE_BITMAP_BYTES);
            result = ESP_OK;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_lock);
    return result;
}

size_t sd_image_store_selected_index(void)
{
    taskENTER_CRITICAL(&s_lock);
    const size_t index = s_selected_index;
    taskEXIT_CRITICAL(&s_lock);
    return index;
}

static esp_err_t write_preferred_filename(const char *filename)
{
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(SD_IMAGE_NVS_NAMESPACE, NVS_READWRITE,
                               &handle);
    if (error != ESP_OK) {
        return error;
    }

    bool changed = true;
    if (filename != NULL) {
        error = nvs_set_str(handle, SD_IMAGE_NVS_PREFERRED_KEY, filename);
    } else {
        error = nvs_erase_key(handle, SD_IMAGE_NVS_PREFERRED_KEY);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            error = ESP_OK;
            changed = false;
        }
    }
    if (error == ESP_OK && changed) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

static size_t runtime_cache_filename_index_locked(const char *filename);

esp_err_t sd_image_store_select_preferred(const char *filename)
{
    mono_image_format_t ignored_format;
    char ignored_path[SD_IMAGE_PATH_CAPACITY];
    if (filename == NULL ||
        !sd_image_catalog_expected_format(filename, &ignored_format) ||
        !sd_image_catalog_build_path(SD_IMAGE_DIRECTORY, filename,
                                     ignored_path, sizeof(ignored_path))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_io_mutex == NULL ||
        xSemaphoreTake(s_io_mutex, 0U) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t selected_index = SIZE_MAX;
    taskENTER_CRITICAL(&s_lock);
    if (s_status.state == SD_IMAGE_STATE_READY) {
        selected_index = runtime_cache_filename_index_locked(filename);
    }
    taskEXIT_CRITICAL(&s_lock);
    if (selected_index == SIZE_MAX) {
        xSemaphoreGive(s_io_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    const esp_err_t error = write_preferred_filename(filename);
    if (error == ESP_OK) {
        taskENTER_CRITICAL(&s_lock);
        const bool changed = s_selected_index != selected_index;
        s_selected_index = selected_index;
        s_status.selected_index = selected_index;
        s_status.format = s_images[selected_index]->format;
        memcpy(s_status.filename, s_images[selected_index]->filename,
               sizeof(s_status.filename));
        if (changed) {
            ++s_status.revision;
        }
        taskEXIT_CRITICAL(&s_lock);
    }
    xSemaphoreGive(s_io_mutex);
    return error;
}

static size_t runtime_cache_filename_index_locked(const char *filename)
{
    if (s_images == NULL || filename == NULL) {
        return SIZE_MAX;
    }
    for (size_t index = 0U; index < s_image_count; ++index) {
        if (strcmp(s_images[index]->filename, filename) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

static esp_err_t file_remove_error(void)
{
    return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
}

esp_err_t sd_image_store_delete(const char *filename)
{
    mono_image_format_t ignored_format;
    char path[SD_IMAGE_PATH_CAPACITY];
    if (filename == NULL ||
        !sd_image_catalog_expected_format(filename, &ignored_format) ||
        !sd_image_catalog_build_path(SD_IMAGE_DIRECTORY, filename, path,
                                     sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_io_mutex == NULL ||
        xSemaphoreTake(s_io_mutex, 0U) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t deleted_index = SIZE_MAX;
    taskENTER_CRITICAL(&s_lock);
    if (s_status.state == SD_IMAGE_STATE_READY) {
        deleted_index = runtime_cache_filename_index_locked(filename);
    }
    taskEXIT_CRITICAL(&s_lock);
    if (deleted_index == SIZE_MAX) {
        xSemaphoreGive(s_io_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    sdmmc_card_t *card = NULL;
    esp_err_t error = mount_card(&card);
    if (error == ESP_OK) {
        struct stat info;
        if (stat(path, &info) != 0) {
            error = file_remove_error();
        } else if (!S_ISREG(info.st_mode)) {
            error = ESP_ERR_INVALID_RESPONSE;
        } else if (remove(path) != 0) {
            error = file_remove_error();
        }
    }

    esp_err_t unmount_error = ESP_OK;
    if (card != NULL) {
        unmount_error = esp_vfs_fat_sdcard_unmount(
            SD_IMAGE_MOUNT_PATH, card);
        if (unmount_error != ESP_OK) {
            ESP_LOGW(TAG, "microSD unmount after delete failed: %s",
                     esp_err_to_name(unmount_error));
        }
    }
    if (error != ESP_OK) {
        xSemaphoreGive(s_io_mutex);
        return error;
    }

    char replacement_preferred[SD_IMAGE_FILENAME_CAPACITY] = {0};
    cached_image_t *deleted_entry = NULL;
    size_t remaining_count = 0U;
    taskENTER_CRITICAL(&s_lock);
    /* Imports and deletes share s_io_mutex, so this exact entry cannot have
     * disappeared between validation and the successful card transaction. */
    deleted_index = runtime_cache_filename_index_locked(filename);
    const size_t previous_count = s_image_count;
    const size_t previous_selected = s_selected_index;
    size_t next_selected = 0U;
    const bool selection_valid =
        deleted_index != SIZE_MAX &&
        sd_image_delete_next_selected(previous_count, previous_selected,
                                      deleted_index, &next_selected);
    if (selection_valid) {
        deleted_entry = s_images[deleted_index];
        const size_t following = previous_count - deleted_index - 1U;
        if (following > 0U) {
            memmove(&s_images[deleted_index],
                    &s_images[deleted_index + 1U],
                    following * sizeof(*s_images));
        }
        s_images[previous_count - 1U] = NULL;
        s_image_count = previous_count - 1U;
        remaining_count = s_image_count;
        s_selected_index = next_selected;
        s_status.image_count = s_image_count;
        s_status.selected_index = next_selected;
        s_status.last_io_error = unmount_error;
        s_status.last_decode_error = MONO_IMAGE_OK;
        if (s_image_count > 0U) {
            s_status.state = SD_IMAGE_STATE_READY;
            s_status.format = s_images[next_selected]->format;
            memcpy(s_status.filename,
                   s_images[next_selected]->filename,
                   sizeof(s_status.filename));
            memcpy(replacement_preferred,
                   s_images[next_selected]->filename,
                   sizeof(replacement_preferred));
        } else {
            s_status.state = SD_IMAGE_STATE_NO_VALID_IMAGE;
            s_status.format = MONO_IMAGE_FORMAT_PBM_P4;
            memset(s_status.filename, 0, sizeof(s_status.filename));
        }
        ++s_status.revision;
    }
    taskEXIT_CRITICAL(&s_lock);

    if (!selection_valid) {
        /* The file is already gone, so keep the catalog conservative and make
         * the internal inconsistency visible to the caller. */
        xSemaphoreGive(s_io_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    free_cache_entry(deleted_entry);

    const esp_err_t preferred_error = remaining_count == 0U
        ? write_preferred_filename(NULL)
        : write_preferred_filename(replacement_preferred);
    if (preferred_error != ESP_OK) {
        /* Disk and runtime deletion already committed. A stale preference is
         * harmless on next boot (loading falls back to the first image), so do
         * not misreport a completed deletion as retryable. */
        ESP_LOGW(TAG, "failed to update preferred image after delete: %s",
                 esp_err_to_name(preferred_error));
    }

    xSemaphoreGive(s_io_mutex);
    return ESP_OK;
}

static void publish_delete_status(sd_image_delete_state_t state,
                                  esp_err_t error,
                                  const char *filename)
{
    char safe_filename[SD_IMAGE_FILENAME_CAPACITY] = {0};
    if (filename != NULL) {
        (void)snprintf(safe_filename, sizeof(safe_filename), "%s",
                       filename);
    }
    taskENTER_CRITICAL(&s_lock);
    s_delete_status.state = state;
    s_delete_status.last_error = error;
    memcpy(s_delete_status.filename, safe_filename,
           sizeof(s_delete_status.filename));
    ++s_delete_status.revision;
    if (s_delete_status.revision == 0U) {
        s_delete_status.revision = 1U;
    }
    taskEXIT_CRITICAL(&s_lock);
}

static void sd_image_delete_task(void *argument)
{
    (void)argument;
    char filename[SD_IMAGE_FILENAME_CAPACITY] = {0};
    taskENTER_CRITICAL(&s_lock);
    memcpy(filename, s_delete_status.filename, sizeof(filename));
    taskEXIT_CRITICAL(&s_lock);

    const esp_err_t error = sd_image_store_delete(filename);
    publish_delete_status(error == ESP_OK
                              ? SD_IMAGE_DELETE_STATE_SUCCESS
                              : SD_IMAGE_DELETE_STATE_FAILED,
                          error, filename);
    vTaskDelete(NULL);
}

esp_err_t sd_image_store_request_delete(const char *filename)
{
    mono_image_format_t ignored_format;
    char ignored_path[SD_IMAGE_PATH_CAPACITY];
    char safe_filename[SD_IMAGE_FILENAME_CAPACITY] = {0};
    if (filename == NULL ||
        !sd_image_catalog_expected_format(filename, &ignored_format) ||
        !sd_image_catalog_build_path(SD_IMAGE_DIRECTORY, filename,
                                     ignored_path,
                                     sizeof(ignored_path))) {
        return ESP_ERR_INVALID_ARG;
    }
    const int copied = snprintf(safe_filename, sizeof(safe_filename),
                                "%s", filename);
    if (copied < 1 || (size_t)copied >= sizeof(safe_filename)) {
        return ESP_ERR_INVALID_ARG;
    }

    bool request_valid = false;
    taskENTER_CRITICAL(&s_lock);
    if (s_delete_status.state == SD_IMAGE_DELETE_STATE_IDLE &&
        s_status.state == SD_IMAGE_STATE_READY && s_images != NULL &&
        runtime_cache_filename_index_locked(safe_filename) != SIZE_MAX) {
        s_delete_status.state = SD_IMAGE_DELETE_STATE_DELETING;
        s_delete_status.last_error = ESP_OK;
        memcpy(s_delete_status.filename, safe_filename,
               sizeof(s_delete_status.filename));
        s_delete_status.filename[sizeof(s_delete_status.filename) - 1U] =
            '\0';
        ++s_delete_status.revision;
        if (s_delete_status.revision == 0U) {
            s_delete_status.revision = 1U;
        }
        request_valid = true;
    }
    taskEXIT_CRITICAL(&s_lock);
    if (!request_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskCreate(sd_image_delete_task, "sd_img_delete",
                    SD_IMAGE_DELETE_TASK_STACK_BYTES, NULL,
                    SD_IMAGE_DELETE_TASK_PRIORITY, NULL) != pdPASS) {
        publish_delete_status(SD_IMAGE_DELETE_STATE_FAILED,
                              ESP_ERR_NO_MEM, safe_filename);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void sd_image_store_get_delete_status(sd_image_delete_status_t *status)
{
    if (status == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    *status = s_delete_status;
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t sd_image_store_dismiss_delete_result(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_delete_status.state != SD_IMAGE_DELETE_STATE_SUCCESS &&
        s_delete_status.state != SD_IMAGE_DELETE_STATE_FAILED) {
        taskEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_delete_status.state = SD_IMAGE_DELETE_STATE_IDLE;
    s_delete_status.last_error = ESP_OK;
    memset(s_delete_status.filename, 0,
           sizeof(s_delete_status.filename));
    ++s_delete_status.revision;
    if (s_delete_status.revision == 0U) {
        s_delete_status.revision = 1U;
    }
    taskEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static esp_err_t ensure_runtime_cache_table(void)
{
    taskENTER_CRITICAL(&s_lock);
    const bool already_available = s_images != NULL;
    taskEXIT_CRITICAL(&s_lock);
    if (already_available) {
        return ESP_OK;
    }

    cached_image_t **candidate = heap_caps_calloc(
        SD_IMAGE_MAX_IMAGES, sizeof(*candidate),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (candidate == NULL) {
        return ESP_ERR_NO_MEM;
    }

    bool installed = false;
    taskENTER_CRITICAL(&s_lock);
    if (s_images == NULL) {
        s_images = candidate;
        installed = true;
    }
    taskEXIT_CRITICAL(&s_lock);
    if (!installed) {
        heap_caps_free(candidate);
    }
    return ESP_OK;
}

static esp_err_t prepare_runtime_cache_publish(const char *filename)
{
    esp_err_t error = ensure_runtime_cache_table();
    if (error != ESP_OK) {
        return error;
    }

    taskENTER_CRITICAL(&s_lock);
    const bool cached =
        runtime_cache_filename_index_locked(filename) != SIZE_MAX;
    const sd_image_cache_publish_action_t action =
        sd_image_cache_publish_action(cached, !s_status.catalog_truncated,
                                      s_image_count,
                                      SD_IMAGE_MAX_IMAGES);
    taskEXIT_CRITICAL(&s_lock);
    return action == SD_IMAGE_CACHE_PUBLISH_REJECT
               ? ESP_ERR_INVALID_SIZE
               : ESP_OK;
}

/* Consumes candidate on every return path. Import transactions are serialized
 * by s_io_mutex, so a successful prepare cannot lose its insertion slot before
 * this function publishes the validated image. */
static esp_err_t publish_runtime_entry(
    cached_image_t *candidate, uint64_t card_capacity_bytes)
{
    if (candidate == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *const filename = candidate->filename;

    esp_err_t result = ESP_OK;
    bool bitmap_published = false;
    taskENTER_CRITICAL(&s_lock);
    const size_t existing_index =
        runtime_cache_filename_index_locked(filename);
    const sd_image_cache_publish_action_t action =
        sd_image_cache_publish_action(existing_index != SIZE_MAX,
                                      !s_status.catalog_truncated,
                                      s_image_count,
                                      SD_IMAGE_MAX_IMAGES);
    size_t selected_index = existing_index;
    if (action == SD_IMAGE_CACHE_PUBLISH_APPEND) {
        selected_index = 0U;
        while (selected_index < s_image_count &&
               sd_image_catalog_compare_names(
                   s_images[selected_index]->filename,
                   candidate->filename) < 0) {
            ++selected_index;
        }
        const size_t following = s_image_count - selected_index;
        if (following > 0U) {
            memmove(&s_images[selected_index + 1U],
                    &s_images[selected_index],
                    following * sizeof(*s_images));
        }
        s_images[selected_index] = candidate;
        ++s_image_count;
        bitmap_published = true;
    } else if (action == SD_IMAGE_CACHE_PUBLISH_REJECT) {
        result = ESP_ERR_INVALID_SIZE;
    }

    if (result == ESP_OK) {
        const bool changed =
            action == SD_IMAGE_CACHE_PUBLISH_APPEND ||
            s_status.state != SD_IMAGE_STATE_READY ||
            s_selected_index != selected_index;
        s_selected_index = selected_index;
        s_status.state = SD_IMAGE_STATE_READY;
        s_status.last_io_error = ESP_OK;
        s_status.last_decode_error = MONO_IMAGE_OK;
        s_status.format = s_images[selected_index]->format;
        s_status.image_count = s_image_count;
        s_status.selected_index = selected_index;
        if (card_capacity_bytes != 0U) {
            s_status.card_capacity_bytes = card_capacity_bytes;
        }
        memcpy(s_status.filename, s_images[selected_index]->filename,
               sizeof(s_status.filename));
        if (changed) {
            ++s_status.revision;
        }
    }
    taskEXIT_CRITICAL(&s_lock);

    if (!bitmap_published) {
        free_cache_entry(candidate);
    }
    return result;
}

static esp_err_t ensure_directory(const char *path)
{
    struct stat info;
    if (stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (errno != ENOENT) {
        return ESP_FAIL;
    }
    if (mkdir(path, 0755) == 0) {
        return ESP_OK;
    }
    return errno == EEXIST ? ESP_ERR_INVALID_STATE : ESP_FAIL;
}

static bool import_is_owned(const sd_image_import_t *import)
{
    if (import == NULL) {
        return false;
    }
    taskENTER_CRITICAL(&s_lock);
    const bool active = s_active_import == import;
    taskEXIT_CRITICAL(&s_lock);
    return active && import->owner == xTaskGetCurrentTaskHandle();
}

static esp_err_t create_temp_file(sd_image_import_t *import)
{
    for (unsigned int attempt = 0U;
         attempt < SD_IMAGE_IMPORT_TEMP_ATTEMPTS; ++attempt) {
        const uint32_t suffix = esp_random();
        const int written = snprintf(
            import->temp_path, sizeof(import->temp_path),
            "%s/.incoming-%08lx.part", SD_IMAGE_DIRECTORY,
            (unsigned long)suffix);
        if (written < 0 || (size_t)written >= sizeof(import->temp_path)) {
            return ESP_ERR_INVALID_SIZE;
        }

        struct stat info;
        if (stat(import->temp_path, &info) == 0) {
            continue;
        }
        if (errno != ENOENT) {
            return ESP_FAIL;
        }
        import->file = fopen(import->temp_path, "wb");
        if (import->file != NULL) {
            import->temp_exists = true;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t finish_import(sd_image_import_t *import,
                               bool remove_temp,
                               esp_err_t result)
{
    if (import->file != NULL) {
        if (fclose(import->file) != 0 && result == ESP_OK) {
            result = ESP_FAIL;
        }
        import->file = NULL;
    }
    if (remove_temp && import->temp_exists) {
        if (remove(import->temp_path) != 0 && errno != ENOENT &&
            result == ESP_OK) {
            result = ESP_FAIL;
        }
        import->temp_exists = false;
    }
    if (import->card != NULL) {
        const esp_err_t unmount_error = esp_vfs_fat_sdcard_unmount(
            SD_IMAGE_MOUNT_PATH, import->card);
        if (unmount_error != ESP_OK && result == ESP_OK) {
            result = unmount_error;
        }
        import->card = NULL;
    }
    if (import->digest_started) {
        mbedtls_sha256_free(&import->digest);
        import->digest_started = false;
    }

    taskENTER_CRITICAL(&s_lock);
    if (s_active_import == import) {
        s_active_import = NULL;
    }
    taskEXIT_CRITICAL(&s_lock);
    free(import);
    xSemaphoreGive(s_io_mutex);
    return result;
}

esp_err_t sd_image_import_begin(
    const sd_image_import_options_t *options,
    sd_image_import_t **import)
{
    if (import == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *import = NULL;
    if (options == NULL || options->expected_size == 0U ||
        options->expected_size > MONO_IMAGE_MAX_FILE_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_io_mutex == NULL ||
        xSemaphoreTake(s_io_mutex, 0U) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    sd_image_import_t *transaction = calloc(1U, sizeof(*transaction));
    if (transaction == NULL) {
        xSemaphoreGive(s_io_mutex);
        return ESP_ERR_NO_MEM;
    }
    transaction->owner = xTaskGetCurrentTaskHandle();
    transaction->expected_size = options->expected_size;
    transaction->verify_sha256 = options->verify_sha256;
    memcpy(transaction->expected_sha256, options->expected_sha256,
           sizeof(transaction->expected_sha256));
    mbedtls_sha256_init(&transaction->digest);
    transaction->digest_started = true;
    if (mbedtls_sha256_starts(&transaction->digest, 0) != 0) {
        return finish_import(transaction, true, ESP_FAIL);
    }

    esp_err_t error = mount_card(&transaction->card);
    if (error == ESP_OK) {
        error = ensure_directory(SD_IMAGE_ROOT_DIRECTORY);
    }
    if (error == ESP_OK) {
        error = ensure_directory(SD_IMAGE_DIRECTORY);
    }
    if (error == ESP_OK) {
        uint64_t total_bytes = 0U;
        uint64_t free_bytes = 0U;
        error = esp_vfs_fat_info(SD_IMAGE_MOUNT_PATH, &total_bytes,
                                 &free_bytes);
        const uint64_t required_bytes =
            (uint64_t)options->expected_size +
            SD_IMAGE_IMPORT_FREE_SPACE_MARGIN;
        if (error == ESP_OK && free_bytes < required_bytes) {
            error = ESP_ERR_NO_MEM;
        }
    }
    if (error == ESP_OK) {
        error = create_temp_file(transaction);
    }
    if (error != ESP_OK) {
        return finish_import(transaction, true, error);
    }

    taskENTER_CRITICAL(&s_lock);
    s_active_import = transaction;
    taskEXIT_CRITICAL(&s_lock);
    *import = transaction;
    return ESP_OK;
}

esp_err_t sd_image_import_write(sd_image_import_t *import,
                                const void *data,
                                size_t size)
{
    if (!import_is_owned(import)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (size > 0U && data == NULL) {
        import->write_error = ESP_ERR_INVALID_ARG;
        return import->write_error;
    }
    if (import->write_error != ESP_OK) {
        return import->write_error;
    }
    if (size > import->expected_size - import->received_size) {
        import->write_error = ESP_ERR_INVALID_SIZE;
        return import->write_error;
    }
    if (size == 0U) {
        return ESP_OK;
    }

    const size_t written = fwrite(data, 1U, size, import->file);
    if (written != size) {
        import->write_error = ESP_FAIL;
        return import->write_error;
    }
    if (mbedtls_sha256_update(&import->digest, data, size) != 0) {
        import->write_error = ESP_FAIL;
        return import->write_error;
    }
    import->received_size += size;
    return ESP_OK;
}

static bool file_matches_digest(const char *path,
                                size_t expected_size,
                                const uint8_t expected_sha256[32])
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    struct stat info;
    if (fstat(fileno(file), &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || (size_t)info.st_size != expected_size) {
        fclose(file);
        return false;
    }

    mbedtls_sha256_context digest;
    mbedtls_sha256_init(&digest);
    bool matches = mbedtls_sha256_starts(&digest, 0) == 0;
    uint8_t buffer[512];
    size_t total = 0U;
    while (matches && total < expected_size) {
        const size_t requested = expected_size - total < sizeof(buffer)
                                     ? expected_size - total
                                     : sizeof(buffer);
        const size_t count = fread(buffer, 1U, requested, file);
        if (count == 0U ||
            mbedtls_sha256_update(&digest, buffer, count) != 0) {
            matches = false;
            break;
        }
        total += count;
    }
    uint8_t actual_sha256[SD_IMAGE_SHA256_BYTES] = {0};
    if (!matches || total != expected_size || ferror(file) != 0 ||
        mbedtls_sha256_finish(&digest, actual_sha256) != 0) {
        matches = false;
    } else {
        matches = memcmp(actual_sha256, expected_sha256,
                         sizeof(actual_sha256)) == 0;
    }
    memset(actual_sha256, 0, sizeof(actual_sha256));
    mbedtls_sha256_free(&digest);
    fclose(file);
    return matches;
}

esp_err_t sd_image_import_commit(sd_image_import_t *import,
                                 sd_image_import_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (!import_is_owned(import)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = import->write_error;
    uint8_t sha256[SD_IMAGE_SHA256_BYTES] = {0};
    if (error == ESP_OK && import->received_size != import->expected_size) {
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK &&
        mbedtls_sha256_finish(&import->digest, sha256) != 0) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK && import->verify_sha256 &&
        memcmp(sha256, import->expected_sha256, sizeof(sha256)) != 0) {
        error = ESP_ERR_INVALID_CRC;
    }
    if (error == ESP_OK && fflush(import->file) != 0) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK && fsync(fileno(import->file)) != 0) {
        error = ESP_FAIL;
    }
    if (fclose(import->file) != 0 && error == ESP_OK) {
        error = ESP_FAIL;
    }
    import->file = NULL;

    uint8_t *validation_bitmap = NULL;
    if (error == ESP_OK) {
        validation_bitmap = heap_caps_malloc(
            MONO_IMAGE_BITMAP_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (validation_bitmap == NULL) {
            error = ESP_ERR_NO_MEM;
        }
    }
    if (error == ESP_OK) {
        esp_err_t io_error = ESP_OK;
        mono_image_result_t decode_error = MONO_IMAGE_OK;
        if (!read_and_decode(import->temp_path,
                             MONO_IMAGE_FORMAT_PBM_P4,
                             validation_bitmap, &io_error,
                             &decode_error)) {
            error = io_error != ESP_OK ? io_error
                                       : ESP_ERR_INVALID_RESPONSE;
        }
    }
    char filename[SD_IMAGE_FILENAME_CAPACITY] = {0};
    char final_path[SD_IMAGE_PATH_CAPACITY] = {0};
    if (error == ESP_OK &&
        (!sd_image_import_build_filename(sha256, filename,
                                         sizeof(filename)) ||
         !sd_image_catalog_build_path(SD_IMAGE_DIRECTORY, filename,
                                      final_path, sizeof(final_path)))) {
        error = ESP_ERR_INVALID_SIZE;
    }

    bool target_exists = false;
    bool content_matches = false;
    if (error == ESP_OK) {
        struct stat info;
        if (stat(final_path, &info) == 0) {
            target_exists = true;
            content_matches = file_matches_digest(
                final_path, import->expected_size, sha256);
        } else if (errno != ENOENT) {
            error = ESP_FAIL;
        }
    }

    cached_image_t *validation_entry = NULL;
    bool duplicate = false;
    if (error == ESP_OK) {
        const sd_image_import_target_action_t action =
            sd_image_import_target_action(target_exists, content_matches);
        if (action != SD_IMAGE_IMPORT_TARGET_COLLISION) {
            error = prepare_runtime_cache_publish(filename);
        }
        if (error == ESP_OK &&
            action != SD_IMAGE_IMPORT_TARGET_COLLISION) {
            validation_entry = create_cache_entry(
                validation_bitmap, MONO_IMAGE_FORMAT_PBM_P4, filename);
            if (validation_entry == NULL) {
                error = ESP_ERR_NO_MEM;
            } else {
                validation_bitmap = NULL;
            }
        }
        if (error == ESP_OK) {
            switch (action) {
            case SD_IMAGE_IMPORT_TARGET_CREATE:
                if (rename(import->temp_path, final_path) != 0) {
                    error = ESP_FAIL;
                } else {
                    import->temp_exists = false;
                }
                break;
            case SD_IMAGE_IMPORT_TARGET_DEDUPLICATE:
                duplicate = true;
                if (remove(import->temp_path) != 0) {
                    error = ESP_FAIL;
                } else {
                    import->temp_exists = false;
                }
                break;
            case SD_IMAGE_IMPORT_TARGET_COLLISION:
            default:
                error = ESP_ERR_INVALID_CRC;
                break;
            }
        }
        if (error == ESP_OK) {
            const uint64_t card_capacity_bytes =
                import->card == NULL
                    ? 0U
                    : (uint64_t)import->card->csd.capacity *
                          (uint64_t)import->card->csd.sector_size;
            error = publish_runtime_entry(validation_entry,
                                          card_capacity_bytes);
            validation_entry = NULL;
        }
    }

    if (error == ESP_OK && result != NULL) {
        result->file_size = import->expected_size;
        result->duplicate = duplicate;
        result->reload_required = false;
        memcpy(result->sha256, sha256, sizeof(result->sha256));
        snprintf(result->filename, sizeof(result->filename), "%s",
                 filename);
    }
    free_cache_entry(validation_entry);
    heap_caps_free(validation_bitmap);
    memset(sha256, 0, sizeof(sha256));
    return finish_import(import, error != ESP_OK, error);
}

esp_err_t sd_image_import_abort(sd_image_import_t *import)
{
    if (!import_is_owned(import)) {
        return ESP_ERR_INVALID_STATE;
    }
    return finish_import(import, true, ESP_OK);
}
