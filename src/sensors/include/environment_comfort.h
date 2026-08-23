#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ENVIRONMENT_COMFORT_SAMPLE_CAPACITY = 16,
};

typedef enum {
    ENVIRONMENT_COMFORT_UNKNOWN = 0,
    ENVIRONMENT_COMFORT_COMFORTABLE,
    ENVIRONMENT_COMFORT_FAIR,
    ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT,
} environment_comfort_level_t;

typedef struct {
    float temperature_c;
    float humidity_percent;
    uint32_t timestamp_ms;
} environment_comfort_sample_t;

typedef struct {
    environment_comfort_sample_t samples[ENVIRONMENT_COMFORT_SAMPLE_CAPACITY];
    uint8_t sample_count;
    bool has_sample;
    uint32_t observation_started_ms;
    uint32_t last_sample_ms;
    environment_comfort_level_t level;
    environment_comfort_level_t candidate_level;
    uint32_t candidate_elapsed_ms;
} environment_comfort_tracker_t;

void environment_comfort_init(environment_comfort_tracker_t *tracker);
/* Clears samples, candidates, and the last published level. */
void environment_comfort_reset(environment_comfort_tracker_t *tracker);
/* Cancels a pending transition but preserves an established level. */
void environment_comfort_mark_invalid(
    environment_comfort_tracker_t *tracker);

environment_comfort_level_t environment_comfort_classify(
    float temperature_c, float humidity_percent);

environment_comfort_level_t environment_comfort_update(
    environment_comfort_tracker_t *tracker,
    float temperature_c,
    float humidity_percent,
    uint32_t timestamp_ms);

#ifdef __cplusplus
}
#endif
