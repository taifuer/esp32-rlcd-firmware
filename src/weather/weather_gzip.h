#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t deflate_offset;
    size_t deflate_length;
    uint32_t expected_crc32;
    uint32_t expected_size;
} weather_gzip_frame_t;

/* QWeather responses are gzip encoded. This parser validates one bounded gzip
 * member and identifies the raw DEFLATE payload without allocating memory. */
bool weather_gzip_parse_frame(const uint8_t *data, size_t length,
                              weather_gzip_frame_t *frame);

uint32_t weather_gzip_crc32(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
