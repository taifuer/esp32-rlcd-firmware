#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "environment_comfort.h"

static environment_comfort_level_t feed(
    environment_comfort_tracker_t *tracker,
    uint32_t *time_ms,
    float temperature_c,
    float humidity_percent,
    uint32_t duration_ms,
    uint32_t interval_ms)
{
    environment_comfort_level_t level = ENVIRONMENT_COMFORT_UNKNOWN;
    const uint32_t end_ms = *time_ms + duration_ms;
    do {
        level = environment_comfort_update(
            tracker, temperature_c, humidity_percent, *time_ms);
        *time_ms += interval_ms;
    } while (*time_ms <= end_ms);
    return level;
}

static void test_direct_classification(void)
{
    assert(environment_comfort_classify(20.0f, 40.0f) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_classify(26.0f, 60.0f) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_classify(19.9f, 50.0f) ==
           ENVIRONMENT_COMFORT_FAIR);
    assert(environment_comfort_classify(24.0f, 39.9f) ==
           ENVIRONMENT_COMFORT_FAIR);
    assert(environment_comfort_classify(16.0f, 30.0f) ==
           ENVIRONMENT_COMFORT_FAIR);
    assert(environment_comfort_classify(28.0f, 70.0f) ==
           ENVIRONMENT_COMFORT_FAIR);
    assert(environment_comfort_classify(15.9f, 50.0f) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    assert(environment_comfort_classify(24.0f, 70.1f) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    assert(environment_comfort_classify(31.2f, 73.0f) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    assert(environment_comfort_classify(NAN, 50.0f) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
    assert(environment_comfort_classify(24.0f, 101.0f) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
}

static void test_initial_observation_and_filter(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);
    uint32_t time_ms = 0U;

    assert(feed(&tracker, &time_ms, 24.0f, 50.0f,
                55000U, 5000U) == ENVIRONMENT_COMFORT_UNKNOWN);
    assert(environment_comfort_update(&tracker, 40.0f, 95.0f,
                                      60000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
}

static void test_hysteresis_and_transition_delays(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);
    uint32_t time_ms = 0U;

    assert(feed(&tracker, &time_ms, 24.0f, 50.0f,
                60000U, 5000U) == ENVIRONMENT_COMFORT_COMFORTABLE);

    assert(feed(&tracker, &time_ms, 26.4f, 62.0f,
                240000U, 5000U) == ENVIRONMENT_COMFORT_COMFORTABLE);

    environment_comfort_level_t level = ENVIRONMENT_COMFORT_UNKNOWN;
    uint32_t changed_at_ms = 0U;
    const uint32_t worsen_started_ms = time_ms;
    for (uint32_t index = 0U; index < 80U; ++index) {
        level = environment_comfort_update(&tracker, 31.2f, 73.0f,
                                           time_ms);
        if (level == ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT) {
            changed_at_ms = time_ms;
            break;
        }
        time_ms += 5000U;
    }
    assert(level == ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    assert(changed_at_ms - worsen_started_ms >= 30000U);
    assert(changed_at_ms - worsen_started_ms <= 120000U);

    const uint32_t recover_started_ms = time_ms;
    changed_at_ms = 0U;
    for (uint32_t index = 0U; index < 120U; ++index) {
        level = environment_comfort_update(&tracker, 24.0f, 50.0f,
                                           time_ms);
        if (level == ENVIRONMENT_COMFORT_COMFORTABLE) {
            changed_at_ms = time_ms;
            break;
        }
        time_ms += 5000U;
    }
    assert(level == ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(changed_at_ms - recover_started_ms >= 120000U);
    assert(changed_at_ms - recover_started_ms <= 240000U);
}

static void test_saving_interval_and_invalid_sample(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);

    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 0U) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 60000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);

    assert(environment_comfort_update(&tracker, 24.0f, -1.0f, 65000U) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
    assert(environment_comfort_update(&tracker, 31.2f, 73.0f, 70000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_update(&tracker, 31.2f, 73.0f, 130000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_update(&tracker, 31.2f, 73.0f, 190000U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);

    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 500001U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    environment_comfort_reset(&tracker);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 510000U) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
}

static void test_saving_transition_bounds(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);

    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 0U) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 60000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);

    assert(environment_comfort_update(&tracker, 31.2f, 73.0f, 120000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_update(&tracker, 31.2f, 73.0f, 180000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_update(&tracker, 31.2f, 73.0f, 240000U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);

    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 300000U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 360000U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 420000U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 480000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
}

static void test_timestamp_wrap(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);
    const uint32_t start_ms = UINT32_MAX - 60000U;

    assert(environment_comfort_update(&tracker, 24.0f, 50.0f,
                                      start_ms) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f,
                                      UINT32_MAX) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f,
                                      4999U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
}

static void test_outer_hysteresis(void)
{
    environment_comfort_tracker_t fair_tracker;
    environment_comfort_init(&fair_tracker);
    assert(environment_comfort_update(&fair_tracker, 18.0f, 50.0f, 0U) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
    assert(environment_comfort_update(&fair_tracker, 18.0f, 50.0f,
                                      60000U) ==
           ENVIRONMENT_COMFORT_FAIR);
    assert(environment_comfort_update(&fair_tracker, 28.4f, 72.0f,
                                      120000U) ==
           ENVIRONMENT_COMFORT_FAIR);
    assert(environment_comfort_update(&fair_tracker, 28.4f, 72.0f,
                                      180000U) ==
           ENVIRONMENT_COMFORT_FAIR);

    environment_comfort_tracker_t adjustment_tracker;
    environment_comfort_init(&adjustment_tracker);
    assert(environment_comfort_update(&adjustment_tracker, 31.2f, 73.0f,
                                      0U) ==
           ENVIRONMENT_COMFORT_UNKNOWN);
    assert(environment_comfort_update(&adjustment_tracker, 31.2f, 73.0f,
                                      60000U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    for (uint32_t time_ms = 120000U; time_ms <= 420000U;
         time_ms += 60000U) {
        assert(environment_comfort_update(&adjustment_tracker, 16.3f, 32.0f,
                                          time_ms) ==
               ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    }
}

int main(void)
{
    test_direct_classification();
    test_initial_observation_and_filter();
    test_hysteresis_and_transition_delays();
    test_saving_interval_and_invalid_sample();
    test_saving_transition_bounds();
    test_timestamp_wrap();
    test_outer_hysteresis();
    puts("environment comfort tests passed");
    return 0;
}
