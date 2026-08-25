#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "image_catalog.h"

int main(void)
{
    sd_image_catalog_t catalog;
    sd_image_catalog_init(&catalog);
    assert(catalog.count == 0U);
    assert(!catalog.overflowed);
    assert(sd_image_catalog_add(&catalog, ".hidden.pbm") ==
           SD_IMAGE_CATALOG_IGNORED);
    assert(sd_image_catalog_add(&catalog, "photo.png") ==
           SD_IMAGE_CATALOG_IGNORED);
    assert(sd_image_catalog_add(&catalog, "中文.pbm") ==
           SD_IMAGE_CATALOG_IGNORED);
    assert(sd_image_catalog_add(&catalog, "zebra.BMP") ==
           SD_IMAGE_CATALOG_ADDED);
    assert(sd_image_catalog_add(&catalog, "Alpha.pbm") ==
           SD_IMAGE_CATALOG_ADDED);
    assert(sd_image_catalog_add(&catalog, "alpha.BMP") ==
           SD_IMAGE_CATALOG_ADDED);
    sd_image_catalog_sort(&catalog);
    assert(strcmp(sd_image_catalog_at(&catalog, 0U), "alpha.BMP") == 0);
    assert(strcmp(sd_image_catalog_at(&catalog, 1U), "Alpha.pbm") == 0);
    assert(strcmp(sd_image_catalog_at(&catalog, 2U), "zebra.BMP") == 0);
    assert(sd_image_catalog_at(&catalog, 3U) == NULL);

    mono_image_format_t format;
    assert(sd_image_catalog_expected_format("test.PBM", &format));
    assert(format == MONO_IMAGE_FORMAT_PBM_P4);
    assert(sd_image_catalog_expected_format("test.bmp", &format));
    assert(format == MONO_IMAGE_FORMAT_BMP_1BPP);
    assert(!sd_image_catalog_expected_format("test.png", &format));
    assert(!sd_image_catalog_expected_format(NULL, &format));

    char path[96];
    assert(sd_image_catalog_build_path("/sdcard/rlcd/images",
                                       "test.pbm", path,
                                       sizeof(path)));
    assert(strcmp(path, "/sdcard/rlcd/images/test.pbm") == 0);
    assert(!sd_image_catalog_build_path("/sdcard/rlcd/images",
                                        "test.pbm", path, 8U));
    assert(!sd_image_catalog_build_path("/sdcard/rlcd/images",
                                        "../test.pbm", path,
                                        sizeof(path)));

    assert(sd_image_catalog_compare_names("Alpha.pbm", "alpha.pbm") < 0);
    assert(sd_image_catalog_compare_names("z.pbm", "Alpha.pbm") > 0);
    assert(sd_image_catalog_compare_names(NULL, NULL) == 0);
    assert(sd_image_catalog_compare_names(NULL, "a.pbm") < 0);

    sd_image_catalog_init(&catalog);
    char name[32];
    for (size_t index = 0U; index < SD_IMAGE_CATALOG_MAX_FILES; ++index) {
        snprintf(name, sizeof(name), "image-%02u.pbm", (unsigned)index);
        assert(sd_image_catalog_add(&catalog, name) ==
               SD_IMAGE_CATALOG_ADDED);
    }
    assert(sd_image_catalog_add(&catalog, "overflow.pbm") ==
           SD_IMAGE_CATALOG_TOO_MANY);
    assert(catalog.overflowed);

    sd_image_catalog_init(NULL);
    sd_image_catalog_sort(NULL);
    assert(sd_image_catalog_add(NULL, "test.pbm") ==
           SD_IMAGE_CATALOG_IGNORED);
    assert(sd_image_catalog_at(NULL, 0U) == NULL);

    puts("image catalog tests passed");
    return 0;
}
