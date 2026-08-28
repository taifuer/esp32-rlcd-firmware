#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FIRMWARE_UPDATE_STATE_IDLE = 0,
    FIRMWARE_UPDATE_STATE_STARTING,
    FIRMWARE_UPDATE_STATE_READY,
    FIRMWARE_UPDATE_STATE_WIFI_VALIDATING,
    FIRMWARE_UPDATE_STATE_WIFI_SAVED,
    FIRMWARE_UPDATE_STATE_RECEIVING,
    FIRMWARE_UPDATE_STATE_VERIFYING,
    FIRMWARE_UPDATE_STATE_SUCCESS,
    FIRMWARE_UPDATE_STATE_FAILED,
    FIRMWARE_UPDATE_STATE_EXPIRED,
    FIRMWARE_UPDATE_STATE_CANCELLED,
} firmware_update_state_t;

bool firmware_update_state_is_session_active(firmware_update_state_t state);
bool firmware_update_state_is_dismissible(firmware_update_state_t state);
uint8_t firmware_update_progress_percent(size_t received, size_t total);

#ifdef __cplusplus
}
#endif
