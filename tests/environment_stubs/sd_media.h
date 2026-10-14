#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct sd_media_read sd_media_read_t;
typedef struct sd_media_read sd_media_write_t;
esp_err_t sd_media_begin_read(sd_media_read_t **session, uint32_t wait_ms);
esp_err_t sd_media_end_read(sd_media_read_t *session);
esp_err_t sd_media_begin_write(sd_media_write_t **session, uint32_t wait_ms);
esp_err_t sd_media_end_write(sd_media_write_t *session);
esp_err_t sd_media_get_free_bytes(sd_media_read_t *session, uint64_t *free_bytes);
