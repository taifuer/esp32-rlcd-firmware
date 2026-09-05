#pragma once

#include <stdint.h>
#include "esp_err.h"

/* A read-only mount lease sharing the image store's SDMMC mutex. The task
 * acquiring it must close every FILE/DIR and release it on every exit path. */
typedef struct sd_media_read sd_media_read_t;
esp_err_t sd_media_begin_read(sd_media_read_t **session, uint32_t wait_ms);
esp_err_t sd_media_end_read(sd_media_read_t *session);
#define SD_MEDIA_MOUNT_PATH "/sdcard"
