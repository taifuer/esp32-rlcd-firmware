#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GALLERY_MANIFEST_MAX_IMAGES 8U
#define GALLERY_MANIFEST_PATH_CAPACITY 144U
#define GALLERY_MANIFEST_ID_CAPACITY 33U
#define GALLERY_MANIFEST_TITLE_CAPACITY 49U
#define GALLERY_MANIFEST_VERSION_CAPACITY 17U
#define GALLERY_MANIFEST_SHA256_BYTES 32U
#define GALLERY_MANIFEST_MAX_JSON_BYTES 4096U
#define GALLERY_MANIFEST_MAX_TOTAL_BYTES (128U * 1024U)

typedef enum {
    GALLERY_MANIFEST_OK = 0,
    GALLERY_MANIFEST_INVALID_ARGUMENT,
    GALLERY_MANIFEST_INVALID_JSON,
    GALLERY_MANIFEST_INVALID_SCHEMA,
    GALLERY_MANIFEST_INVALID_FIELD,
    GALLERY_MANIFEST_INVALID_PATH,
    GALLERY_MANIFEST_INVALID_IMAGE,
    GALLERY_MANIFEST_TOO_MANY_IMAGES,
    GALLERY_MANIFEST_TOO_LARGE,
} gallery_manifest_result_t;

typedef struct {
    char pack[GALLERY_MANIFEST_ID_CAPACITY];
    char version[GALLERY_MANIFEST_VERSION_CAPACITY];
    char manifest_path[GALLERY_MANIFEST_PATH_CAPACITY];
} gallery_catalog_pointer_t;

typedef struct {
    char id[GALLERY_MANIFEST_ID_CAPACITY];
    char title[GALLERY_MANIFEST_TITLE_CAPACITY];
    char path[GALLERY_MANIFEST_PATH_CAPACITY];
    uint32_t size;
    uint8_t sha256[GALLERY_MANIFEST_SHA256_BYTES];
} gallery_manifest_image_t;

typedef struct {
    char pack[GALLERY_MANIFEST_ID_CAPACITY];
    char version[GALLERY_MANIFEST_VERSION_CAPACITY];
    char title[GALLERY_MANIFEST_TITLE_CAPACITY];
    size_t image_count;
    uint32_t total_size;
    gallery_manifest_image_t images[GALLERY_MANIFEST_MAX_IMAGES];
} gallery_manifest_t;

gallery_manifest_result_t gallery_catalog_pointer_parse(
    const char *json, size_t length, gallery_catalog_pointer_t *pointer);
gallery_manifest_result_t gallery_manifest_parse(
    const char *json, size_t length, gallery_manifest_t *manifest);
bool gallery_manifest_matches_pointer(
    const gallery_manifest_t *manifest,
    const gallery_catalog_pointer_t *pointer);
const char *gallery_manifest_result_name(gallery_manifest_result_t result);

#ifdef __cplusplus
}
#endif
