#include "boot_recovery_policy.h"

#include <limits.h>
#include <stddef.h>

static uint8_t increment_saturated(uint8_t value)
{
    return value == UINT8_MAX ? value : (uint8_t)(value + 1U);
}

boot_recovery_decision_t boot_recovery_evaluate(
    const boot_recovery_evaluation_t *evaluation)
{
    boot_recovery_decision_t decision = {0};
    if (evaluation == NULL) {
        return decision;
    }

    const bool same_image = evaluation->record_valid &&
                            evaluation->image_fingerprint_matches;
    if (same_image && evaluation->manual_recovery_requested &&
        evaluation->reset_class != BOOT_RECOVERY_RESET_POWER) {
        decision.consecutive_faults = evaluation->consecutive_faults;
    } else if (same_image &&
        evaluation->reset_class == BOOT_RECOVERY_RESET_FAULT &&
        !evaluation->manual_recovery_requested) {
        /* A fault after a previously healthy run starts a new sequence.  A
         * fault while the following boot is still inside its health window
         * continues that sequence. */
        decision.consecutive_faults =
            evaluation->previous_start_in_progress
                ? increment_saturated(evaluation->consecutive_faults)
                : 1U;
    }

    decision.enter_recovery =
        evaluation->manual_recovery_requested ||
        decision.consecutive_faults >= BOOT_RECOVERY_FAULT_THRESHOLD;
    return decision;
}

boot_recovery_decision_t boot_recovery_confirm_healthy(
    boot_recovery_decision_t current)
{
    current.consecutive_faults = 0U;
    current.enter_recovery = false;
    return current;
}

bool boot_recovery_should_confirm_ota(
    const boot_recovery_ota_gate_t *gate)
{
    if (gate == NULL || gate->recovery_mode ||
        !gate->startup_milestones_complete ||
        gate->uptime_ms < BOOT_RECOVERY_OTA_CONFIRM_DELAY_MS) {
        return false;
    }
    return gate->first_attempt ||
           gate->since_last_attempt_ms >=
               BOOT_RECOVERY_OTA_CONFIRM_RETRY_MS;
}

bool boot_recovery_should_mark_healthy(bool recovery_mode,
                                       bool startup_milestones_complete,
                                       uint32_t uptime_ms)
{
    return !recovery_mode && startup_milestones_complete &&
           uptime_ms >= BOOT_RECOVERY_HEALTHY_DELAY_MS;
}
