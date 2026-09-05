#include "boot_recovery.h"

#include <stddef.h>
#include <string.h>

#include "boot_recovery_policy.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    BOOT_RECOVERY_RECORD_MAGIC = 0x524c4344U,
    BOOT_RECOVERY_RECORD_SCHEMA = 1U,
    BOOT_RECOVERY_FLAG_START_IN_PROGRESS = 1U << 0,
    BOOT_RECOVERY_FLAG_PLANNED_RESTART = 1U << 1,
};

typedef struct {
    uint32_t magic;
    uint32_t schema;
    uint8_t image_fingerprint[BOOT_RECOVERY_IMAGE_FINGERPRINT_BYTES];
    uint32_t flags;
    uint32_t consecutive_faults;
    uint32_t reset_reason;
    uint32_t phase;
    uint32_t free_internal_heap;
    uint32_t minimum_internal_heap;
    uint32_t main_stack_free;
    uint32_t checksum;
} boot_recovery_record_t;

static RTC_NOINIT_ATTR boot_recovery_record_t s_record;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static boot_recovery_status_t s_status;
static bool s_started;

static uint32_t record_checksum(const boot_recovery_record_t *record)
{
    const uint8_t *const bytes = (const uint8_t *)record;
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < offsetof(boot_recovery_record_t, checksum); ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

static bool record_valid(const boot_recovery_record_t *record)
{
    return record->magic == BOOT_RECOVERY_RECORD_MAGIC &&
           record->schema == BOOT_RECOVERY_RECORD_SCHEMA &&
           record->phase <= BOOT_RECOVERY_PHASE_HEALTHY &&
           record->consecutive_faults <= UINT8_MAX &&
           record->checksum == record_checksum(record);
}

static void commit_record(void)
{
    s_record.checksum = record_checksum(&s_record);
}

static boot_recovery_reset_class_t classify_reset(
    esp_reset_reason_t reason, bool planned_restart)
{
    switch (reason) {
    case ESP_RST_SW:
        return planned_restart ? BOOT_RECOVERY_RESET_EXPECTED
                               : BOOT_RECOVERY_RESET_FAULT;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_CPU_LOCKUP:
        return BOOT_RECOVERY_RESET_FAULT;
    case ESP_RST_POWERON:
    case ESP_RST_BROWNOUT:
    case ESP_RST_PWR_GLITCH:
        return BOOT_RECOVERY_RESET_POWER;
    case ESP_RST_UNKNOWN:
    case ESP_RST_EXT:
    case ESP_RST_DEEPSLEEP:
    case ESP_RST_SDIO:
    case ESP_RST_USB:
    case ESP_RST_JTAG:
    case ESP_RST_EFUSE:
    default:
        return BOOT_RECOVERY_RESET_OTHER;
    }
}

void boot_recovery_begin(esp_reset_reason_t reset_reason,
                         const uint8_t app_elf_sha256[32],
                         bool manual_recovery_requested)
{
    if (app_elf_sha256 == NULL) {
        return;
    }

    const uint32_t free_internal_heap =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t minimum_internal_heap =
        heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t main_stack_free =
        (uint32_t)uxTaskGetStackHighWaterMark(NULL);

    portENTER_CRITICAL(&s_lock);
    if (s_started) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_started = true;
    const bool valid = record_valid(&s_record);
    const bool same_image =
        valid && memcmp(s_record.image_fingerprint, app_elf_sha256,
                        BOOT_RECOVERY_IMAGE_FINGERPRINT_BYTES) == 0;
    const bool planned_restart =
        valid &&
        (s_record.flags & BOOT_RECOVERY_FLAG_PLANNED_RESTART) != 0U;
    const boot_recovery_evaluation_t evaluation = {
        .record_valid = valid,
        .image_fingerprint_matches = same_image,
        .previous_start_in_progress =
            valid &&
            (s_record.flags & BOOT_RECOVERY_FLAG_START_IN_PROGRESS) != 0U,
        .reset_class = classify_reset(reset_reason, planned_restart),
        .manual_recovery_requested = manual_recovery_requested,
        .consecutive_faults =
            valid && s_record.consecutive_faults <= UINT8_MAX
                ? (uint8_t)s_record.consecutive_faults
                : 0U,
    };
    const boot_recovery_decision_t decision =
        boot_recovery_evaluate(&evaluation);

    s_status = (boot_recovery_status_t){
        .recovery_mode = decision.enter_recovery,
        .manual_recovery = manual_recovery_requested,
        .consecutive_faults = decision.consecutive_faults,
        .reset_reason = reset_reason,
        .previous_phase =
            valid ? (boot_recovery_phase_t)s_record.phase
                  : BOOT_RECOVERY_PHASE_EARLY,
        .current_phase = BOOT_RECOVERY_PHASE_EARLY,
        .previous_free_internal_heap =
            valid ? s_record.free_internal_heap : 0U,
        .previous_minimum_internal_heap =
            valid ? s_record.minimum_internal_heap : 0U,
        .previous_main_stack_free =
            valid ? s_record.main_stack_free : 0U,
    };

    memset(&s_record, 0, sizeof(s_record));
    s_record.magic = BOOT_RECOVERY_RECORD_MAGIC;
    s_record.schema = BOOT_RECOVERY_RECORD_SCHEMA;
    memcpy(s_record.image_fingerprint, app_elf_sha256,
           BOOT_RECOVERY_IMAGE_FINGERPRINT_BYTES);
    s_record.flags = BOOT_RECOVERY_FLAG_START_IN_PROGRESS;
    s_record.consecutive_faults = decision.consecutive_faults;
    s_record.reset_reason = (uint32_t)reset_reason;
    s_record.phase = BOOT_RECOVERY_PHASE_EARLY;
    s_record.free_internal_heap = free_internal_heap;
    s_record.minimum_internal_heap = minimum_internal_heap;
    s_record.main_stack_free = main_stack_free;
    commit_record();
    portEXIT_CRITICAL(&s_lock);
}

void boot_recovery_set_phase(boot_recovery_phase_t phase)
{
    if ((unsigned)phase >= BOOT_RECOVERY_PHASE_HEALTHY) {
        return;
    }
    const uint32_t free_internal_heap =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t minimum_internal_heap =
        heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t main_stack_free =
        (uint32_t)uxTaskGetStackHighWaterMark(NULL);

    portENTER_CRITICAL(&s_lock);
    if (s_started && record_valid(&s_record)) {
        s_status.current_phase = phase;
        s_record.phase = (uint32_t)phase;
        s_record.free_internal_heap = free_internal_heap;
        s_record.minimum_internal_heap = minimum_internal_heap;
        s_record.main_stack_free = main_stack_free;
        commit_record();
    }
    portEXIT_CRITICAL(&s_lock);
}

void boot_recovery_note_planned_restart(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_started && record_valid(&s_record)) {
        s_record.flags |= BOOT_RECOVERY_FLAG_PLANNED_RESTART;
        commit_record();
    }
    portEXIT_CRITICAL(&s_lock);
}

