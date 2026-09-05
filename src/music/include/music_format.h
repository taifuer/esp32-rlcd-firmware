#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    MUSIC_MAX_TRACKS = 32,
    MUSIC_FILENAME_CAPACITY = 128,
    MUSIC_MAX_FILE_BYTES = 256 * 1024 * 1024,
    MUSIC_MAX_METADATA_BYTES = 1024 * 1024,
    MUSIC_MAX_FRAME_BYTES = 2048,
    MUSIC_PCM_BUFFER_BYTES = 4608,
};

typedef enum { MUSIC_FORMAT_NONE, MUSIC_FORMAT_MP3, MUSIC_FORMAT_WAV } music_format_t;
typedef struct {
    uint32_t sample_rate;
    uint16_t frame_bytes;
    uint16_t samples;
    uint8_t channels;
} music_mp3_frame_t;
typedef struct {
    music_format_t format;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t sample_rate;
    uint8_t channels;
} music_file_info_t;

music_format_t music_filename_format(const char *name);
bool music_mp3_parse_header(const uint8_t header[4], music_mp3_frame_t *frame);
/* Bounded validation; leaves the stream at its first playable byte. */
bool music_file_probe(FILE *file, uint32_t file_bytes, music_format_t format,
                      music_file_info_t *info);
