#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "monochrome_image.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_IMAGE_CATALOG_MAX_FILES 32U
#define SD_IMAGE_CATALOG_NAME_CAPACITY 64U

typedef enum {
    SD_IMAGE_CATALOG_IGNORED = 0,
    SD_IMAGE_CATALOG_ADDED,
    SD_IMAGE_CATALOG_TOO_MANY,
} sd_image_catalog_add_result_t;

typedef struct {
    size_t count;
    bool overflowed;
    char names[SD_IMAGE_CATALOG_MAX_FILES]
              [SD_IMAGE_CATALOG_NAME_CAPACITY];
} sd_image_catalog_t;

void sd_image_catalog_init(sd_image_catalog_t *catalog);
sd_image_catalog_add_result_t sd_image_catalog_add(
    sd_image_catalog_t *catalog, const char *name);
void sd_image_catalog_sort(sd_image_catalog_t *catalog);
const char *sd_image_catalog_at(const sd_image_catalog_t *catalog,
                                size_t index);
bool sd_image_catalog_expected_format(const char *name,
                                      mono_image_format_t *format);
bool sd_image_catalog_build_path(const char *base_path,
                                 const char *name,
                                 char *path,
                                 size_t path_capacity);

#ifdef __cplusplus
}
#endif
