#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "environment_observation.h"

static void test_no_success_is_not_displayable(void)
{
    environment_observation_t observation;
    environment_observation_init(&observation);

    environment_observation_status_t status =
        environment_observation_evaluate(&observation, 12345U);
    assert(!status.display_valid);
    assert(!status.stale);
    assert(status.last_success_ms == 0U);
    assert(status.age_ms == 0U);
    assert(status.consecutive_failures == 0U);

    environment_observation_record_failure(&observation);
    status = environment_observation_evaluate(&observation, 12346U);
    assert(!status.display_valid);
    assert(!status.stale);
    assert(status.consecutive_failures == 1U);
}

static void test_success_is_fresh(void)
{
    environment_observation_t observation;
    environment_observation_init(&observation);
    environment_observation_record_success(&observation, 1000U);

    const environment_observation_status_t status =
        environment_observation_evaluate(&observation, 2500U);
    assert(status.display_valid);
    assert(!status.stale);
    assert(status.last_success_ms == 1000U);
    assert(status.age_ms == 1500U);
    assert(status.consecutive_failures == 0U);
}

static void test_transient_failure_retains_last_good(void)
{
    environment_observation_t observation;
    environment_observation_init(&observation);
    environment_observation_record_success(&observation, 2000U);
    environment_observation_record_failure(&observation);
    environment_observation_record_failure(&observation);

    const environment_observation_status_t status =
        environment_observation_evaluate(&observation, 7000U);
    assert(status.display_valid);
    assert(status.stale);
    assert(status.last_success_ms == 2000U);
    assert(status.age_ms == 5000U);
    assert(status.consecutive_failures == 2U);
}

static void test_recovery_replaces_timestamp_and_clears_failures(void)
{
    environment_observation_t observation;
    environment_observation_init(&observation);
    environment_observation_record_success(&observation, 1000U);
    environment_observation_record_failure(&observation);
    environment_observation_record_failure(&observation);
    environment_observation_record_success(&observation, 9000U);

    const environment_observation_status_t status =
        environment_observation_evaluate(&observation, 9100U);
    assert(status.display_valid);
    assert(!status.stale);
    assert(status.last_success_ms == 9000U);
    assert(status.age_ms == 100U);
    assert(status.consecutive_failures == 0U);
}

static void test_stale_window_boundary(void)
{
    environment_observation_t observation;
    environment_observation_init(&observation);
    environment_observation_record_success(&observation, 1000U);
    environment_observation_record_failure(&observation);

    environment_observation_status_t status =
        environment_observation_evaluate(
            &observation,
            1000U + ENVIRONMENT_OBSERVATION_STALE_WINDOW_MS - 1U);
    assert(status.display_valid);
    assert(status.stale);
    assert(status.age_ms ==
           ENVIRONMENT_OBSERVATION_STALE_WINDOW_MS - 1U);

    status = environment_observation_evaluate(
        &observation,
        1000U + ENVIRONMENT_OBSERVATION_STALE_WINDOW_MS);
    assert(!status.display_valid);
    assert(!status.stale);
    assert(status.age_ms == ENVIRONMENT_OBSERVATION_STALE_WINDOW_MS);
    assert(status.consecutive_failures == 1U);
}

static void test_timestamp_wrap(void)
{
    environment_observation_t observation;
    environment_observation_init(&observation);
    const uint32_t success_ms = UINT32_MAX - 100000U;
    environment_observation_record_success(&observation, success_ms);
    environment_observation_record_failure(&observation);

    environment_observation_status_t status =
        environment_observation_evaluate(&observation, 79998U);
    assert(status.display_valid);
    assert(status.stale);
    assert(status.age_ms == 179999U);

    status = environment_observation_evaluate(&observation, 79999U);
    assert(!status.display_valid);
    assert(!status.stale);
    assert(status.age_ms == 180000U);
}

static void test_failure_counter_saturates(void)
{
    environment_observation_t observation;
    environment_observation_init(&observation);

    for (uint32_t count = 0U; count < 300U; ++count) {
        environment_observation_record_failure(&observation);
    }
    environment_observation_status_t status =
        environment_observation_evaluate(&observation, 0U);
    assert(status.consecutive_failures == UINT8_MAX);

    environment_observation_record_failure(&observation);
    status = environment_observation_evaluate(&observation, 0U);
    assert(status.consecutive_failures == UINT8_MAX);
}

int main(void)
{
    test_no_success_is_not_displayable();
    test_success_is_fresh();
    test_transient_failure_retains_last_good();
    test_recovery_replaces_timestamp_and_clears_failures();
    test_stale_window_boundary();
    test_timestamp_wrap();
    test_failure_counter_saturates();
    puts("environment observation tests passed");
    return 0;
}
