#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "firmware_update_policy.h"

int main(void)
{
    assert(!firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_IDLE));
    assert(firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_STARTING));
    assert(firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_READY));
    assert(firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_WIFI_VALIDATING));
    assert(firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_WIFI_SAVED));
    assert(firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_RECEIVING));
    assert(firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_VERIFYING));
    assert(firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_SUCCESS));
    assert(!firmware_update_state_is_session_active(
        FIRMWARE_UPDATE_STATE_FAILED));

    assert(firmware_update_state_is_dismissible(
        FIRMWARE_UPDATE_STATE_FAILED));
    assert(firmware_update_state_is_dismissible(
        FIRMWARE_UPDATE_STATE_EXPIRED));
    assert(firmware_update_state_is_dismissible(
        FIRMWARE_UPDATE_STATE_CANCELLED));
    assert(!firmware_update_state_is_dismissible(
        FIRMWARE_UPDATE_STATE_SUCCESS));

    assert(firmware_update_progress_percent(0U, 0U) == 0U);
    assert(firmware_update_progress_percent(0U, 100U) == 0U);
    assert(firmware_update_progress_percent(1U, 3U) == 33U);
    assert(firmware_update_progress_percent(99U, 100U) == 99U);
    assert(firmware_update_progress_percent(100U, 100U) == 100U);
    assert(firmware_update_progress_percent(101U, 100U) == 100U);

    puts("firmware update policy tests passed");
    return 0;
}
