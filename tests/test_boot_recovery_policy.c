#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "boot_recovery_policy.h"

static boot_recovery_evaluation_t valid_evaluation(void)
{
    return (boot_recovery_evaluation_t){
        .record_valid = true,
        .image_fingerprint_matches = true,
        .reset_class = BOOT_RECOVERY_RESET_OTHER,
    };
}

static void test_missing_or_stale_record_starts_clean(void)
{
    boot_recovery_decision_t decision = boot_recovery_evaluate(NULL);
    assert(decision.consecutive_faults == 0U);
    assert(!decision.enter_recovery);

    boot_recovery_evaluation_t evaluation = valid_evaluation();
    evaluation.record_valid = false;
    evaluation.previous_start_in_progress = true;
    evaluation.reset_class = BOOT_RECOVERY_RESET_FAULT;
    evaluation.consecutive_faults = UINT8_MAX;
    decision = boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 0U);
    assert(!decision.enter_recovery);

    evaluation.record_valid = true;
    evaluation.image_fingerprint_matches = false;
    decision = boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 0U);
    assert(!decision.enter_recovery);
}

static void test_power_reset_starts_clean(void)
{
    boot_recovery_evaluation_t evaluation = valid_evaluation();
    evaluation.previous_start_in_progress = true;
    evaluation.reset_class = BOOT_RECOVERY_RESET_POWER;
    evaluation.consecutive_faults = UINT8_MAX;

    const boot_recovery_decision_t decision =
        boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 0U);
    assert(!decision.enter_recovery);
}

static void test_faults_start_or_continue_a_sequence(void)
{
    boot_recovery_evaluation_t evaluation = valid_evaluation();
    evaluation.previous_start_in_progress = true;
    evaluation.reset_class = BOOT_RECOVERY_RESET_FAULT;
    evaluation.consecutive_faults = 1U;

    boot_recovery_decision_t decision =
        boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 2U);
    assert(!decision.enter_recovery);

    evaluation.previous_start_in_progress = false;
    decision = boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 1U);
    assert(!decision.enter_recovery);

    evaluation.previous_start_in_progress = true;
    evaluation.reset_class = BOOT_RECOVERY_RESET_EXPECTED;
    decision = boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 0U);
    assert(!decision.enter_recovery);

    evaluation.reset_class = BOOT_RECOVERY_RESET_OTHER;
    decision = boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 0U);
    assert(!decision.enter_recovery);
}

static void test_threshold_enters_recovery(void)
{
    boot_recovery_evaluation_t evaluation = valid_evaluation();
    evaluation.previous_start_in_progress = true;
    evaluation.reset_class = BOOT_RECOVERY_RESET_FAULT;
    evaluation.consecutive_faults = BOOT_RECOVERY_FAULT_THRESHOLD - 1U;

    const boot_recovery_decision_t decision =
        boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == BOOT_RECOVERY_FAULT_THRESHOLD);
    assert(decision.enter_recovery);
}

static void test_manual_recovery_does_not_add_a_fault(void)
{
    boot_recovery_evaluation_t evaluation = valid_evaluation();
    evaluation.previous_start_in_progress = true;
    evaluation.reset_class = BOOT_RECOVERY_RESET_FAULT;
    evaluation.manual_recovery_requested = true;
    evaluation.consecutive_faults = 1U;

    boot_recovery_decision_t decision =
        boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 1U);
    assert(decision.enter_recovery);

    evaluation.reset_class = BOOT_RECOVERY_RESET_POWER;
    decision = boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == 0U);
    assert(decision.enter_recovery);
}

static void test_fault_counter_saturates(void)
{
    boot_recovery_evaluation_t evaluation = valid_evaluation();
    evaluation.previous_start_in_progress = true;
    evaluation.reset_class = BOOT_RECOVERY_RESET_FAULT;
    evaluation.consecutive_faults = UINT8_MAX;

    const boot_recovery_decision_t decision =
        boot_recovery_evaluate(&evaluation);
    assert(decision.consecutive_faults == UINT8_MAX);
    assert(decision.enter_recovery);
}

static void test_healthy_confirmation_clears_state(void)
{
    const boot_recovery_decision_t current = {
        .consecutive_faults = UINT8_MAX,
        .enter_recovery = true,
    };
    const boot_recovery_decision_t healthy =
        boot_recovery_confirm_healthy(current);
    assert(healthy.consecutive_faults == 0U);
    assert(!healthy.enter_recovery);
}

static void test_ota_confirmation_gate(void)
{
    boot_recovery_ota_gate_t gate = {
        .startup_milestones_complete = true,
        .uptime_ms = BOOT_RECOVERY_OTA_CONFIRM_DELAY_MS,
        .first_attempt = true,
    };
    assert(boot_recovery_should_confirm_ota(&gate));

    gate.uptime_ms = BOOT_RECOVERY_OTA_CONFIRM_DELAY_MS - 1U;
    assert(!boot_recovery_should_confirm_ota(&gate));
    gate.uptime_ms = BOOT_RECOVERY_OTA_CONFIRM_DELAY_MS;
    gate.startup_milestones_complete = false;
    assert(!boot_recovery_should_confirm_ota(&gate));
    gate.startup_milestones_complete = true;
    gate.recovery_mode = true;
    assert(!boot_recovery_should_confirm_ota(&gate));
    gate.recovery_mode = false;
    gate.first_attempt = false;
    gate.since_last_attempt_ms =
        BOOT_RECOVERY_OTA_CONFIRM_RETRY_MS - 1U;
    assert(!boot_recovery_should_confirm_ota(&gate));
    gate.since_last_attempt_ms = BOOT_RECOVERY_OTA_CONFIRM_RETRY_MS;
    assert(boot_recovery_should_confirm_ota(&gate));
    assert(!boot_recovery_should_confirm_ota(NULL));
}

static void test_healthy_runtime_gate(void)
{
    assert(!boot_recovery_should_mark_healthy(
        false, true, BOOT_RECOVERY_HEALTHY_DELAY_MS - 1U));
    assert(!boot_recovery_should_mark_healthy(
        false, false, BOOT_RECOVERY_HEALTHY_DELAY_MS));
    assert(!boot_recovery_should_mark_healthy(
        true, true, BOOT_RECOVERY_HEALTHY_DELAY_MS));
    assert(boot_recovery_should_mark_healthy(
        false, true, BOOT_RECOVERY_HEALTHY_DELAY_MS));
}

int main(void)
{
    test_missing_or_stale_record_starts_clean();
    test_power_reset_starts_clean();
    test_faults_start_or_continue_a_sequence();
    test_threshold_enters_recovery();
    test_manual_recovery_does_not_add_a_fault();
    test_fault_counter_saturates();
    test_healthy_confirmation_clears_state();
    test_ota_confirmation_gate();
    test_healthy_runtime_gate();

    puts("boot recovery policy tests passed");
    return 0;
}
