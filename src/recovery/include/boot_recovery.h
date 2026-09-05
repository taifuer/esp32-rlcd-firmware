#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_RECOVERY_IMAGE_FINGERPRINT_BYTES 8U

typedef enum {
    BOOT_RECOVERY_PHASE_EARLY = 0,
    BOOT_RECOVERY_PHASE_DISPLAY,
    BOOT_RECOVERY_PHASE_STORAGE,
    BOOT_RECOVERY_PHASE_HARDWARE,
    BOOT_RECOVERY_PHASE_SERVICES,
    BOOT_RECOVERY_PHASE_MAIN_LOOP,
    BOOT_RECOVERY_PHASE_HEALTHY,
} boot_recovery_phase_t;

typedef struct {
    bool recovery_mode;
    bool manual_recovery;
    uint8_t consecutive_faults;
    esp_reset_reason_t reset_reason;
    boot_recovery_phase_t previous_phase;
    boot_recovery_phase_t current_phase;
    uint32_t main_stack_free_at_checkpoint;
    uint32_t previous_free_internal_heap;
    uint32_t previous_minimum_internal_heap;
    uint32_t previous_main_stack_free;
} boot_recovery_status_t;

void boot_recovery_begin(esp_reset_reason_t reset_reason,
                         const uint8_t app_elf_sha256[32],
                         bool manual_recovery_requested);
void boot_recovery_set_phase(boot_recovery_phase_t phase);
void boot_recovery_note_planned_restart(void);
void boot_recovery_mark_healthy(void);
bool boot_recovery_is_active(void);
void boot_recovery_get_status(boot_recovery_status_t *status);
const char *boot_recovery_reset_reason_name(esp_reset_reason_t reason);
const char *boot_recovery_phase_name(boot_recovery_phase_t phase);

#ifdef __cplusplus
}
#endif
