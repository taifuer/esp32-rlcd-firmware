#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gallery_manifest.h"

static const char VALID_POINTER[] =
    "{\"schema\":1,\"pack\":\"starter\",\"version\":\"1\","
    "\"manifest\":\"/gallery/v1/starter/manifest.json\"}";

static const char VALID_MANIFEST[] =
    "{\"schema\":1,\"pack\":\"starter\",\"version\":\"1\","
    "\"title\":\"Starter gallery\",\"images\":["
    "{\"id\":\"sunrise\",\"title\":\"Sunrise\","
    "\"path\":\"/gallery/v1/starter/images/sunrise.pbm\","
    "\"size\":15011,"
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
    "\"width\":400,\"height\":300,\"format\":\"pbm-p4\"},"
    "{\"id\":\"city\",\"title\":\"City\","
    "\"path\":\"/gallery/v1/starter/images/city.pbm\","
    "\"size\":15011,"
    "\"sha256\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\","
    "\"width\":400,\"height\":300,\"format\":\"pbm-p4\"}]}";

static void test_valid_catalog_and_manifest(void)
{
    gallery_catalog_pointer_t pointer = {0};
    assert(gallery_catalog_pointer_parse(
               VALID_POINTER, strlen(VALID_POINTER), &pointer) ==
           GALLERY_MANIFEST_OK);
    assert(strcmp(pointer.pack, "starter") == 0);
    assert(strcmp(pointer.version, "1") == 0);
    assert(strcmp(pointer.manifest_path,
                  "/gallery/v1/starter/manifest.json") == 0);

    gallery_manifest_t manifest = {0};
    assert(gallery_manifest_parse(
               VALID_MANIFEST, strlen(VALID_MANIFEST), &manifest) ==
           GALLERY_MANIFEST_OK);
    assert(manifest.image_count == 2U);
    assert(manifest.total_size == 30022U);
    assert(strcmp(manifest.images[0].id, "sunrise") == 0);
    assert(manifest.images[0].sha256[0] == 0x01U);
    assert(manifest.images[0].sha256[31] == 0xefU);
    assert(gallery_manifest_matches_pointer(&manifest, &pointer));

    pointer.version[0] = '2';
    assert(!gallery_manifest_matches_pointer(&manifest, &pointer));
    assert(!gallery_manifest_matches_pointer(NULL, &pointer));
}

static void test_rejects_untrusted_or_malformed_catalog(void)
{
    const char *invalid[] = {
        "{}",
        "{\"schema\":2,\"pack\":\"starter\",\"version\":\"1\",\"manifest\":\"/gallery/v1/starter/manifest.json\"}",
        "{\"schema\":1,\"pack\":\"other\",\"version\":\"1\",\"manifest\":\"/gallery/v1/starter/manifest.json\"}",
        "{\"schema\":1,\"pack\":\"starter\",\"version\":\"v1\",\"manifest\":\"/gallery/v1/starter/manifest.json\"}",
        "{\"schema\":1,\"pack\":\"starter\",\"version\":\"1\",\"manifest\":\"https://example.com/x.json\"}",
        "{\"schema\":1,\"pack\":\"starter\",\"version\":\"1\",\"manifest\":\"/gallery/v1/starter/../manifest.json\"}",
        "{\"schema\":1,\"pack\":\"starter\",\"version\":\"1\",\"manifest\":\"/gallery/v1/starter/%6d/manifest.json\"}",
        "{\"schema\":1,\"pack\":\"starter\",\"version\":\"1\",\"manifest\":\"/gallery/v1/starter/manifest.json\",\"extra\":true}",
        "{\"schema\":1,\"schema\":1,\"pack\":\"starter\",\"version\":\"1\",\"manifest\":\"/gallery/v1/starter/manifest.json\"}",
    };
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        gallery_catalog_pointer_t pointer = {0};
        assert(gallery_catalog_pointer_parse(
                   invalid[index], strlen(invalid[index]), &pointer) !=
               GALLERY_MANIFEST_OK);
    }
}

static void test_rejects_invalid_images(void)
{
    const char *invalid_path =
        "{\"schema\":1,\"pack\":\"starter\",\"version\":\"1\",\"title\":\"x\",\"images\":["
        "{\"id\":\"x\",\"title\":\"x\",\"path\":\"/other/x.pbm\",\"size\":15011,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"width\":400,\"height\":300,\"format\":\"pbm-p4\"}]}";
    gallery_manifest_t manifest = {0};
    assert(gallery_manifest_parse(
               invalid_path, strlen(invalid_path), &manifest) ==
           GALLERY_MANIFEST_INVALID_IMAGE);

    const char *extra_image_field =
        "{\"schema\":1,\"pack\":\"starter\",\"version\":\"1\",\"title\":\"x\",\"images\":["
        "{\"id\":\"x\",\"title\":\"x\",\"path\":\"/gallery/v1/starter/x.pbm\",\"size\":15011,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"width\":400,\"height\":300,\"format\":\"pbm-p4\",\"extra\":true}]}";
    assert(gallery_manifest_parse(
               extra_image_field, strlen(extra_image_field), &manifest) ==
           GALLERY_MANIFEST_INVALID_IMAGE);

    char duplicate[sizeof(VALID_MANIFEST) + 32U];
    snprintf(duplicate, sizeof(duplicate), "%s", VALID_MANIFEST);
    char *city = strstr(duplicate, "\"id\":\"city\"");
    assert(city != NULL);
    memcpy(city + strlen("\"id\":\""), "sunrise", 7U);
    assert(gallery_manifest_parse(
               duplicate, strlen(duplicate), &manifest) !=
           GALLERY_MANIFEST_OK);

    assert(gallery_manifest_parse(NULL, 1U, &manifest) ==
           GALLERY_MANIFEST_INVALID_ARGUMENT);
    assert(gallery_manifest_parse(VALID_MANIFEST,
                                  strlen(VALID_MANIFEST), NULL) ==
           GALLERY_MANIFEST_INVALID_ARGUMENT);
    assert(strcmp(gallery_manifest_result_name(GALLERY_MANIFEST_OK),
                  "ok") == 0);
}

int main(void)
{
    test_valid_catalog_and_manifest();
    test_rejects_untrusted_or_malformed_catalog();
    test_rejects_invalid_images();
    puts("gallery manifest tests passed");
    return 0;
}
