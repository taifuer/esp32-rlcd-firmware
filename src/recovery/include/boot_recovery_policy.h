#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BOOT_RECOVERY_FAULT_THRESHOLD = 3U,
    BOOT_RECOVERY_OTA_CONFIRM_DELAY_MS = 15000U,
    BOOT_RECOVERY_OTA_CONFIRM_RETRY_MS = 5000U,
    BOOT_RECOVERY_HEALTHY_DELAY_MS = 60000U,
};

typedef enum {
    BOOT_RECOVERY_RESET_POWER = 0,
    BOOT_RECOVERY_RESET_EXPECTED,
    BOOT_RECOVERY_RESET_FAULT,
    BOOT_RECOVERY_RESET_OTHER,
} boot_recovery_reset_class_t;

typedef struct {
    bool record_valid;
    bool image_fingerprint_matches;
    bool previous_start_in_progress;
    boot_recovery_reset_class_t reset_class;
    bool manual_recovery_requested;
    uint8_t consecutive_faults;
} boot_recovery_evaluation_t;

typedef struct {
    uint8_t consecutive_faults;
    bool enter_recovery;
} boot_recovery_decision_t;

boot_recovery_decision_t boot_recovery_evaluate(
    const boot_recovery_evaluation_t *evaluation);

boot_recovery_decision_t boot_recovery_confirm_healthy(
    boot_recovery_decision_t current);

typedef struct {
    bool recovery_mode;
    bool startup_milestones_complete;
    uint32_t uptime_ms;
    bool first_attempt;
    uint32_t since_last_attempt_ms;
} boot_recovery_ota_gate_t;

bool boot_recovery_should_confirm_ota(
    const boot_recovery_ota_gate_t *gate);
bool boot_recovery_should_mark_healthy(bool recovery_mode,
                                       bool startup_milestones_complete,
                                       uint32_t uptime_ms);

#ifdef __cplusplus
}
#endif
