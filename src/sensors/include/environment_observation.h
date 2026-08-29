#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ENVIRONMENT_OBSERVATION_STALE_WINDOW_MS = 180000U,
};

typedef struct {
    bool has_success;
    uint32_t last_success_ms;
    uint8_t consecutive_failures;
} environment_observation_t;

typedef struct {
    bool display_valid;
    bool stale;
    uint32_t last_success_ms;
    uint32_t age_ms;
    uint8_t consecutive_failures;
} environment_observation_status_t;

void environment_observation_init(environment_observation_t *observation);

/* Records a usable measurement and starts a new freshness window. */
void environment_observation_record_success(
    environment_observation_t *observation, uint32_t timestamp_ms);

/* Records one failed attempt. The counter saturates at UINT8_MAX. */
void environment_observation_record_failure(
    environment_observation_t *observation);

/*
 * A failed observation may retain the last good value for less than
 * ENVIRONMENT_OBSERVATION_STALE_WINDOW_MS. Timestamp subtraction is
 * intentionally unsigned so the normal uint32_t millisecond wrap is safe.
 */
environment_observation_status_t environment_observation_evaluate(
    const environment_observation_t *observation, uint32_t timestamp_ms);

#ifdef __cplusplus
}
#endif
