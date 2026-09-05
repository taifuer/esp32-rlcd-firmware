#include "music_library.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_image.h"
#include "sd_media.h"
#include "mbedtls/sha256.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static music_track_t *s_tracks;
static music_library_status_t s_status;
static bool s_started;

static void scan_task(void *arg)
{
    (void)arg;
    music_library_status_t result = {.scanned = true};
    sd_media_read_t *lease = NULL;
    DIR *directory = NULL;
    music_track_t *tracks = NULL;
    /* The existing image cache owns the first startup mount. Never delay the
     * UI or repeatedly probe an absent card. */
    for (unsigned attempts = 0; attempts < 200U; ++attempts) {
        sd_image_status_t images;
        sd_image_store_get_status(&images);
        if (images.state != SD_IMAGE_STATE_LOADING) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    result.error = sd_media_begin_read(&lease, 0);
    if (result.error != ESP_OK) goto done;
    result.card_available = true;
    directory = opendir(SD_MEDIA_MOUNT_PATH MUSIC_CARD_DIRECTORY);
    if (directory == NULL) goto done;
    tracks = heap_caps_calloc(MUSIC_MAX_TRACKS, sizeof(*tracks), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (tracks == NULL) { result.error = ESP_ERR_NO_MEM; goto done; }
    struct dirent *entry;
    unsigned visited = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (++visited > 256U) { result.truncated = true; break; }
        const music_format_t format = music_filename_format(entry->d_name);
        if (format == MUSIC_FORMAT_NONE) continue;
        char path[192];
        const int length = snprintf(path, sizeof(path), "%s%s/%s", SD_MEDIA_MOUNT_PATH,
                                    MUSIC_CARD_DIRECTORY, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 44 ||
            (uint64_t)st.st_size > MUSIC_MAX_FILE_BYTES) continue;
        FILE *file = fopen(path, "rb");
        if (file == NULL) continue;
        music_track_t track = {.file_bytes = (uint32_t)st.st_size};
        const bool valid = music_file_probe(file, track.file_bytes, format, &track.info);
        fclose(file);
        if (!valid) continue;
        memcpy(track.filename, entry->d_name, strlen(entry->d_name) + 1U);
        /* Keep the first 32 names in a deterministic, bytewise UTF-8 order. */
        size_t position = 0;
        while (position < result.count && strcmp(tracks[position].filename, track.filename) < 0) ++position;
        if (result.count == MUSIC_MAX_TRACKS) result.truncated = true;
        else ++result.count;
        if (position < MUSIC_MAX_TRACKS) {
            for (size_t j = result.count - 1U; j > position; --j) tracks[j] = tracks[j - 1U];
            tracks[position] = track;
        }
        vTaskDelay(1);
    }
done:
    if (directory != NULL) closedir(directory);
    if (lease != NULL) {
        const esp_err_t error = sd_media_end_read(lease);
        if (result.error == ESP_OK) result.error = error;
    }
    if (result.count == 0U) {
        heap_caps_free(tracks);
        tracks = NULL;
    }
    taskENTER_CRITICAL(&s_lock);
    s_tracks = tracks;
    result.revision = s_status.revision + 1U;
    s_status = result;
    taskEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

esp_err_t music_library_init(void)
{
    if (s_started) return ESP_ERR_INVALID_STATE;
    s_started = true;
    if (xTaskCreate(scan_task, "music_scan", 6144, NULL, 1, NULL) != pdPASS) {
        taskENTER_CRITICAL(&s_lock);
        s_status = (music_library_status_t){.scanned = true, .error = ESP_ERR_NO_MEM};
        taskEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void music_library_get_status(music_library_status_t *status)
{
    if (status == NULL) return;
    taskENTER_CRITICAL(&s_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_lock);
}

bool music_library_track(size_t index, music_track_t *track)
{
    if (track == NULL) return false;
    taskENTER_CRITICAL(&s_lock);
    const bool available = index < s_status.count && s_tracks != NULL;
    if (available) *track = s_tracks[index];
    taskEXIT_CRITICAL(&s_lock);
    return available;
}

bool music_library_find(const char *filename, size_t *index)
{
    if (music_filename_format(filename) == MUSIC_FORMAT_NONE) return false;
    bool found = false;
    taskENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < s_status.count; ++i) {
        if (strcmp(s_tracks[i].filename, filename) == 0) {
            if (index != NULL) *index = i;
            found = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_lock);
    return found;
}

#define MUSIC_DIRECTORY SD_MEDIA_MOUNT_PATH MUSIC_CARD_DIRECTORY
#define MUSIC_PATH_CAPACITY (sizeof(MUSIC_DIRECTORY) + MUSIC_FILENAME_CAPACITY + 1U)
struct music_import {
    TaskHandle_t owner;
    sd_media_write_t *lease;
    FILE *file;
    size_t received;
    esp_err_t error;
    music_track_t track;
    char temp_path[MUSIC_PATH_CAPACITY];
    bool temp_exists;
    mbedtls_sha256_context digest;
};

static bool build_path(const char *name, char path[MUSIC_PATH_CAPACITY])
{
    if (music_filename_format(name) == MUSIC_FORMAT_NONE) return false;
    const int n = snprintf(path, MUSIC_PATH_CAPACITY, "%s/%s", MUSIC_DIRECTORY, name);
    return n > 0 && (size_t)n < MUSIC_PATH_CAPACITY;
}

static esp_err_t ensure_music_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    return errno == ENOENT && mkdir(path, 0755) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t finish_import(music_import_t *transaction, esp_err_t result)
{
    if (transaction->file != NULL && fclose(transaction->file) != 0 && result == ESP_OK) result = ESP_FAIL;
    if (transaction->temp_exists && remove(transaction->temp_path) != 0 && errno != ENOENT) {
        ESP_LOGW("music", "could not remove incomplete upload");
        if (result == ESP_OK) result = ESP_FAIL;
    }
    if (transaction->lease != NULL) {
        const esp_err_t unmount = sd_media_end_write(transaction->lease);
        if (unmount != ESP_OK) ESP_LOGW("music", "SD unmount: %s", esp_err_to_name(unmount));
        /* A completed rename/catalog publication cannot be undone by a late
         * unmount error. Do not falsely report the song as uncommitted. */
    }
    mbedtls_sha256_free(&transaction->digest);
    free(transaction);
    return result;
}

esp_err_t music_import_begin(const char *filename, size_t bytes, music_import_t **transaction)
{
    if (transaction == NULL) return ESP_ERR_INVALID_ARG;
    *transaction = NULL;
    char path[MUSIC_PATH_CAPACITY];
    if (!build_path(filename, path) || bytes < 44U || bytes > MUSIC_UPLOAD_MAX_BYTES) return ESP_ERR_INVALID_ARG;
    music_library_status_t status;
    music_library_get_status(&status);
    if (!status.scanned || !status.card_available) return ESP_ERR_INVALID_STATE;
    if (status.count >= MUSIC_MAX_TRACKS || status.truncated) return ESP_ERR_INVALID_SIZE;
    music_import_t *item = calloc(1, sizeof(*item));
    if (item == NULL) return ESP_ERR_NO_MEM;
    item->owner = xTaskGetCurrentTaskHandle();
    item->track.file_bytes = (uint32_t)bytes;
    strcpy(item->track.filename, filename);
    mbedtls_sha256_init(&item->digest);
    esp_err_t error = sd_media_begin_write(&item->lease, 0);
    if (error != ESP_OK) return finish_import(item, error);
    music_library_get_status(&status);
    if (status.count >= MUSIC_MAX_TRACKS || status.truncated) return finish_import(item, ESP_ERR_INVALID_SIZE);
    struct stat st;
    if (stat(path, &st) == 0) return finish_import(item, ESP_ERR_NOT_ALLOWED);
    if (errno != ENOENT) return finish_import(item, ESP_FAIL);
    error = ensure_music_directory(SD_MEDIA_MOUNT_PATH "/rlcd");
    if (error == ESP_OK) error = ensure_music_directory(MUSIC_DIRECTORY);
    uint64_t total_bytes = 0, free_bytes = 0;
    if (error == ESP_OK) error = esp_vfs_fat_info(SD_MEDIA_MOUNT_PATH, &total_bytes, &free_bytes);
    if (error == ESP_OK && free_bytes < bytes + 65536U) error = ESP_ERR_NO_MEM;
    if (error != ESP_OK) return finish_import(item, error);
    /* Allocate the publication slot before writing or renaming anything. The
     * table stays allocated; readers only ever receive copies under s_lock. */
    if (s_tracks == NULL) {
        music_track_t *tracks = heap_caps_calloc(MUSIC_MAX_TRACKS, sizeof(*tracks), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (tracks == NULL) return finish_import(item, ESP_ERR_NO_MEM);
        taskENTER_CRITICAL(&s_lock);
        s_tracks = tracks;
        taskEXIT_CRITICAL(&s_lock);
    }
    if (mbedtls_sha256_starts(&item->digest, 0) != 0) return finish_import(item, ESP_FAIL);
    for (unsigned i = 0; i < 8U; ++i) {
        snprintf(item->temp_path, sizeof(item->temp_path), "%s/.incoming-%08lx.part",
                 MUSIC_DIRECTORY, (unsigned long)esp_random());
        const int fd = open(item->temp_path, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return finish_import(item, ESP_FAIL);
        }
        item->temp_exists = true;
        item->file = fdopen(fd, "w+b");
        if (item->file == NULL) { close(fd); return finish_import(item, ESP_FAIL); }
        *transaction = item;
        return ESP_OK;
    }
    return finish_import(item, ESP_FAIL);
}

esp_err_t music_import_write(music_import_t *item, const void *data, size_t bytes)
{
    if (item == NULL || item->owner != xTaskGetCurrentTaskHandle()) return ESP_ERR_INVALID_STATE;
    if (item->error != ESP_OK) return item->error;
    if ((bytes > 0U && data == NULL) || bytes > item->track.file_bytes - item->received) return item->error = ESP_ERR_INVALID_SIZE;
    if (bytes == 0U) return ESP_OK;
    if (fwrite(data, 1, bytes, item->file) != bytes || mbedtls_sha256_update(&item->digest, data, bytes) != 0) return item->error = ESP_FAIL;
    item->received += bytes;
    return ESP_OK;
}

esp_err_t music_import_abort(music_import_t *item)
{
    if (item == NULL || item->owner != xTaskGetCurrentTaskHandle()) return ESP_ERR_INVALID_STATE;
    return finish_import(item, ESP_OK);
}

esp_err_t music_import_commit(music_import_t *item, bool (*checkpoint)(void *), void *context)
{
    if (item == NULL || item->owner != xTaskGetCurrentTaskHandle()) return ESP_ERR_INVALID_STATE;
    if (item->error != ESP_OK) return finish_import(item, item->error);
    if (item->received != item->track.file_bytes) return finish_import(item, ESP_ERR_INVALID_SIZE);
    if (fflush(item->file) != 0 || fsync(fileno(item->file)) != 0) return finish_import(item, ESP_FAIL);
    uint8_t expected[32], actual[32];
    if (mbedtls_sha256_finish(&item->digest, expected) != 0 ||
        !music_file_validate(item->file, item->track.file_bytes, music_filename_format(item->track.filename),
                             &item->track.info, checkpoint, context)) return finish_import(item, ESP_ERR_INVALID_RESPONSE);
    /* Re-read the complete file, comparing its digest with the received bytes.
     * Fixed-size heap buffer avoids large HTTP task stack use or whole-song RAM. */
    uint8_t *buffer = malloc(4096);
    if (buffer == NULL) return finish_import(item, ESP_ERR_NO_MEM);
    esp_err_t error = ESP_OK;
    if (fseek(item->file, 0, SEEK_SET) != 0 || mbedtls_sha256_starts(&item->digest, 0) != 0) error = ESP_FAIL;
    size_t remaining = item->track.file_bytes;
    while (error == ESP_OK && remaining > 0U) {
        if (checkpoint != NULL && !checkpoint(context)) { error = ESP_ERR_TIMEOUT; break; }
        const size_t chunk = remaining < 4096U ? remaining : 4096U;
        if (fread(buffer, 1, chunk, item->file) != chunk || mbedtls_sha256_update(&item->digest, buffer, chunk) != 0) error = ESP_FAIL;
        remaining -= chunk;
    }
    free(buffer);
    if (error == ESP_OK && (mbedtls_sha256_finish(&item->digest, actual) != 0 || memcmp(expected, actual, 32) != 0)) error = ESP_ERR_INVALID_CRC;
    if (fclose(item->file) != 0 && error == ESP_OK) error = ESP_FAIL;
    item->file = NULL;
    if (error != ESP_OK) return finish_import(item, error);
    char path[MUSIC_PATH_CAPACITY];
    if (!build_path(item->track.filename, path)) return finish_import(item, ESP_ERR_INVALID_ARG);
    struct stat st;
    if (stat(path, &st) == 0) return finish_import(item, ESP_ERR_NOT_ALLOWED);
    if (errno != ENOENT) return finish_import(item, ESP_FAIL);
    if (checkpoint != NULL && !checkpoint(context)) return finish_import(item, ESP_ERR_TIMEOUT);
    if (rename(item->temp_path, path) != 0) return finish_import(item, ESP_FAIL);
    item->temp_exists = false;
    taskENTER_CRITICAL(&s_lock);
    size_t at = 0;
    while (at < s_status.count && strcmp(s_tracks[at].filename, item->track.filename) < 0) ++at;
    memmove(&s_tracks[at + 1U], &s_tracks[at], (s_status.count - at) * sizeof(*s_tracks));
    s_tracks[at] = item->track;
    ++s_status.count;
    ++s_status.revision;
    taskEXIT_CRITICAL(&s_lock);
    return finish_import(item, ESP_OK);
}

esp_err_t music_library_delete(const char *filename)
{
    char path[MUSIC_PATH_CAPACITY];
    size_t index;
    if (!build_path(filename, path)) return ESP_ERR_INVALID_ARG;
    if (!music_library_find(filename, &index)) return ESP_ERR_NOT_FOUND;
    sd_media_write_t *lease = NULL;
    esp_err_t error = sd_media_begin_write(&lease, 0);
    if (error != ESP_OK) return error;
    if (!music_library_find(filename, &index)) {
        (void)sd_media_end_write(lease);
        return ESP_ERR_NOT_FOUND;
    }
    music_track_t track;
    struct stat st;
    /* Match the exact cataloged file. Physical card replacement is not a
     * supported way to change content while the device is running. */
    if (!music_library_track(index, &track) || strcmp(track.filename, filename) != 0 ||
        stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != track.file_bytes) error = ESP_ERR_INVALID_STATE;
    else if (remove(path) != 0) error = ESP_FAIL;
    else {
        taskENTER_CRITICAL(&s_lock);
        --s_status.count;
        memmove(&s_tracks[index], &s_tracks[index + 1U], (s_status.count - index) * sizeof(*s_tracks));
        memset(&s_tracks[s_status.count], 0, sizeof(*s_tracks));
        ++s_status.revision;
        taskEXIT_CRITICAL(&s_lock);
    }
    const esp_err_t unmount = sd_media_end_write(lease);
    if (unmount != ESP_OK) ESP_LOGW("music", "SD unmount: %s", esp_err_to_name(unmount));
    return error;
}
