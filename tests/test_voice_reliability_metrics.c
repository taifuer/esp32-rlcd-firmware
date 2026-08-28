#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "voice_reliability_metrics.h"

static voice_reliability_resources_t resources(
    uint32_t internal_free, uint32_t internal_largest,
    uint32_t psram_free, uint32_t psram_largest,
    uint32_t stack_hwm)
{
    const voice_reliability_resources_t value = {
        .internal_free_bytes = internal_free,
        .internal_largest_block_bytes = internal_largest,
        .psram_free_bytes = psram_free,
        .psram_largest_block_bytes = psram_largest,
        .worker_stack_hwm_bytes = stack_hwm,
    };
    return value;
}

static voice_reliability_sample_t sample(
    uint32_t generation, voice_reliability_outcome_t outcome,
    voice_reliability_resources_t begin,
    voice_reliability_resources_t working,
    voice_reliability_resources_t end)
{
    voice_reliability_sample_t value;
    voice_reliability_sample_init(&value, generation, &begin);
    voice_reliability_sample_observe(&value, &working);
    voice_reliability_sample_observe(&value, &end);
    value.end = end;
    value.outcome = outcome;
    value.command_id = outcome == VOICE_RELIABILITY_OUTCOME_MATCHED
                           ? 2
                           : 0;
    value.confidence_permille =
        outcome == VOICE_RELIABILITY_OUTCOME_MATCHED ? 875U : 0U;
    value.capture_ms = 1250U;
    value.wall_ms = 1400U;
    value.cpu_lock_acquired = true;
    value.cpu_lock_released = true;
    return value;
}

static void test_summary_and_trends(void)
{
    voice_reliability_summary_t summary;
    voice_reliability_summary_init(&summary);

    voice_reliability_sample_t first = sample(
        10U, VOICE_RELIABILITY_OUTCOME_MATCHED,
        resources(100000U, 60000U, 7000000U, 6500000U, 3900U),
        resources(92000U, 52000U, 6980000U, 6480000U, 3600U),
        resources(99500U, 59500U, 6999000U, 6499000U, 3600U));
    assert(voice_reliability_summary_record(&summary, &first));
    assert(summary.sessions == 1U);
    assert(summary.matched == 1U);
    assert(summary.last.sequence == 1U);
    assert(summary.first_end.internal_free_bytes == 99500U);
    assert(summary.minimum.internal_free_bytes == 92000U);
    assert(summary.minimum.worker_stack_hwm_bytes == 3600U);

    voice_reliability_sample_t second = sample(
        11U, VOICE_RELIABILITY_OUTCOME_NO_VOICE,
        resources(99400U, 59400U, 6998900U, 6498900U, 3600U),
        resources(91000U, 50000U, 6970000U, 6470000U, 3400U),
        resources(99200U, 59000U, 6998500U, 6498500U, 3400U));
    second.capture_ms = 5000U;
    second.wall_ms = 5200U;
    assert(voice_reliability_summary_record(&summary, &second));
    assert(summary.sessions == 2U);
    assert(summary.no_voice == 1U);
    assert(summary.last.sequence == 2U);
    assert(summary.capture_max_ms == 5000U);
    assert(summary.wall_max_ms == 5200U);
    assert(summary.minimum.internal_free_bytes == 91000U);
    assert(summary.minimum.internal_largest_block_bytes == 50000U);
    assert(summary.minimum.psram_free_bytes == 6970000U);
    assert(summary.minimum.worker_stack_hwm_bytes == 3400U);
    assert(voice_reliability_trend_bytes(
               summary.first_end.internal_free_bytes,
               summary.latest_end.internal_free_bytes) == -300);
    assert(voice_reliability_trend_bytes(100U, 150U) == 50);
}

static void test_all_outcomes_and_lock_failures(void)
{
    voice_reliability_summary_t summary;
    voice_reliability_summary_init(&summary);
    const voice_reliability_resources_t stable =
        resources(100U, 90U, 0U, 0U, 80U);
    const voice_reliability_outcome_t outcomes[] = {
        VOICE_RELIABILITY_OUTCOME_MATCHED,
        VOICE_RELIABILITY_OUTCOME_NO_VOICE,
        VOICE_RELIABILITY_OUTCOME_NOT_UNDERSTOOD,
        VOICE_RELIABILITY_OUTCOME_CANCELLED,
        VOICE_RELIABILITY_OUTCOME_FAILED,
    };
    for (size_t index = 0U;
         index < sizeof(outcomes) / sizeof(outcomes[0]); ++index) {
        voice_reliability_sample_t value = sample(
            (uint32_t)index + 1U, outcomes[index], stable, stable,
            stable);
        if (index == 3U) {
            value.cpu_lock_acquired = false;
            value.cpu_lock_released = false;
            value.final_error = -2;
        }
        if (index == 4U) {
            value.cpu_lock_released = false;
            value.final_error = -1;
        }
        assert(voice_reliability_summary_record(&summary, &value));
    }
    assert(summary.sessions == 5U);
    assert(summary.matched == 1U);
    assert(summary.no_voice == 1U);
    assert(summary.not_understood == 1U);
    assert(summary.cancelled == 1U);
    assert(summary.failed == 1U);
    assert(summary.error_sessions == 2U);
    assert(summary.cpu_lock_acquire_failures == 1U);
    assert(summary.cpu_lock_release_failures == 1U);
    assert(summary.minimum.psram_free_bytes == 0U);
}

