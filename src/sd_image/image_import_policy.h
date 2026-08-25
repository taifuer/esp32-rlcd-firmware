#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SD_IMAGE_IMPORT_TARGET_CREATE = 0,
    SD_IMAGE_IMPORT_TARGET_DEDUPLICATE,
    SD_IMAGE_IMPORT_TARGET_COLLISION,
} sd_image_import_target_action_t;

typedef enum {
    SD_IMAGE_CACHE_PUBLISH_APPEND = 0,
    SD_IMAGE_CACHE_PUBLISH_SELECT_EXISTING,
    SD_IMAGE_CACHE_PUBLISH_REJECT,
} sd_image_cache_publish_action_t;

bool sd_image_import_build_filename(const uint8_t sha256[32],
                                    char *filename,
                                    size_t capacity);
sd_image_import_target_action_t sd_image_import_target_action(
    bool target_exists, bool content_matches);
sd_image_cache_publish_action_t sd_image_cache_publish_action(
    bool filename_cached, bool catalog_complete, size_t image_count,
    size_t capacity);

#ifdef __cplusplus
}
#endif
