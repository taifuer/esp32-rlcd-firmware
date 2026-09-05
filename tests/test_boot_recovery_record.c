#include <assert.h>
#include <stdio.h>

/* Exercise the actual target adapter with only hardware/RTOS calls stubbed.
 * Access to its static storage lets a reset retain RTC memory while clearing
 * ordinary BSS, without adding fault-injection commands to user firmware. */
#include "../src/recovery/boot_recovery.c"

static const uint8_t image_a[32] = {0x12, 0x34};
static const uint8_t image_b[32] = {0x56, 0x78};

void recovery_test_enter_critical(portMUX_TYPE *lock)
{
    assert(*lock == 0U);
    *lock = 1U;
}

void recovery_test_exit_critical(portMUX_TYPE *lock)
{
    assert(*lock == 1U);
    *lock = 0U;
}

size_t heap_caps_get_free_size(uint32_t capabilities)
{
    assert(s_lock == 0U);
    assert(capabilities == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return 80000U;
}

size_t heap_caps_get_minimum_free_size(uint32_t capabilities)
{
    assert(s_lock == 0U);
    assert(capabilities == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return 60000U;
}

uint32_t uxTaskGetStackHighWaterMark(void *task)
{
    assert(s_lock == 0U && task == NULL);
    return 12000U;
}

static void simulate_reset(esp_reset_reason_t reason, const uint8_t *image,
                           bool manual)
{
    assert(s_lock == 0U);
    memset(&s_status, 0, sizeof(s_status));
    s_started = false;
    boot_recovery_begin(reason, image, manual);
    assert(record_valid(&s_record));
    assert(s_lock == 0U);
}

static void clean_boot(void)
{
    memset(&s_record, 0xa5, sizeof(s_record));
    simulate_reset(ESP_RST_POWERON, image_a, false);
    assert(!boot_recovery_is_active());
    assert(s_status.consecutive_faults == 0U);
}

static void test_crash_sequence_and_diagnostics(void)
{
    clean_boot();
    const esp_reset_reason_t faults[] = {
        ESP_RST_PANIC, ESP_RST_TASK_WDT, ESP_RST_SW,
    };
    for (size_t i = 0U; i < 3U; ++i) {
        boot_recovery_set_phase(BOOT_RECOVERY_PHASE_SERVICES);
        simulate_reset(faults[i], image_a, false);
        boot_recovery_status_t status;
        boot_recovery_get_status(&status);
        assert(status.consecutive_faults == i + 1U);
        assert(status.recovery_mode == (i == 2U));
        assert(status.previous_phase == BOOT_RECOVERY_PHASE_SERVICES);
        assert(status.previous_main_stack_free == 12000U);
        assert(status.previous_free_internal_heap == 80000U);
        assert(status.previous_minimum_internal_heap == 60000U);
        assert(status.main_stack_free_at_checkpoint == 12000U);
    }
    boot_recovery_mark_healthy();
    assert(boot_recovery_is_active());
    assert(s_status.consecutive_faults == 3U);
    simulate_reset(ESP_RST_POWERON, image_a, false);
    assert(!boot_recovery_is_active());
}

static void test_reset_classes_and_image_change(void)
{
    const esp_reset_reason_t faults[] = {
        ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
        ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_CPU_LOCKUP,
    };
    for (size_t i = 0U; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        clean_boot();
        simulate_reset(faults[i], image_a, false);
        assert(s_status.consecutive_faults == 1U);
    }
    const esp_reset_reason_t other[] = {
        ESP_RST_POWERON, ESP_RST_BROWNOUT, ESP_RST_PWR_GLITCH,
        ESP_RST_USB, ESP_RST_JTAG, ESP_RST_EXT, ESP_RST_DEEPSLEEP,
        ESP_RST_UNKNOWN, ESP_RST_SDIO, ESP_RST_EFUSE,
    };
    for (size_t i = 0U; i < sizeof(other) / sizeof(other[0]); ++i) {
        clean_boot();
        simulate_reset(ESP_RST_PANIC, image_a, false);
        simulate_reset(other[i], image_a, false);
        assert(s_status.consecutive_faults == 0U);
    }
    simulate_reset(ESP_RST_PANIC, image_b, false);
    assert(s_status.consecutive_faults == 0U);
}

static void test_record_validation(void)
{
    clean_boot();
    simulate_reset(ESP_RST_PANIC, image_a, false);
    s_record.phase ^= 1U; /* Torn/corrupted RTC payload. */
    simulate_reset(ESP_RST_PANIC, image_a, false);
    assert(s_status.consecutive_faults == 0U);

    s_record.schema++;
    commit_record();
    simulate_reset(ESP_RST_PANIC, image_a, false);
    assert(s_status.consecutive_faults == 0U);
    s_record.phase = UINT32_MAX;
    commit_record();
    simulate_reset(ESP_RST_PANIC, image_a, false);
    assert(s_status.consecutive_faults == 0U);
    s_record.consecutive_faults = UINT8_MAX + 1U;
    commit_record();
    simulate_reset(ESP_RST_PANIC, image_a, false);
    assert(s_status.consecutive_faults == 0U);
}

static void test_planned_restart_is_consumed_once(void)
{
    for (unsigned order = 0U; order < 2U; ++order) {
        clean_boot();
        if (order == 0U) {
            boot_recovery_note_planned_restart();
            boot_recovery_mark_healthy();
        } else {
            boot_recovery_mark_healthy();
            boot_recovery_note_planned_restart();
        }
        simulate_reset(ESP_RST_SW, image_a, false);
        assert(s_status.consecutive_faults == 0U);
        simulate_reset(ESP_RST_SW, image_a, false);
        assert(s_status.consecutive_faults == 1U);
    }
    clean_boot();
    boot_recovery_note_planned_restart();
    simulate_reset(ESP_RST_PANIC, image_a, false);
    assert(s_status.consecutive_faults == 1U);
}

static void test_health_manual_and_idempotence(void)
{
    clean_boot();
    simulate_reset(ESP_RST_PANIC, image_a, false);
    boot_recovery_set_phase(BOOT_RECOVERY_PHASE_HEALTHY);
    assert(s_status.current_phase != BOOT_RECOVERY_PHASE_HEALTHY);
    boot_recovery_set_phase((boot_recovery_phase_t)-1);
    assert(record_valid(&s_record));
    boot_recovery_mark_healthy();
    assert(s_status.consecutive_faults == 0U);
    assert(s_status.current_phase == BOOT_RECOVERY_PHASE_HEALTHY);
    simulate_reset(ESP_RST_PANIC, image_a, false);
    assert(s_status.consecutive_faults == 1U);
    simulate_reset(ESP_RST_PANIC, image_a, true);
    assert(s_status.manual_recovery && boot_recovery_is_active());
    assert(s_status.consecutive_faults == 1U);
    boot_recovery_begin(ESP_RST_POWERON, image_b, false);
    assert(s_status.manual_recovery && s_status.consecutive_faults == 1U);
    assert(memcmp(s_record.image_fingerprint, image_a,
                  BOOT_RECOVERY_IMAGE_FINGERPRINT_BYTES) == 0);
}

int main(void)
{
    test_crash_sequence_and_diagnostics();
    test_reset_classes_and_image_change();
    test_record_validation();
    test_planned_restart_is_consumed_once();
    test_health_manual_and_idempotence();
    puts("boot recovery record tests passed");
    return 0;
}
