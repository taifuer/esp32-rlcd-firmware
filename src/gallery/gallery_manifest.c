#include "gallery_manifest.h"

#include <string.h>

#include "cJSON.h"
#include "monochrome_image.h"

static const char PACK_ID[] = "starter";
static const char PACK_PATH_PREFIX[] = "/gallery/v1/starter/";

static bool object_has_exact_fields(const cJSON *object,
                                    const char *const *names,
                                    size_t name_count)
{
    if (!cJSON_IsObject(object) || names == NULL || name_count == 0U ||
        name_count > 31U) {
        return false;
    }
    uint32_t seen = 0U;
    for (const cJSON *item = object->child; item != NULL;
         item = item->next) {
        if (item->string == NULL) {
            return false;
        }
        size_t match = name_count;
        for (size_t index = 0U; index < name_count; ++index) {
            if (strcmp(item->string, names[index]) == 0) {
                match = index;
                break;
            }
        }
        if (match == name_count || (seen & (UINT32_C(1) << match)) != 0U) {
            return false;
        }
        seen |= UINT32_C(1) << match;
    }
    return seen == (UINT32_C(1) << name_count) - 1U;
}

static const cJSON *unique_item(const cJSON *object, const char *name)
{
    if (!cJSON_IsObject(object) || name == NULL) {
        return NULL;
    }
    const cJSON *match = NULL;
    for (const cJSON *item = object->child; item != NULL;
         item = item->next) {
        if (item->string != NULL && strcmp(item->string, name) == 0) {
            if (match != NULL) {
                return NULL;
            }
            match = item;
        }
    }
    return match;
}

static bool copy_string(const cJSON *item, char *destination,
                        size_t capacity)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        destination == NULL || capacity == 0U) {
        return false;
    }
    const size_t length = strlen(item->valuestring);
    if (length == 0U || length >= capacity) {
        return false;
    }
    memcpy(destination, item->valuestring, length + 1U);
    return true;
}

static bool identifier_is_safe(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    for (size_t index = 0U; value[index] != '\0'; ++index) {
        const char character = value[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '-')) {
            return false;
        }
    }
    return true;
}

