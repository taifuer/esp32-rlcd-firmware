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

static void assert_filtered(const environment_comfort_tracker_t *tracker,
                            float temperature_c, float humidity_percent)
{
    float actual_temperature_c = 0.0f;
    float actual_humidity_percent = 0.0f;
    assert(environment_comfort_filtered_measurement(
        tracker, &actual_temperature_c, &actual_humidity_percent));
    assert(fabsf(actual_temperature_c - temperature_c) < 0.001f);
    assert(fabsf(actual_humidity_percent - humidity_percent) < 0.001f);
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
                55000U, 5000U) == ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_update(&tracker, 40.0f, 95.0f,
                                      60000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);

    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;
    assert(environment_comfort_filtered_measurement(
        &tracker, &temperature_c, &humidity_percent));
    assert(temperature_c == 24.0f);
    assert(humidity_percent == 50.0f);
}

static void test_filtered_measurement_requires_samples(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);
    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;

    assert(!environment_comfort_filtered_measurement(
        &tracker, &temperature_c, &humidity_percent));
    assert(!environment_comfort_filtered_measurement(
        NULL, &temperature_c, &humidity_percent));
    assert(!environment_comfort_filtered_measurement(
        &tracker, NULL, &humidity_percent));
}

static void test_first_measurement_is_immediate(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);
    assert(environment_comfort_update(&tracker, 23.5f, 48.0f, 1234U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(tracker.sample_count == 1U);
    assert_filtered(&tracker, 23.5f, 48.0f);

    environment_comfort_reset(&tracker);
    assert(environment_comfort_update(&tracker, 32.0f, 80.0f, 2345U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
    assert_filtered(&tracker, 32.0f, 80.0f);
}

static void test_slow_cadences_reject_isolated_spike(void)
{
    const uint32_t intervals[] = {30000U, 60000U};
    const uint32_t jitter[] = {0U, 1700U};
    for (size_t cadence = 0; cadence < 2U; ++cadence) {
        for (size_t drift = 0; drift < 2U; ++drift) {
            environment_comfort_tracker_t tracker;
            environment_comfort_init(&tracker);
            const uint32_t interval = intervals[cadence] + jitter[drift];
            for (uint32_t sample = 0; sample < 3U; ++sample) {
                assert(environment_comfort_update(&tracker, 24.0f, 50.0f,
                    sample * interval) == ENVIRONMENT_COMFORT_COMFORTABLE);
            }
            assert(tracker.sample_count == 3U);
            assert(environment_comfort_update(&tracker, 40.0f, 95.0f,
                3U * interval) == ENVIRONMENT_COMFORT_COMFORTABLE);
            assert(tracker.sample_count == 3U);
            assert_filtered(&tracker, 24.0f, 50.0f);
            for (uint32_t sample = 4U; sample < 7U; ++sample) {
                assert(environment_comfort_update(&tracker, 24.0f, 50.0f,
                    sample * interval) == ENVIRONMENT_COMFORT_COMFORTABLE);
                assert_filtered(&tracker, 24.0f, 50.0f);
            }
        }
    }
}

static void test_mode_change_keeps_filter_history(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);
    /* Normal -> saving -> normal, with real-world scheduling drift. */
    const uint32_t times[] = {
        0U, 30017U, 60044U, 120200U, 180500U, 210700U, 240900U,
    };
    for (size_t sample = 0; sample < sizeof(times) / sizeof(times[0]); ++sample) {
        const bool spike = sample == 4U;
        assert(environment_comfort_update(&tracker,
            spike ? 40.0f : 24.0f, spike ? 95.0f : 50.0f, times[sample]) ==
            ENVIRONMENT_COMFORT_COMFORTABLE);
        assert_filtered(&tracker, 24.0f, 50.0f);
        assert(tracker.sample_count <= 3U);
    }
}

static void test_slow_cadences_keep_transition_delays(void)
{
    const uint32_t intervals[] = {30000U, 31700U, 60000U, 61700U};
    for (size_t cadence = 0;
         cadence < sizeof(intervals) / sizeof(intervals[0]); ++cadence) {
        environment_comfort_tracker_t tracker;
        environment_comfort_init(&tracker);
        const uint32_t interval = intervals[cadence];
        uint32_t time_ms = 0U;
        for (uint32_t sample = 0; sample < 3U; ++sample) {
            (void)environment_comfort_update(&tracker, 24.0f, 50.0f, time_ms);
            time_ms += interval;
        }
        const uint32_t worsening_started_ms = time_ms;
        environment_comfort_level_t level = ENVIRONMENT_COMFORT_COMFORTABLE;
        for (uint32_t sample = 0; sample < 8U; ++sample) {
            level = environment_comfort_update(&tracker, 32.0f, 80.0f, time_ms);
            if (level == ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT) break;
            time_ms += interval;
        }
        assert(level == ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
        assert(time_ms - worsening_started_ms >= 30000U);
        assert(time_ms - worsening_started_ms <= 3U * interval);

        time_ms += interval;
        const uint32_t recovery_started_ms = time_ms;
        for (uint32_t sample = 0; sample < 10U; ++sample) {
            level = environment_comfort_update(&tracker, 24.0f, 50.0f, time_ms);
            if (level == ENVIRONMENT_COMFORT_COMFORTABLE) break;
            time_ms += interval;
        }
        assert(level == ENVIRONMENT_COMFORT_COMFORTABLE);
        assert(time_ms - recovery_started_ms >= 120000U);
        assert(time_ms - recovery_started_ms <= 120000U + 3U * interval);
    }
}

static void test_short_failure_keeps_valid_samples(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);
    for (uint32_t sample = 0; sample < 3U; ++sample) {
        (void)environment_comfort_update(&tracker, 24.0f, 50.0f,
                                          sample * 30100U);
    }
    environment_comfort_mark_invalid(&tracker);
    assert(tracker.sample_count == 3U);
    assert(tracker.last_sample_ms == 60200U);
    assert_filtered(&tracker, 24.0f, 50.0f);
    assert(environment_comfort_update(&tracker, NAN, 50.0f, 90300U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(tracker.last_sample_ms == 60200U);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 120400U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert_filtered(&tracker, 24.0f, 50.0f);
}

static void test_filter_age_and_pause_boundary(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);
    (void)environment_comfort_update(&tracker, 24.0f, 50.0f, 0U);
    (void)environment_comfort_update(&tracker, 24.0f, 50.0f, 60000U);
    (void)environment_comfort_update(&tracker, 40.0f, 95.0f, 179999U);
    assert(tracker.sample_count == 3U);
    assert_filtered(&tracker, 24.0f, 50.0f);
    (void)environment_comfort_update(&tracker, 40.0f, 95.0f, 180000U);
    assert(tracker.sample_count == 3U);
    assert(tracker.samples[0].timestamp_ms == 60000U);
    assert_filtered(&tracker, 40.0f, 95.0f);

    environment_comfort_reset(&tracker);
    (void)environment_comfort_update(&tracker, 24.0f, 50.0f, 0U);
    /* A three-minute pause rebuilds filtering but does not count as a
     * continuously observed worsening transition. */
    assert(environment_comfort_update(&tracker, 40.0f, 95.0f, 180000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(tracker.sample_count == 1U);
    assert(tracker.observation_started_ms == 180000U);
    assert_filtered(&tracker, 40.0f, 95.0f);
    assert(environment_comfort_update(&tracker, 40.0f, 95.0f, 240001U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_update(&tracker, 40.0f, 95.0f, 300002U) ==
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);

    /* main resets comfort after a failed observation reaches 180 seconds;
     * the next success must show the newly measured environment immediately. */
    environment_comfort_mark_invalid(&tracker);
    environment_comfort_reset(&tracker);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 480500U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(tracker.sample_count == 1U);
    assert_filtered(&tracker, 24.0f, 50.0f);
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
           ENVIRONMENT_COMFORT_COMFORTABLE);
    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 60000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);

    assert(environment_comfort_update(&tracker, 24.0f, -1.0f, 65000U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
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
           ENVIRONMENT_COMFORT_COMFORTABLE);
}

static void test_saving_transition_bounds(void)
{
    environment_comfort_tracker_t tracker;
    environment_comfort_init(&tracker);

    assert(environment_comfort_update(&tracker, 24.0f, 50.0f, 0U) ==
           ENVIRONMENT_COMFORT_COMFORTABLE);
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
           ENVIRONMENT_COMFORT_COMFORTABLE);
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
           ENVIRONMENT_COMFORT_FAIR);
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
           ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT);
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
    test_filtered_measurement_requires_samples();
    test_first_measurement_is_immediate();
    test_slow_cadences_reject_isolated_spike();
    test_mode_change_keeps_filter_history();
    test_slow_cadences_keep_transition_delays();
    test_short_failure_keeps_valid_samples();
    test_filter_age_and_pause_boundary();
    test_hysteresis_and_transition_delays();
    test_saving_interval_and_invalid_sample();
    test_saving_transition_bounds();
    test_timestamp_wrap();
    test_outer_hysteresis();
    puts("environment comfort tests passed");
    return 0;
}
