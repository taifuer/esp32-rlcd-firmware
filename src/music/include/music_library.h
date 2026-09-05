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
    uint8_t count;
    esp_err_t error;
} music_library_status_t;

esp_err_t music_library_init(void);
void music_library_get_status(music_library_status_t *status);
bool music_library_track(size_t index, music_track_t *track);
