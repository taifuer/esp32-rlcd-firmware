#include "music_library.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_image.h"
#include "sd_media.h"

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
