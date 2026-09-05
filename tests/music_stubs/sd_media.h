#pragma once
#include <stdint.h>
#include "esp_err.h"
/* Test root is a fresh, per-run directory supplied by scripts/test.sh. */
typedef struct sd_media_read sd_media_read_t;
esp_err_t sd_media_begin_read(sd_media_read_t **session, uint32_t wait_ms);
esp_err_t sd_media_end_read(sd_media_read_t *session);
typedef struct sd_media_read sd_media_write_t;
esp_err_t sd_media_begin_write(sd_media_write_t **session, uint32_t wait_ms);
esp_err_t sd_media_end_write(sd_media_write_t *session);
