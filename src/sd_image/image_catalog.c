#include "image_catalog.h"

#include <stdio.h>
#include <string.h>

static bool ascii_is_allowed(char character)
{
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') ||
           character == '-' || character == '_' || character == '.';
}

static char ascii_lower(char character)
{
    if (character >= 'A' && character <= 'Z') {
        return (char)(character + ('a' - 'A'));
    }
    return character;
}

int sd_image_catalog_compare_names(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        if (left == right) {
            return 0;
        }
        return left == NULL ? -1 : 1;
    }
    size_t index = 0U;
    while (left[index] != '\0' && right[index] != '\0') {
        const unsigned char folded_left =
            (unsigned char)ascii_lower(left[index]);
        const unsigned char folded_right =
            (unsigned char)ascii_lower(right[index]);
        if (folded_left != folded_right) {
            return folded_left < folded_right ? -1 : 1;
        }
        ++index;
    }
    if (left[index] != right[index]) {
        return left[index] == '\0' ? -1 : 1;
    }
    return strcmp(left, right);
}

static bool extension_equals(const char *extension, const char *expected)
{
    size_t index = 0U;
    while (extension[index] != '\0' && expected[index] != '\0') {
        if (ascii_lower(extension[index]) != expected[index]) {
            return false;
        }
        ++index;
    }
    return extension[index] == '\0' && expected[index] == '\0';
}

bool sd_image_catalog_expected_format(const char *name,
                                      mono_image_format_t *format)
{
    if (name == NULL || format == NULL) {
        return false;
    }
    const char *extension = strrchr(name, '.');
    if (extension == NULL) {
        return false;
    }
    if (extension_equals(extension, ".pbm")) {
        *format = MONO_IMAGE_FORMAT_PBM_P4;
        return true;
    }
    if (extension_equals(extension, ".bmp")) {
        *format = MONO_IMAGE_FORMAT_BMP_1BPP;
        return true;
    }
    return false;
}

static bool filename_supported(const char *name)
{
    if (name == NULL || name[0] == '\0' || name[0] == '.') {
        return false;
    }
    size_t length = 0U;
    while (name[length] != '\0') {
        if (length + 1U >= SD_IMAGE_CATALOG_NAME_CAPACITY ||
            !ascii_is_allowed(name[length])) {
            return false;
        }
        ++length;
    }
    mono_image_format_t format;
    return sd_image_catalog_expected_format(name, &format);
}

void sd_image_catalog_init(sd_image_catalog_t *catalog)
{
    if (catalog != NULL) {
        memset(catalog, 0, sizeof(*catalog));
    }
}

sd_image_catalog_add_result_t sd_image_catalog_add(
    sd_image_catalog_t *catalog, const char *name)
{
    if (catalog == NULL || !filename_supported(name)) {
        return SD_IMAGE_CATALOG_IGNORED;
    }
    if (catalog->count >= SD_IMAGE_CATALOG_MAX_FILES) {
        catalog->overflowed = true;
        return SD_IMAGE_CATALOG_TOO_MANY;
    }
    snprintf(catalog->names[catalog->count],
             sizeof(catalog->names[catalog->count]), "%s", name);
    ++catalog->count;
    return SD_IMAGE_CATALOG_ADDED;
}

void sd_image_catalog_sort(sd_image_catalog_t *catalog)
{
    if (catalog == NULL) {
        return;
    }
    for (size_t index = 1U; index < catalog->count; ++index) {
        char current[SD_IMAGE_CATALOG_NAME_CAPACITY];
        memcpy(current, catalog->names[index], sizeof(current));
        size_t position = index;
        while (position > 0U &&
               sd_image_catalog_compare_names(
                   current, catalog->names[position - 1U]) < 0) {
            memcpy(catalog->names[position],
                   catalog->names[position - 1U],
                   sizeof(catalog->names[position]));
            --position;
        }
        memcpy(catalog->names[position], current,
               sizeof(catalog->names[position]));
    }
}

const char *sd_image_catalog_at(const sd_image_catalog_t *catalog,
                                size_t index)
{
    if (catalog == NULL || index >= catalog->count) {
        return NULL;
    }
    return catalog->names[index];
}

bool sd_image_catalog_build_path(const char *base_path,
                                 const char *name,
                                 char *path,
                                 size_t path_capacity)
{
    if (base_path == NULL || name == NULL || path == NULL ||
        path_capacity == 0U || !filename_supported(name)) {
        return false;
    }
    const int written = snprintf(path, path_capacity, "%s/%s",
                                 base_path, name);
    return written >= 0 && (size_t)written < path_capacity;
}
