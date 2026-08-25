#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GALLERY_DOWNLOAD_STATE_IDLE = 0,
    GALLERY_DOWNLOAD_STATE_CONNECTING,
    GALLERY_DOWNLOAD_STATE_FETCHING_CATALOG,
    GALLERY_DOWNLOAD_STATE_DOWNLOADING,
    GALLERY_DOWNLOAD_STATE_VERIFYING,
    GALLERY_DOWNLOAD_STATE_SUCCESS,
    GALLERY_DOWNLOAD_STATE_FAILED,
} gallery_download_state_t;

typedef struct {
    gallery_download_state_t state;
    esp_err_t last_error;
    size_t downloaded_bytes;
    size_t total_bytes;
    size_t image_index;
    size_t image_count;
    uint8_t percent;
} gallery_download_status_t;

esp_err_t gallery_download_init(void);
/* Start after the temporary settings AP has closed while maintenance is held. */
esp_err_t gallery_download_start(void);
esp_err_t gallery_download_get_status(gallery_download_status_t *status);
esp_err_t gallery_download_dismiss_result(void);
bool gallery_download_state_is_active(gallery_download_state_t state);
const char *gallery_download_error_detail(esp_err_t error);

#ifdef __cplusplus
}
#endif