void boot_recovery_mark_healthy(void)
{
    const uint32_t free_internal_heap =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t minimum_internal_heap =
        heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t main_stack_free =
        (uint32_t)uxTaskGetStackHighWaterMark(NULL);

    portENTER_CRITICAL(&s_lock);
    if (s_started && record_valid(&s_record) && !s_status.recovery_mode) {
        boot_recovery_decision_t decision = {
            .consecutive_faults = (uint8_t)s_record.consecutive_faults,
            .enter_recovery = false,
        };
        decision = boot_recovery_confirm_healthy(decision);
        s_record.consecutive_faults = decision.consecutive_faults;
        /* Preserve a planned-restart marker if another task set it just
         * before this health transition.  The following boot consumes it
         * when classifying ESP_RST_SW, while boot_recovery_begin() always
         * rewrites the record so the marker cannot leak into later boots. */
        s_record.flags &=
            ~(uint32_t)BOOT_RECOVERY_FLAG_START_IN_PROGRESS;
        s_record.phase = BOOT_RECOVERY_PHASE_HEALTHY;
        s_record.free_internal_heap = free_internal_heap;
        s_record.minimum_internal_heap = minimum_internal_heap;
        s_record.main_stack_free = main_stack_free;
        s_status.consecutive_faults = 0U;
        s_status.current_phase = BOOT_RECOVERY_PHASE_HEALTHY;
        commit_record();
    }
    portEXIT_CRITICAL(&s_lock);
}

bool boot_recovery_is_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_lock);
    active = s_status.recovery_mode;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

void boot_recovery_get_status(boot_recovery_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    if (s_started && record_valid(&s_record)) {
        status->main_stack_free_at_checkpoint = s_record.main_stack_free;
    }
    portEXIT_CRITICAL(&s_lock);
}

const char *boot_recovery_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "POWER ON";
    case ESP_RST_EXT:
        return "EXTERNAL";
    case ESP_RST_SW:
        return "SOFTWARE";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT WDT";
    case ESP_RST_TASK_WDT:
        return "TASK WDT";
    case ESP_RST_WDT:
        return "WATCHDOG";
    case ESP_RST_DEEPSLEEP:
        return "DEEP SLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    case ESP_RST_USB:
        return "USB";
    case ESP_RST_JTAG:
        return "JTAG";
    case ESP_RST_EFUSE:
        return "EFUSE";
    case ESP_RST_PWR_GLITCH:
        return "POWER GLITCH";
    case ESP_RST_CPU_LOCKUP:
        return "CPU LOCKUP";
    case ESP_RST_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

const char *boot_recovery_phase_name(boot_recovery_phase_t phase)
{
    switch (phase) {
    case BOOT_RECOVERY_PHASE_DISPLAY:
        return "DISPLAY";
    case BOOT_RECOVERY_PHASE_STORAGE:
        return "STORAGE";
    case BOOT_RECOVERY_PHASE_HARDWARE:
        return "HARDWARE";
    case BOOT_RECOVERY_PHASE_SERVICES:
        return "SERVICES";
    case BOOT_RECOVERY_PHASE_MAIN_LOOP:
        return "MAIN LOOP";
    case BOOT_RECOVERY_PHASE_HEALTHY:
        return "HEALTHY";
    case BOOT_RECOVERY_PHASE_EARLY:
    default:
        return "EARLY START";
    }
}
