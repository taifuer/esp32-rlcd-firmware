#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "image_import_policy.h"

int main(void)
{
    uint8_t digest[32];
    for (size_t index = 0U; index < sizeof(digest); ++index) {
        digest[index] = (uint8_t)index;
    }

    char filename[64];
    assert(sd_image_import_build_filename(digest, filename,
                                          sizeof(filename)));
    assert(strcmp(filename,
                  "image-000102030405060708090a0b0c0d0e0f.pbm") == 0);
    assert(!sd_image_import_build_filename(digest, filename, 42U));
    assert(!sd_image_import_build_filename(NULL, filename,
                                           sizeof(filename)));
    assert(!sd_image_import_build_filename(digest, NULL,
                                           sizeof(filename)));

    assert(sd_image_import_target_action(false, false) ==
           SD_IMAGE_IMPORT_TARGET_CREATE);
    assert(sd_image_import_target_action(false, true) ==
           SD_IMAGE_IMPORT_TARGET_CREATE);
    assert(sd_image_import_target_action(true, true) ==
           SD_IMAGE_IMPORT_TARGET_DEDUPLICATE);
    assert(sd_image_import_target_action(true, false) ==
           SD_IMAGE_IMPORT_TARGET_COLLISION);

    assert(sd_image_cache_publish_action(false, true, 0U, 32U) ==
           SD_IMAGE_CACHE_PUBLISH_APPEND);
    assert(sd_image_cache_publish_action(false, true, 31U, 32U) ==
           SD_IMAGE_CACHE_PUBLISH_APPEND);
    assert(sd_image_cache_publish_action(true, true, 32U, 32U) ==
           SD_IMAGE_CACHE_PUBLISH_SELECT_EXISTING);
    assert(sd_image_cache_publish_action(true, false, 32U, 32U) ==
           SD_IMAGE_CACHE_PUBLISH_SELECT_EXISTING);
    assert(sd_image_cache_publish_action(false, true, 32U, 32U) ==
           SD_IMAGE_CACHE_PUBLISH_REJECT);
    assert(sd_image_cache_publish_action(false, false, 1U, 32U) ==
           SD_IMAGE_CACHE_PUBLISH_REJECT);
    assert(sd_image_cache_publish_action(false, true, 0U, 0U) ==
           SD_IMAGE_CACHE_PUBLISH_REJECT);

    puts("SD image import policy tests passed");
    return 0;
}
