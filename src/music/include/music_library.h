#pragma once

#include "esp_err.h"
#include "music_format.h"

#define MUSIC_CARD_DIRECTORY "/rlcd/music"
typedef struct {
    char filename[MUSIC_FILENAME_CAPACITY];
    uint32_t file_bytes;
    music_file_info_t info;
} music_track_t;
typedef struct {
    bool scanned;
    bool truncated;
    bool card_available;
    uint8_t count;
    uint32_t revision;
    esp_err_t error;
} music_library_status_t;

esp_err_t music_library_init(void);
void music_library_get_status(music_library_status_t *status);
bool music_library_track(size_t index, music_track_t *track);
bool music_library_find(const char *filename, size_t *index);
/* These operations require the caller to stop/quiesce music first. A shared
 * SD lease serializes all disk changes. Never overwrite an existing file. */
typedef struct music_import music_import_t;
esp_err_t music_import_begin(const char *filename, size_t bytes, music_import_t **transaction);
esp_err_t music_import_write(music_import_t *transaction, const void *data, size_t bytes);
/* Both commit and abort consume the caller-owned transaction. */
esp_err_t music_import_commit(music_import_t *transaction, bool (*checkpoint)(void *), void *context);
esp_err_t music_import_abort(music_import_t *transaction);
esp_err_t music_library_delete(const char *filename);
