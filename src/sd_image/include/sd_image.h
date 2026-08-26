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
#define SD_IMAGE_MAX_IMAGES 32U
#define SD_IMAGE_SHA256_BYTES 32U

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
    /* Changes whenever the runtime catalog or selected image changes. */
    uint32_t revision;
    esp_err_t last_io_error;
    mono_image_result_t last_decode_error;
    mono_image_format_t format;
    uint64_t card_capacity_bytes;
    size_t image_count;
    size_t selected_index;
    bool catalog_truncated;
    char filename[SD_IMAGE_FILENAME_CAPACITY];
} sd_image_status_t;

typedef struct {
    size_t count;
    size_t selected_index;
    char filenames[SD_IMAGE_MAX_IMAGES][SD_IMAGE_FILENAME_CAPACITY];
} sd_image_catalog_snapshot_t;

typedef struct {
    /* The complete PBM P4 file length, including its header. */
    size_t expected_size;
    /* Optional end-to-end verification, for example for server downloads. */
    bool verify_sha256;
    uint8_t expected_sha256[SD_IMAGE_SHA256_BYTES];
} sd_image_import_options_t;

typedef struct {
    size_t file_size;
    bool duplicate;
    /* Kept for API compatibility; successful imports are published live. */
    bool reload_required;
    uint8_t sha256[SD_IMAGE_SHA256_BYTES];
    char filename[SD_IMAGE_FILENAME_CAPACITY];
} sd_image_import_result_t;

typedef enum {
    SD_IMAGE_DELETE_STATE_IDLE = 0,
    SD_IMAGE_DELETE_STATE_DELETING,
    SD_IMAGE_DELETE_STATE_SUCCESS,
    SD_IMAGE_DELETE_STATE_FAILED,
} sd_image_delete_state_t;

typedef struct {
    sd_image_delete_state_t state;
    uint32_t revision;
    esp_err_t last_error;
    char filename[SD_IMAGE_FILENAME_CAPACITY];
} sd_image_delete_status_t;

typedef struct sd_image_import sd_image_import_t;

esp_err_t sd_image_store_init(void);
void sd_image_store_get_status(sd_image_status_t *status);
size_t sd_image_store_count(void);
size_t sd_image_store_selected_index(void);
/* Copy a consistent, pointer-free catalog snapshot owned by the caller. */
bool sd_image_store_catalog_snapshot(sd_image_catalog_snapshot_t *snapshot);
/* Copy one stable filename without exposing internal catalog storage. */
bool sd_image_store_filename_at(size_t index, char *filename,
                                size_t capacity);
/* Copy the selected bitmap and its catalog coordinates atomically. */
bool sd_image_store_copy_selected(uint8_t *bitmap, size_t capacity,
                                  size_t *selected_index,
                                  size_t *image_count);
/* Copy one cached bitmap selected by its exact stable filename. */
esp_err_t sd_image_store_copy_bitmap(const char *filename,
                                     uint8_t *bitmap, size_t capacity);
/* Persist and activate an exact cached filename as one operation. */
esp_err_t sd_image_store_select_preferred(const char *filename);
/*
 * Delete an exact filename from both the card and the live catalog. Only a
 * currently cached, directory-safe filename is accepted. Public reads return
 * caller-owned snapshots, so the deleted cache allocation is reclaimed before
 * this function returns.
 */
esp_err_t sd_image_store_delete(const char *filename);
/* Run the same exact-file deletion transaction on a dedicated worker task. */
esp_err_t sd_image_store_request_delete(const char *filename);
void sd_image_store_get_delete_status(sd_image_delete_status_t *status);
esp_err_t sd_image_store_dismiss_delete_result(void);
bool sd_image_import_build_filename(
    const uint8_t sha256[SD_IMAGE_SHA256_BYTES],
    char *filename, size_t capacity);

/*
 * A PBM import is the only operation that mounts the card for writing. Calls
 * belonging to one transaction must be made from the same FreeRTOS task.
 * begin() requires an exact, bounded size; write() may be called repeatedly.
 * commit() and abort() both consume the transaction. A successful commit
 * never overwrites an existing file. It publishes the validated bitmap in the
 * sorted runtime catalog (or selects the existing entry for a duplicate)
 * without a reboot. SD_IMAGE_MAX_IMAGES limits active catalog entries;
 * deleting one releases both the slot and its decoded bitmap allocation.
 */
esp_err_t sd_image_import_begin(
    const sd_image_import_options_t *options,
    sd_image_import_t **import);
esp_err_t sd_image_import_write(sd_image_import_t *import,
                                const void *data,
                                size_t size);
esp_err_t sd_image_import_commit(sd_image_import_t *import,
                                 sd_image_import_result_t *result);
esp_err_t sd_image_import_abort(sd_image_import_t *import);
const char *sd_image_state_name(sd_image_state_t state);

#ifdef __cplusplus
}
#endif
