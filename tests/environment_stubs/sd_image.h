#pragma once
#include <stdint.h>
enum {
    SD_IMAGE_STATE_NOT_INITIALIZED,
    SD_IMAGE_STATE_LOADING,
    SD_IMAGE_STATE_CARD_UNAVAILABLE,
    SD_IMAGE_STATE_READY,
};
typedef struct {
    int state;
    uint64_t card_capacity_bytes;
} sd_image_status_t;
void sd_image_store_get_status(sd_image_status_t *status);
