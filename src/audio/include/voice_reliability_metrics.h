#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_RELIABILITY_OUTCOME_NONE = 0,
    VOICE_RELIABILITY_OUTCOME_MATCHED,
    VOICE_RELIABILITY_OUTCOME_NO_VOICE,
    VOICE_RELIABILITY_OUTCOME_NOT_UNDERSTOOD,
    VOICE_RELIABILITY_OUTCOME_CANCELLED,
    VOICE_RELIABILITY_OUTCOME_FAILED,
} voice_reliability_outcome_t;

typedef struct {
    uint32_t internal_free_bytes;
    uint32_t internal_largest_block_bytes;
    uint32_t psram_free_bytes;
    uint32_t psram_largest_block_bytes;
    /* ESP-IDF reports the task lifetime high-water mark in bytes. */
    uint32_t worker_stack_hwm_bytes;
} voice_reliability_resources_t;

typedef struct {
    uint32_t sequence;
    uint32_t generation;
    voice_reliability_outcome_t outcome;
    int32_t command_id;
    uint16_t confidence_permille;
    uint32_t capture_ms;
    uint32_t wall_ms;
    voice_reliability_resources_t begin;
    voice_reliability_resources_t minimum;
    voice_reliability_resources_t end;
    bool cpu_lock_acquired;
    bool cpu_lock_released;
    int32_t final_error;
} voice_reliability_sample_t;

typedef struct {
    uint32_t sessions;
    uint32_t matched;
    uint32_t no_voice;
    uint32_t not_understood;
    uint32_t cancelled;
    uint32_t failed;
    uint32_t error_sessions;
    uint32_t capture_max_ms;
    uint32_t wall_max_ms;
    uint32_t cpu_lock_acquire_failures;
    uint32_t cpu_lock_release_failures;
    voice_reliability_resources_t first_end;
    voice_reliability_resources_t latest_end;
    voice_reliability_resources_t minimum;
    voice_reliability_sample_t last;
} voice_reliability_summary_t;

void voice_reliability_summary_init(voice_reliability_summary_t *summary);
void voice_reliability_sample_init(
    voice_reliability_sample_t *sample, uint32_t generation,
    const voice_reliability_resources_t *begin);
void voice_reliability_sample_observe(
    voice_reliability_sample_t *sample,
    const voice_reliability_resources_t *resources);
bool voice_reliability_summary_record(
    voice_reliability_summary_t *summary,
    const voice_reliability_sample_t *sample);
int64_t voice_reliability_trend_bytes(uint32_t first,
                                      uint32_t latest);
const char *voice_reliability_outcome_name(
    voice_reliability_outcome_t outcome);

#ifdef __cplusplus
}
#endif
