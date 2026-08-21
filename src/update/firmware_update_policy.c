#include "firmware_update_policy.h"

bool firmware_update_state_is_session_active(firmware_update_state_t state)
{
    return state >= FIRMWARE_UPDATE_STATE_STARTING &&
           state <= FIRMWARE_UPDATE_STATE_SUCCESS;
}

bool firmware_update_state_is_dismissible(firmware_update_state_t state)
{
    return state == FIRMWARE_UPDATE_STATE_FAILED ||
           state == FIRMWARE_UPDATE_STATE_EXPIRED ||
           state == FIRMWARE_UPDATE_STATE_CANCELLED;
}

uint8_t firmware_update_progress_percent(size_t received, size_t total)
{
    if (total == 0U) {
        return 0U;
    }
    if (received >= total) {
        return 100U;
    }
    return (uint8_t)((received * 100U) / total);
}
