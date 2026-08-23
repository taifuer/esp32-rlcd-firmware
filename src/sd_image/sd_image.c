#include "sd_image.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "board_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "image_catalog.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_image";

enum {
    SD_IMAGE_TASK_STACK_BYTES = 8192,
    SD_IMAGE_TASK_PRIORITY = 1,
};

static const char *const SD_IMAGE_MOUNT_PATH = "/sdcard";
static const char *const SD_IMAGE_DIRECTORY =
    "/sdcard" SD_IMAGE_CARD_DIRECTORY;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static sd_image_status_t s_status = {
    .state = SD_IMAGE_STATE_NOT_INITIALIZED,
};
static uint8_t *s_bitmap;

static void publish_result(const sd_image_status_t *status,
                           uint8_t *bitmap)
{
    taskENTER_CRITICAL(&s_lock);
    s_status = *status;
    s_bitmap = bitmap;
    taskEXIT_CRITICAL(&s_lock);
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

static bool read_and_decode(const char *path,
                            mono_image_format_t expected_format,
                            uint8_t *bitmap,
                            sd_image_status_t *status)
{
    status->last_io_error = ESP_OK;
    status->last_decode_error = MONO_IMAGE_OK;

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        status->last_io_error = ESP_FAIL;
        return false;
    }

    struct stat file_info;
    if (fstat(fileno(file), &file_info) != 0 ||
        !S_ISREG(file_info.st_mode)) {
        status->last_io_error = ESP_ERR_INVALID_RESPONSE;
        fclose(file);
        return false;
    }
    if (file_info.st_size <= 0 ||
        (uint64_t)file_info.st_size > MONO_IMAGE_MAX_FILE_BYTES) {
        status->last_decode_error = file_info.st_size <= 0
                                        ? MONO_IMAGE_EMPTY
                                        : MONO_IMAGE_TOO_LARGE;
        fclose(file);
        return false;
    }

    const size_t source_size = (size_t)file_info.st_size;
    uint8_t *source = heap_caps_malloc(
        source_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (source == NULL) {
        status->state = SD_IMAGE_STATE_NO_MEMORY;
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
        status->last_io_error = ESP_FAIL;
        heap_caps_free(source);
        return false;
    }

    mono_image_info_t image_info = {0};
    const mono_image_result_t decode_result = mono_image_decode(
        source, source_size, expected_format, bitmap,
        MONO_IMAGE_BITMAP_BYTES, &image_info);
    heap_caps_free(source);
    status->last_decode_error = decode_result;
    if (decode_result != MONO_IMAGE_OK) {
        return false;
    }
    status->format = image_info.format;
    return true;
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
    uint8_t *bitmap = NULL;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 0,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
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

    esp_err_t error = esp_vfs_fat_sdmmc_mount(
        SD_IMAGE_MOUNT_PATH, &host, &slot, &mount_config, &card);
    if (error != ESP_OK) {
        result.last_io_error = error;
        ESP_LOGI(TAG, "microSD unavailable: %s", esp_err_to_name(error));
        publish_result(&result, NULL);
        vTaskDelete(NULL);
        return;
    }

    result.card_capacity_bytes =
        (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;
    ESP_LOGI(TAG, "microSD mounted: %llu MiB",
             (unsigned long long)(result.card_capacity_bytes /
                                  (1024ULL * 1024ULL)));

    sd_image_catalog_t *catalog = heap_caps_calloc(
        1U, sizeof(*catalog), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (catalog == NULL) {
        result.state = SD_IMAGE_STATE_NO_MEMORY;
        goto finish;
    }
    sd_image_catalog_init(catalog);

    DIR *directory = opendir(SD_IMAGE_DIRECTORY);
    if (directory == NULL) {
        result.state = errno == ENOENT
                           ? SD_IMAGE_STATE_DIRECTORY_MISSING
                           : SD_IMAGE_STATE_INTERNAL_ERROR;
        result.last_io_error = errno == ENOENT
                                   ? ESP_ERR_NOT_FOUND
                                   : ESP_FAIL;
        heap_caps_free(catalog);
        goto finish;
    }
    errno = 0;
    const struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (sd_image_catalog_add(catalog, entry->d_name) ==
            SD_IMAGE_CATALOG_TOO_MANY) {
            result.state = SD_IMAGE_STATE_TOO_MANY_FILES;
            break;
        }
    }
    if (errno != 0 && result.state != SD_IMAGE_STATE_TOO_MANY_FILES) {
        result.state = SD_IMAGE_STATE_INTERNAL_ERROR;
        result.last_io_error = ESP_FAIL;
    }
    closedir(directory);
    if (result.state == SD_IMAGE_STATE_TOO_MANY_FILES ||
        result.state == SD_IMAGE_STATE_INTERNAL_ERROR) {
        heap_caps_free(catalog);
        goto finish;
    }
    if (catalog->count == 0U) {
        result.state = SD_IMAGE_STATE_NO_SUPPORTED_FILE;
        heap_caps_free(catalog);
        goto finish;
    }
    sd_image_catalog_sort(catalog);

    bitmap = heap_caps_malloc(
        MONO_IMAGE_BITMAP_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (bitmap == NULL) {
        result.state = SD_IMAGE_STATE_NO_MEMORY;
        heap_caps_free(catalog);
        goto finish;
    }

    result.state = SD_IMAGE_STATE_NO_VALID_IMAGE;
    for (size_t index = 0U; index < catalog->count; ++index) {
        const char *filename = sd_image_catalog_at(catalog, index);
        char path[160];
        mono_image_format_t expected_format;
        if (filename == NULL ||
            !sd_image_catalog_expected_format(filename, &expected_format) ||
            !sd_image_catalog_build_path(SD_IMAGE_DIRECTORY, filename,
                                         path, sizeof(path))) {
            continue;
        }
        if (read_and_decode(path, expected_format, bitmap, &result)) {
            snprintf(result.filename, sizeof(result.filename), "%s",
                     filename);
            result.state = SD_IMAGE_STATE_READY;
            ESP_LOGI(TAG, "image ready: %s (%s)", filename,
                     expected_format == MONO_IMAGE_FORMAT_PBM_P4
                         ? "PBM P4"
                         : "1-bit BMP");
            break;
        }
        if (result.state == SD_IMAGE_STATE_NO_MEMORY) {
            break;
        }
        if (result.last_io_error != ESP_OK) {
            ESP_LOGW(TAG, "skipping unreadable image %s: %s", filename,
                     esp_err_to_name(result.last_io_error));
        } else {
            ESP_LOGW(TAG, "skipping invalid image %s: %s", filename,
                     mono_image_result_name(result.last_decode_error));
        }
    }
    heap_caps_free(catalog);

finish:
    error = esp_vfs_fat_sdcard_unmount(SD_IMAGE_MOUNT_PATH, card);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "microSD unmount failed: %s", esp_err_to_name(error));
        if (result.last_io_error == ESP_OK) {
            result.last_io_error = error;
        }
    }
    if (result.state != SD_IMAGE_STATE_READY) {
        heap_caps_free(bitmap);
        bitmap = NULL;
    }
    ESP_LOGI(TAG, "microSD image state: %s",
             sd_image_state_name(result.state));
    publish_result(&result, bitmap);
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

    if (xTaskCreate(sd_image_task, "sd_image", SD_IMAGE_TASK_STACK_BYTES,
                    NULL, SD_IMAGE_TASK_PRIORITY, NULL) != pdPASS) {
        sd_image_status_t failure = {
            .state = SD_IMAGE_STATE_INTERNAL_ERROR,
            .last_io_error = ESP_ERR_NO_MEM,
        };
        publish_result(&failure, NULL);
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

bool sd_image_store_get(sd_image_view_t *view)
{
    if (view == NULL) {
        return false;
    }
    memset(view, 0, sizeof(*view));
    taskENTER_CRITICAL(&s_lock);
    const bool ready = s_status.state == SD_IMAGE_STATE_READY &&
                       s_bitmap != NULL;
    if (ready) {
        view->bitmap = s_bitmap;
        view->size = MONO_IMAGE_BITMAP_BYTES;
        view->format = s_status.format;
        view->filename = s_status.filename;
    }
    taskEXIT_CRITICAL(&s_lock);
    return ready;
}
