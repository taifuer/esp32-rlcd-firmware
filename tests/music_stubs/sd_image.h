#pragma once
enum { SD_IMAGE_STATE_LOADING, SD_IMAGE_STATE_READY };
typedef struct { int state; } sd_image_status_t;
void sd_image_store_get_status(sd_image_status_t *status);
