#include "image_import_policy.h"

#include <stdio.h>

enum {
    DIGEST_FILENAME_BYTES = 16,
};

bool sd_image_import_build_filename(const uint8_t sha256[32],
                                    char *filename,
                                    size_t capacity)
{
    if (sha256 == NULL || filename == NULL || capacity == 0U) {
        return false;
    }

    size_t position = 0U;
    const int prefix_length = snprintf(filename, capacity, "image-");
    if (prefix_length < 0 || (size_t)prefix_length >= capacity) {
        return false;
    }
    position = (size_t)prefix_length;
    for (size_t index = 0U; index < DIGEST_FILENAME_BYTES; ++index) {
        const int written = snprintf(filename + position,
                                     capacity - position, "%02x",
                                     sha256[index]);
        if (written != 2 || position + 2U >= capacity) {
            return false;
        }
        position += 2U;
    }
    const int suffix_length = snprintf(filename + position,
                                       capacity - position, ".pbm");
    return suffix_length == 4 && position + 4U < capacity;
}

sd_image_import_target_action_t sd_image_import_target_action(
    bool target_exists, bool content_matches)
{
    if (!target_exists) {
        return SD_IMAGE_IMPORT_TARGET_CREATE;
    }
    return content_matches ? SD_IMAGE_IMPORT_TARGET_DEDUPLICATE
                           : SD_IMAGE_IMPORT_TARGET_COLLISION;
}

sd_image_cache_publish_action_t sd_image_cache_publish_action(
    bool filename_cached, bool catalog_complete, size_t image_count,
    size_t capacity)
{
    if (filename_cached) {
        return SD_IMAGE_CACHE_PUBLISH_SELECT_EXISTING;
    }
    if (!catalog_complete || capacity == 0U || image_count >= capacity) {
        return SD_IMAGE_CACHE_PUBLISH_REJECT;
    }
    return SD_IMAGE_CACHE_PUBLISH_APPEND;
}
