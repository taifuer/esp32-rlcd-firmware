#include "environment_observation.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

void environment_observation_init(environment_observation_t *observation)
{
    if (observation == NULL) {
        return;
    }
    memset(observation, 0, sizeof(*observation));
}

void environment_observation_record_success(
    environment_observation_t *observation, uint32_t timestamp_ms)
{
    if (observation == NULL) {
        return;
    }
    observation->has_success = true;
    observation->last_success_ms = timestamp_ms;
    observation->consecutive_failures = 0U;
}

void environment_observation_record_failure(
    environment_observation_t *observation)
{
    if (observation == NULL) {
        return;
    }
    if (observation->consecutive_failures < UINT8_MAX) {
        ++observation->consecutive_failures;
    }
}

environment_observation_status_t environment_observation_evaluate(
    const environment_observation_t *observation, uint32_t timestamp_ms)
{
    environment_observation_status_t status = {0};
    if (observation == NULL) {
        return status;
    }

    status.last_success_ms = observation->last_success_ms;
    status.consecutive_failures = observation->consecutive_failures;
    if (!observation->has_success) {
        return status;
    }

    status.age_ms = timestamp_ms - observation->last_success_ms;
    if (observation->consecutive_failures == 0U) {
        status.display_valid = true;
        return status;
    }

    if (status.age_ms < ENVIRONMENT_OBSERVATION_STALE_WINDOW_MS) {
        status.display_valid = true;
        status.stale = true;
    }
    return status;
}