static void test_invalid_inputs_reset_and_saturation(void)
{
    voice_reliability_summary_t summary;
    voice_reliability_summary_init(&summary);
    const voice_reliability_resources_t stable =
        resources(10U, 9U, 8U, 7U, 6U);
    voice_reliability_sample_t value = sample(
        1U, VOICE_RELIABILITY_OUTCOME_NONE, stable, stable, stable);
    assert(!voice_reliability_summary_record(&summary, &value));
    value.outcome = VOICE_RELIABILITY_OUTCOME_FAILED;
    value.generation = 0U;
    assert(!voice_reliability_summary_record(&summary, &value));
    assert(!voice_reliability_summary_record(NULL, &value));
    assert(!voice_reliability_summary_record(&summary, NULL));

    value.generation = 1U;
    summary.sessions = UINT32_MAX;
    summary.failed = UINT32_MAX;
    assert(voice_reliability_summary_record(&summary, &value));
    assert(summary.sessions == UINT32_MAX);
    assert(summary.failed == UINT32_MAX);
    assert(summary.last.sequence == UINT32_MAX);

    voice_reliability_summary_init(&summary);
    assert(summary.sessions == 0U);
    voice_reliability_summary_init(NULL);
    voice_reliability_sample_init(NULL, 1U, &stable);
    voice_reliability_sample_init(&value, 1U, NULL);
    voice_reliability_sample_observe(NULL, &stable);
    voice_reliability_sample_observe(&value, NULL);
    assert(voice_reliability_outcome_name(
               VOICE_RELIABILITY_OUTCOME_MATCHED) != NULL);
    assert(voice_reliability_outcome_name(
               (voice_reliability_outcome_t)99) != NULL);
}

static void test_fifty_records_and_reset(void)
{
    voice_reliability_summary_t summary;
    voice_reliability_summary_init(&summary);
    for (uint32_t index = 0U; index < 50U; ++index) {
        const voice_reliability_resources_t begin = resources(
            200000U - index * 10U, 120000U - index * 8U,
            7000000U - index * 20U, 6800000U - index * 16U,
            5000U - index);
        const voice_reliability_resources_t working = resources(
            begin.internal_free_bytes - 1000U,
            begin.internal_largest_block_bytes - 500U,
            begin.psram_free_bytes - 2000U,
            begin.psram_largest_block_bytes - 1500U,
            begin.worker_stack_hwm_bytes - 100U);
        const voice_reliability_resources_t end = resources(
            begin.internal_free_bytes - 100U,
            begin.internal_largest_block_bytes - 50U,
            begin.psram_free_bytes - 200U,
            begin.psram_largest_block_bytes - 150U,
            begin.worker_stack_hwm_bytes - 100U);
        const voice_reliability_outcome_t outcome =
            index < 33U ? VOICE_RELIABILITY_OUTCOME_MATCHED
            : index < 38U
                ? VOICE_RELIABILITY_OUTCOME_NOT_UNDERSTOOD
            : index < 42U ? VOICE_RELIABILITY_OUTCOME_NO_VOICE
                          : VOICE_RELIABILITY_OUTCOME_CANCELLED;
        voice_reliability_sample_t value = sample(
            index + 1U, outcome, begin, working, end);
        assert(voice_reliability_summary_record(&summary, &value));
    }

    assert(summary.sessions == 50U);
    assert(summary.matched == 33U);
    assert(summary.no_voice == 4U);
    assert(summary.not_understood == 5U);
    assert(summary.cancelled == 8U);
    assert(summary.failed == 0U);
    assert(summary.error_sessions == 0U);
    assert(summary.matched + summary.no_voice +
               summary.not_understood + summary.cancelled +
               summary.failed ==
           summary.sessions);
    assert(summary.last.sequence == 50U);
    assert(summary.latest_end.internal_free_bytes == 199410U);
    assert(summary.minimum.internal_free_bytes == 198510U);
    assert(summary.minimum.worker_stack_hwm_bytes == 4851U);
    assert(voice_reliability_trend_bytes(
               summary.first_end.internal_free_bytes,
               summary.latest_end.internal_free_bytes) == -490);

    voice_reliability_summary_init(&summary);
    assert(summary.sessions == 0U);
    const voice_reliability_resources_t stable =
        resources(100U, 90U, 80U, 70U, 60U);
    voice_reliability_sample_t after_reset = sample(
        51U, VOICE_RELIABILITY_OUTCOME_MATCHED, stable, stable,
        stable);
    assert(voice_reliability_summary_record(&summary, &after_reset));
    assert(summary.sessions == 1U);
    assert(summary.last.sequence == 1U);
}

int main(void)
{
    test_summary_and_trends();
    test_all_outcomes_and_lock_failures();
    test_invalid_inputs_reset_and_saturation();
    test_fifty_records_and_reset();
    puts("voice reliability metrics tests passed");
    return 0;
}