static bool version_is_safe(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    for (size_t index = 0U; value[index] != '\0'; ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    return true;
}

static bool path_is_safe(const char *path, const char *suffix)
{
    if (path == NULL || suffix == NULL ||
        strncmp(path, PACK_PATH_PREFIX,
                sizeof(PACK_PATH_PREFIX) - 1U) != 0 ||
        strstr(path, "..") != NULL || strchr(path, '?') != NULL ||
        strchr(path, '#') != NULL || strchr(path, '\\') != NULL) {
        return false;
    }
    for (size_t index = 0U; path[index] != '\0'; ++index) {
        const char character = path[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '/' || character == '-' || character == '_' ||
              character == '.') ||
            (character == '/' && index > 0U && path[index - 1U] == '/')) {
            return false;
        }
    }
    const size_t path_length = strlen(path);
    const size_t suffix_length = strlen(suffix);
    return path_length > suffix_length &&
           strcmp(path + path_length - suffix_length, suffix) == 0;
}

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool parse_sha256(const cJSON *item, uint8_t digest[32])
{
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        strlen(item->valuestring) != 64U) {
        return false;
    }
    for (size_t offset = 0U; offset < 64U; offset += 2U) {
        const int high = hex_value(item->valuestring[offset]);
        const int low = hex_value(item->valuestring[offset + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        digest[offset / 2U] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static cJSON *parse_root(const char *json, size_t length)
{
    if (json == NULL || length == 0U ||
        length > GALLERY_MANIFEST_MAX_JSON_BYTES) {
        return NULL;
    }
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (root == NULL) {
        return NULL;
    }
    while (end < json + length &&
           (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        ++end;
    }
    if (end != json + length || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static bool schema_is_one(const cJSON *root)
{
    const cJSON *schema = unique_item(root, "schema");
    return cJSON_IsNumber(schema) && schema->valuedouble == 1.0;
}

gallery_manifest_result_t gallery_catalog_pointer_parse(
    const char *json, size_t length, gallery_catalog_pointer_t *pointer)
{
    if (json == NULL || pointer == NULL) {
        return GALLERY_MANIFEST_INVALID_ARGUMENT;
    }
    memset(pointer, 0, sizeof(*pointer));
    if (length > GALLERY_MANIFEST_MAX_JSON_BYTES) {
        return GALLERY_MANIFEST_TOO_LARGE;
    }
    cJSON *root = parse_root(json, length);
    if (root == NULL) {
        return GALLERY_MANIFEST_INVALID_JSON;
    }
    gallery_manifest_result_t result = GALLERY_MANIFEST_OK;
    static const char *const fields[] = {
        "schema", "pack", "version", "manifest",
    };
    if (!object_has_exact_fields(root, fields,
                                 sizeof(fields) / sizeof(fields[0]))) {
        result = GALLERY_MANIFEST_INVALID_FIELD;
    } else if (!schema_is_one(root)) {
        result = GALLERY_MANIFEST_INVALID_SCHEMA;
    } else if (!copy_string(unique_item(root, "pack"), pointer->pack,
                            sizeof(pointer->pack)) ||
               strcmp(pointer->pack, PACK_ID) != 0 ||
               !copy_string(unique_item(root, "version"), pointer->version,
                            sizeof(pointer->version)) ||
               !version_is_safe(pointer->version)) {
        result = GALLERY_MANIFEST_INVALID_FIELD;
    } else if (!copy_string(unique_item(root, "manifest"),
                            pointer->manifest_path,
                            sizeof(pointer->manifest_path)) ||
               !path_is_safe(pointer->manifest_path, "/manifest.json")) {
        result = GALLERY_MANIFEST_INVALID_PATH;
    }
    cJSON_Delete(root);
    if (result != GALLERY_MANIFEST_OK) {
        memset(pointer, 0, sizeof(*pointer));
    }
    return result;
}

static gallery_manifest_result_t parse_image(
    const cJSON *object, gallery_manifest_image_t *image)
{
    static const char *const fields[] = {
        "id", "title", "path", "size", "sha256", "width", "height",
        "format",
    };
    if (!object_has_exact_fields(object, fields,
                                 sizeof(fields) / sizeof(fields[0]))) {
        return GALLERY_MANIFEST_INVALID_IMAGE;
    }
    const cJSON *width = unique_item(object, "width");
    const cJSON *height = unique_item(object, "height");
    const cJSON *format = unique_item(object, "format");
    const cJSON *size = unique_item(object, "size");
    if (!copy_string(unique_item(object, "id"), image->id,
                     sizeof(image->id)) ||
        !identifier_is_safe(image->id) ||
        !copy_string(unique_item(object, "title"), image->title,
                     sizeof(image->title)) ||
        !copy_string(unique_item(object, "path"), image->path,
                     sizeof(image->path)) ||
        !path_is_safe(image->path, ".pbm") ||
        !cJSON_IsNumber(width) || width->valuedouble != MONO_IMAGE_WIDTH ||
        !cJSON_IsNumber(height) ||
        height->valuedouble != MONO_IMAGE_HEIGHT ||
        !cJSON_IsString(format) || format->valuestring == NULL ||
        strcmp(format->valuestring, "pbm-p4") != 0 ||
        !cJSON_IsNumber(size) || size->valuedouble <= 0.0 ||
        size->valuedouble > MONO_IMAGE_MAX_FILE_BYTES ||
        size->valuedouble != (double)(uint32_t)size->valuedouble ||
        !parse_sha256(unique_item(object, "sha256"), image->sha256)) {
        return GALLERY_MANIFEST_INVALID_IMAGE;
    }
    image->size = (uint32_t)size->valuedouble;
    return GALLERY_MANIFEST_OK;
}

gallery_manifest_result_t gallery_manifest_parse(
    const char *json, size_t length, gallery_manifest_t *manifest)
{
    if (json == NULL || manifest == NULL) {
        return GALLERY_MANIFEST_INVALID_ARGUMENT;
    }
    memset(manifest, 0, sizeof(*manifest));
    if (length > GALLERY_MANIFEST_MAX_JSON_BYTES) {
        return GALLERY_MANIFEST_TOO_LARGE;
    }
    cJSON *root = parse_root(json, length);
    if (root == NULL) {
        return GALLERY_MANIFEST_INVALID_JSON;
    }

    gallery_manifest_result_t result = GALLERY_MANIFEST_OK;
    static const char *const fields[] = {
        "schema", "pack", "version", "title", "images",
    };
    if (!object_has_exact_fields(root, fields,
                                 sizeof(fields) / sizeof(fields[0]))) {
        result = GALLERY_MANIFEST_INVALID_FIELD;
        goto finish;
    }
    if (!schema_is_one(root)) {
        result = GALLERY_MANIFEST_INVALID_SCHEMA;
        goto finish;
    }
    if (!copy_string(unique_item(root, "pack"), manifest->pack,
                     sizeof(manifest->pack)) ||
        strcmp(manifest->pack, PACK_ID) != 0 ||
        !copy_string(unique_item(root, "version"), manifest->version,
                     sizeof(manifest->version)) ||
        !version_is_safe(manifest->version) ||
        !copy_string(unique_item(root, "title"), manifest->title,
                     sizeof(manifest->title))) {
        result = GALLERY_MANIFEST_INVALID_FIELD;
        goto finish;
    }

    const cJSON *images = unique_item(root, "images");
    if (!cJSON_IsArray(images)) {
        result = GALLERY_MANIFEST_INVALID_FIELD;
        goto finish;
    }
    const int count = cJSON_GetArraySize(images);
    if (count <= 0 || count > (int)GALLERY_MANIFEST_MAX_IMAGES) {
        result = count > (int)GALLERY_MANIFEST_MAX_IMAGES
                     ? GALLERY_MANIFEST_TOO_MANY_IMAGES
                     : GALLERY_MANIFEST_INVALID_FIELD;
        goto finish;
    }
    manifest->image_count = (size_t)count;
    for (int index = 0; index < count; ++index) {
        const cJSON *object = cJSON_GetArrayItem(images, index);
        if (!cJSON_IsObject(object)) {
            result = GALLERY_MANIFEST_INVALID_IMAGE;
            goto finish;
        }
        result = parse_image(object, &manifest->images[index]);
        if (result != GALLERY_MANIFEST_OK) {
            goto finish;
        }
        for (int previous = 0; previous < index; ++previous) {
            if (strcmp(manifest->images[previous].id,
                       manifest->images[index].id) == 0 ||
                strcmp(manifest->images[previous].path,
                       manifest->images[index].path) == 0) {
                result = GALLERY_MANIFEST_INVALID_IMAGE;
                goto finish;
            }
        }
        if (manifest->total_size >
            GALLERY_MANIFEST_MAX_TOTAL_BYTES -
                manifest->images[index].size) {
            result = GALLERY_MANIFEST_TOO_LARGE;
            goto finish;
        }
        manifest->total_size += manifest->images[index].size;
    }

finish:
    cJSON_Delete(root);
    if (result != GALLERY_MANIFEST_OK) {
        memset(manifest, 0, sizeof(*manifest));
    }
    return result;
}

bool gallery_manifest_matches_pointer(
    const gallery_manifest_t *manifest,
    const gallery_catalog_pointer_t *pointer)
{
    return manifest != NULL && pointer != NULL &&
           strcmp(manifest->pack, pointer->pack) == 0 &&
           strcmp(manifest->version, pointer->version) == 0;
}

const char *gallery_manifest_result_name(gallery_manifest_result_t result)
{
    switch (result) {
    case GALLERY_MANIFEST_OK:
        return "ok";
    case GALLERY_MANIFEST_INVALID_ARGUMENT:
        return "invalid argument";
    case GALLERY_MANIFEST_INVALID_JSON:
        return "invalid json";
    case GALLERY_MANIFEST_INVALID_SCHEMA:
        return "invalid schema";
    case GALLERY_MANIFEST_INVALID_FIELD:
        return "invalid field";
    case GALLERY_MANIFEST_INVALID_PATH:
        return "invalid path";
    case GALLERY_MANIFEST_INVALID_IMAGE:
        return "invalid image";
    case GALLERY_MANIFEST_TOO_MANY_IMAGES:
        return "too many images";
    case GALLERY_MANIFEST_TOO_LARGE:
        return "too large";
    default:
        return "unknown";
    }
}
