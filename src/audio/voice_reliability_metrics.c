#include "voice_reliability_metrics.h"

#include <limits.h>
#include <string.h>

static uint32_t lower(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static void update_minimum(voice_reliability_resources_t *minimum,
                           const voice_reliability_resources_t *observed)
{
    minimum->internal_free_bytes = lower(
        minimum->internal_free_bytes, observed->internal_free_bytes);
    minimum->internal_largest_block_bytes = lower(
        minimum->internal_largest_block_bytes,
        observed->internal_largest_block_bytes);
    minimum->psram_free_bytes = lower(
        minimum->psram_free_bytes, observed->psram_free_bytes);
    minimum->psram_largest_block_bytes = lower(
        minimum->psram_largest_block_bytes,
        observed->psram_largest_block_bytes);
    minimum->worker_stack_hwm_bytes = lower(
        minimum->worker_stack_hwm_bytes,
        observed->worker_stack_hwm_bytes);
}

static void increment_saturated(uint32_t *value)
{
    if (*value < UINT32_MAX) {
        ++*value;
    }
}

void voice_reliability_summary_init(voice_reliability_summary_t *summary)
{
    if (summary != NULL) {
        memset(summary, 0, sizeof(*summary));
    }
}

void voice_reliability_sample_init(
    voice_reliability_sample_t *sample, uint32_t generation,
    const voice_reliability_resources_t *begin)
{
    if (sample == NULL || begin == NULL) {
        return;
    }
    memset(sample, 0, sizeof(*sample));
    sample->generation = generation;
    sample->begin = *begin;
    sample->minimum = *begin;
    sample->end = *begin;
}

void voice_reliability_sample_observe(
    voice_reliability_sample_t *sample,
    const voice_reliability_resources_t *resources)
{
    if (sample == NULL || resources == NULL) {
        return;
    }
    update_minimum(&sample->minimum, resources);
}

bool voice_reliability_summary_record(
    voice_reliability_summary_t *summary,
    const voice_reliability_sample_t *sample)
{
    if (summary == NULL || sample == NULL || sample->generation == 0U ||
        sample->outcome <= VOICE_RELIABILITY_OUTCOME_NONE ||
        sample->outcome > VOICE_RELIABILITY_OUTCOME_FAILED) {
        return false;
    }

    voice_reliability_sample_t accepted = *sample;
    accepted.sequence = summary->sessions == UINT32_MAX
                            ? UINT32_MAX
                            : summary->sessions + 1U;
    if (summary->sessions == 0U) {
        summary->first_end = accepted.end;
        summary->minimum = accepted.minimum;
    } else {
        update_minimum(&summary->minimum, &accepted.minimum);
    }
    summary->latest_end = accepted.end;
    summary->last = accepted;
    increment_saturated(&summary->sessions);

    switch (accepted.outcome) {
    case VOICE_RELIABILITY_OUTCOME_MATCHED:
        increment_saturated(&summary->matched);
        break;
    case VOICE_RELIABILITY_OUTCOME_NO_VOICE:
        increment_saturated(&summary->no_voice);
        break;
    case VOICE_RELIABILITY_OUTCOME_NOT_UNDERSTOOD:
        increment_saturated(&summary->not_understood);
        break;
    case VOICE_RELIABILITY_OUTCOME_CANCELLED:
        increment_saturated(&summary->cancelled);
        break;
    case VOICE_RELIABILITY_OUTCOME_FAILED:
        increment_saturated(&summary->failed);
        break;
    case VOICE_RELIABILITY_OUTCOME_NONE:
    default:
        return false;
    }

    if (accepted.capture_ms > summary->capture_max_ms) {
        summary->capture_max_ms = accepted.capture_ms;
    }
    if (accepted.wall_ms > summary->wall_max_ms) {
        summary->wall_max_ms = accepted.wall_ms;
    }
    if (accepted.final_error != 0) {
        increment_saturated(&summary->error_sessions);
    }
    if (!accepted.cpu_lock_acquired) {
        increment_saturated(&summary->cpu_lock_acquire_failures);
    } else if (!accepted.cpu_lock_released) {
        increment_saturated(&summary->cpu_lock_release_failures);
    }
    return true;
}

int64_t voice_reliability_trend_bytes(uint32_t first,
                                      uint32_t latest)
{
    return (int64_t)latest - (int64_t)first;
}

const char *voice_reliability_outcome_name(
    voice_reliability_outcome_t outcome)
{
    switch (outcome) {
    case VOICE_RELIABILITY_OUTCOME_MATCHED:
        return "matched";
    case VOICE_RELIABILITY_OUTCOME_NO_VOICE:
        return "no_voice";
    case VOICE_RELIABILITY_OUTCOME_NOT_UNDERSTOOD:
        return "not_understood";
    case VOICE_RELIABILITY_OUTCOME_CANCELLED:
        return "cancelled";
    case VOICE_RELIABILITY_OUTCOME_FAILED:
        return "failed";
    case VOICE_RELIABILITY_OUTCOME_NONE:
    default:
        return "none";
    }
}
