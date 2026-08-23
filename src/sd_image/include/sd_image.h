#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "monochrome_image.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_IMAGE_CARD_DIRECTORY "/rlcd/images"
#define SD_IMAGE_FILENAME_CAPACITY 64U

typedef enum {
    SD_IMAGE_STATE_NOT_INITIALIZED = 0,
    SD_IMAGE_STATE_LOADING,
    SD_IMAGE_STATE_CARD_UNAVAILABLE,
    SD_IMAGE_STATE_DIRECTORY_MISSING,
    SD_IMAGE_STATE_NO_SUPPORTED_FILE,
    SD_IMAGE_STATE_TOO_MANY_FILES,
    SD_IMAGE_STATE_NO_VALID_IMAGE,
    SD_IMAGE_STATE_READY,
    SD_IMAGE_STATE_NO_MEMORY,
    SD_IMAGE_STATE_INTERNAL_ERROR,
} sd_image_state_t;

typedef struct {
    sd_image_state_t state;
    esp_err_t last_io_error;
    mono_image_result_t last_decode_error;
    mono_image_format_t format;
    uint64_t card_capacity_bytes;
    char filename[SD_IMAGE_FILENAME_CAPACITY];
} sd_image_status_t;

typedef struct {
    const uint8_t *bitmap;
    size_t size;
    mono_image_format_t format;
    const char *filename;
} sd_image_view_t;

esp_err_t sd_image_store_init(void);
void sd_image_store_get_status(sd_image_status_t *status);
/*
 * The returned view points to immutable storage owned by this component. It
 * remains valid for the lifetime of the application; callers must not free or
 * modify the bitmap or filename.
 */
bool sd_image_store_get(sd_image_view_t *view);
const char *sd_image_state_name(sd_image_state_t state);

#ifdef __cplusplus
}
#endif
