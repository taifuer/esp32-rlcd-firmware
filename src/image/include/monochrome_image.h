#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MONO_IMAGE_WIDTH 400U
#define MONO_IMAGE_HEIGHT 300U
#define MONO_IMAGE_ROW_BYTES 50U
#define MONO_IMAGE_BITMAP_BYTES 15000U
#define MONO_IMAGE_MAX_FILE_BYTES 16384U
#define MONO_IMAGE_MAX_PBM_HEADER_BYTES 1024U

/*
 * Decoded pixels are stored from top to bottom, MSB first within each byte.
 * A set bit represents white on the current ST7305 display; a clear bit
 * represents black. This is deliberately different from PBM P4, where a set
 * bit represents black.
 */
typedef enum {
    MONO_IMAGE_FORMAT_PBM_P4 = 0,
    MONO_IMAGE_FORMAT_BMP_1BPP,
} mono_image_format_t;

typedef enum {
    MONO_IMAGE_OK = 0,
    MONO_IMAGE_INVALID_ARGUMENT,
    MONO_IMAGE_EMPTY,
    MONO_IMAGE_TOO_LARGE,
    MONO_IMAGE_UNSUPPORTED_FORMAT,
    MONO_IMAGE_FORMAT_MISMATCH,
    MONO_IMAGE_INVALID_HEADER,
    MONO_IMAGE_WRONG_DIMENSIONS,
    MONO_IMAGE_UNSUPPORTED_ENCODING,
    MONO_IMAGE_INVALID_PALETTE,
    MONO_IMAGE_TRUNCATED,
    MONO_IMAGE_TRAILING_DATA,
    MONO_IMAGE_OUTPUT_TOO_SMALL,
} mono_image_result_t;

typedef struct {
    mono_image_format_t format;
    bool source_top_down;
} mono_image_info_t;

/*
 * Decode one strictly bounded 400 x 300 monochrome image. expected_format is
 * normally derived from the filename extension. info may be NULL.
 *
 * The source and bitmap ranges must not overlap. The bitmap is not modified
 * unless the complete input has first passed validation.
 */
mono_image_result_t mono_image_decode(
    const uint8_t *source,
    size_t source_size,
    mono_image_format_t expected_format,
    uint8_t *bitmap,
    size_t bitmap_size,
    mono_image_info_t *info);

const char *mono_image_result_name(mono_image_result_t result);

#ifdef __cplusplus
}
#endif
