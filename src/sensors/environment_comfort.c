#include "environment_comfort.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

enum {
    COMFORT_FILTER_WINDOW_MS = 60000U,
    COMFORT_INITIAL_OBSERVATION_MS = 60000U,
    COMFORT_WORSEN_DELAY_MS = 30000U,
    COMFORT_RECOVER_DELAY_MS = 120000U,
    COMFORT_MAX_SAMPLE_GAP_MS = 300000U,
    COMFORT_MAX_CANDIDATE_STEP_MS = 60000U,
};

static const float COMFORT_TEMPERATURE_LOW_C = 20.0f;
static const float COMFORT_TEMPERATURE_HIGH_C = 26.0f;
static const float FAIR_TEMPERATURE_LOW_C = 16.0f;
static const float FAIR_TEMPERATURE_HIGH_C = 28.0f;
static const float COMFORT_HUMIDITY_LOW_PERCENT = 40.0f;
static const float COMFORT_HUMIDITY_HIGH_PERCENT = 60.0f;
static const float FAIR_HUMIDITY_LOW_PERCENT = 30.0f;
static const float FAIR_HUMIDITY_HIGH_PERCENT = 70.0f;
static const float TEMPERATURE_HYSTERESIS_C = 0.5f;
static const float HUMIDITY_HYSTERESIS_PERCENT = 3.0f;

/* These bands are a product-level indoor environment hint, not a standard. */

static bool measurement_valid(float temperature_c,
                              float humidity_percent)
{
    return isfinite(temperature_c) && isfinite(humidity_percent) &&
           temperature_c >= -45.0f && temperature_c <= 130.0f &&
           humidity_percent >= 0.0f && humidity_percent <= 100.0f;
}

static bool in_range(float value, float low, float high)
{
    return value >= low && value <= high;
}

static bool environment_in_range(float temperature_c,
                                 float humidity_percent,
                                 float temperature_low,
                                 float temperature_high,
                                 float humidity_low,
                                 float humidity_high)
{
    return in_range(temperature_c, temperature_low, temperature_high) &&
           in_range(humidity_percent, humidity_low, humidity_high);
}

environment_comfort_level_t environment_comfort_classify(
    float temperature_c, float humidity_percent)
{
    if (!measurement_valid(temperature_c, humidity_percent)) {
        return ENVIRONMENT_COMFORT_UNKNOWN;
    }
    if (environment_in_range(
            temperature_c, humidity_percent,
            COMFORT_TEMPERATURE_LOW_C, COMFORT_TEMPERATURE_HIGH_C,
            COMFORT_HUMIDITY_LOW_PERCENT,
            COMFORT_HUMIDITY_HIGH_PERCENT)) {
        return ENVIRONMENT_COMFORT_COMFORTABLE;
    }
    if (environment_in_range(
            temperature_c, humidity_percent,
            FAIR_TEMPERATURE_LOW_C, FAIR_TEMPERATURE_HIGH_C,
            FAIR_HUMIDITY_LOW_PERCENT, FAIR_HUMIDITY_HIGH_PERCENT)) {
        return ENVIRONMENT_COMFORT_FAIR;
    }
    return ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT;
}

void environment_comfort_reset(environment_comfort_tracker_t *tracker)
{
    if (tracker == NULL) {
        return;
    }
    memset(tracker, 0, sizeof(*tracker));
    tracker->level = ENVIRONMENT_COMFORT_UNKNOWN;
    tracker->candidate_level = ENVIRONMENT_COMFORT_UNKNOWN;
}

void environment_comfort_init(environment_comfort_tracker_t *tracker)
{
    environment_comfort_reset(tracker);
}

void environment_comfort_mark_invalid(
    environment_comfort_tracker_t *tracker)
{
    if (tracker == NULL) {
        return;
    }
    tracker->candidate_level = ENVIRONMENT_COMFORT_UNKNOWN;
    tracker->candidate_elapsed_ms = 0U;
    if (tracker->level == ENVIRONMENT_COMFORT_UNKNOWN) {
        tracker->sample_count = 0U;
        tracker->has_sample = false;
        tracker->observation_started_ms = 0U;
        tracker->last_sample_ms = 0U;
    }
}

static void remove_oldest_sample(environment_comfort_tracker_t *tracker)
{
    if (tracker->sample_count == 0U) {
        return;
    }
    --tracker->sample_count;
    if (tracker->sample_count > 0U) {
        memmove(&tracker->samples[0], &tracker->samples[1],
                (size_t)tracker->sample_count * sizeof(tracker->samples[0]));
    }
}

static void add_sample(environment_comfort_tracker_t *tracker,
                       float temperature_c,
                       float humidity_percent,
                       uint32_t timestamp_ms)
{
    while (tracker->sample_count > 0U &&
           timestamp_ms - tracker->samples[0].timestamp_ms >
               COMFORT_FILTER_WINDOW_MS) {
        remove_oldest_sample(tracker);
    }
    if (tracker->sample_count == ENVIRONMENT_COMFORT_SAMPLE_CAPACITY) {
        remove_oldest_sample(tracker);
    }
    tracker->samples[tracker->sample_count++] =
        (environment_comfort_sample_t){
            .temperature_c = temperature_c,
            .humidity_percent = humidity_percent,
            .timestamp_ms = timestamp_ms,
        };
}

static void insertion_sort(float *values, uint8_t count)
{
    for (uint8_t index = 1U; index < count; ++index) {
        const float value = values[index];
        uint8_t position = index;
        while (position > 0U && values[position - 1U] > value) {
            values[position] = values[position - 1U];
            --position;
        }
        values[position] = value;
    }
}

static float median(float *values, uint8_t count)
{
    insertion_sort(values, count);
    const uint8_t middle = count / 2U;
    if ((count & 1U) != 0U) {
        return values[middle];
    }
    return (values[middle - 1U] + values[middle]) / 2.0f;
}

static void filtered_measurement(const environment_comfort_tracker_t *tracker,
                                 float *temperature_c,
                                 float *humidity_percent)
{
    float temperatures[ENVIRONMENT_COMFORT_SAMPLE_CAPACITY];
    float humidities[ENVIRONMENT_COMFORT_SAMPLE_CAPACITY];
    for (uint8_t index = 0U; index < tracker->sample_count; ++index) {
        temperatures[index] = tracker->samples[index].temperature_c;
        humidities[index] = tracker->samples[index].humidity_percent;
    }
    *temperature_c = median(temperatures, tracker->sample_count);
    *humidity_percent = median(humidities, tracker->sample_count);
}

static environment_comfort_level_t classify_with_hysteresis(
    environment_comfort_level_t current,
    float temperature_c,
    float humidity_percent)
{
    if (current == ENVIRONMENT_COMFORT_COMFORTABLE) {
        if (environment_in_range(
                temperature_c, humidity_percent,
                COMFORT_TEMPERATURE_LOW_C - TEMPERATURE_HYSTERESIS_C,
                COMFORT_TEMPERATURE_HIGH_C + TEMPERATURE_HYSTERESIS_C,
                COMFORT_HUMIDITY_LOW_PERCENT -
                    HUMIDITY_HYSTERESIS_PERCENT,
                COMFORT_HUMIDITY_HIGH_PERCENT +
                    HUMIDITY_HYSTERESIS_PERCENT)) {
            return ENVIRONMENT_COMFORT_COMFORTABLE;
        }
        return environment_comfort_classify(temperature_c,
                                            humidity_percent);
    }

    if (current == ENVIRONMENT_COMFORT_FAIR) {
        if (environment_in_range(
                temperature_c, humidity_percent,
                COMFORT_TEMPERATURE_LOW_C + TEMPERATURE_HYSTERESIS_C,
                COMFORT_TEMPERATURE_HIGH_C - TEMPERATURE_HYSTERESIS_C,
                COMFORT_HUMIDITY_LOW_PERCENT +
                    HUMIDITY_HYSTERESIS_PERCENT,
                COMFORT_HUMIDITY_HIGH_PERCENT -
                    HUMIDITY_HYSTERESIS_PERCENT)) {
            return ENVIRONMENT_COMFORT_COMFORTABLE;
        }
        if (!environment_in_range(
                temperature_c, humidity_percent,
                FAIR_TEMPERATURE_LOW_C - TEMPERATURE_HYSTERESIS_C,
                FAIR_TEMPERATURE_HIGH_C + TEMPERATURE_HYSTERESIS_C,
                FAIR_HUMIDITY_LOW_PERCENT - HUMIDITY_HYSTERESIS_PERCENT,
                FAIR_HUMIDITY_HIGH_PERCENT + HUMIDITY_HYSTERESIS_PERCENT)) {
            return ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT;
        }
        return ENVIRONMENT_COMFORT_FAIR;
    }

    if (current == ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT) {
        if (!environment_in_range(
                temperature_c, humidity_percent,
                FAIR_TEMPERATURE_LOW_C + TEMPERATURE_HYSTERESIS_C,
                FAIR_TEMPERATURE_HIGH_C - TEMPERATURE_HYSTERESIS_C,
                FAIR_HUMIDITY_LOW_PERCENT + HUMIDITY_HYSTERESIS_PERCENT,
                FAIR_HUMIDITY_HIGH_PERCENT - HUMIDITY_HYSTERESIS_PERCENT)) {
            return ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT;
        }
        if (environment_in_range(
                temperature_c, humidity_percent,
                COMFORT_TEMPERATURE_LOW_C + TEMPERATURE_HYSTERESIS_C,
                COMFORT_TEMPERATURE_HIGH_C - TEMPERATURE_HYSTERESIS_C,
                COMFORT_HUMIDITY_LOW_PERCENT +
                    HUMIDITY_HYSTERESIS_PERCENT,
                COMFORT_HUMIDITY_HIGH_PERCENT -
                    HUMIDITY_HYSTERESIS_PERCENT)) {
            return ENVIRONMENT_COMFORT_COMFORTABLE;
        }
        return ENVIRONMENT_COMFORT_FAIR;
    }

    return environment_comfort_classify(temperature_c, humidity_percent);
}

static uint8_t severity(environment_comfort_level_t level)
{
    switch (level) {
    case ENVIRONMENT_COMFORT_COMFORTABLE:
        return 0U;
    case ENVIRONMENT_COMFORT_FAIR:
        return 1U;
    case ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT:
        return 2U;
    case ENVIRONMENT_COMFORT_UNKNOWN:
    default:
        return 3U;
    }
}

static uint32_t saturated_add(uint32_t left, uint32_t right)
{
    return UINT32_MAX - left < right ? UINT32_MAX : left + right;
}

environment_comfort_level_t environment_comfort_update(
    environment_comfort_tracker_t *tracker,
    float temperature_c,
    float humidity_percent,
    uint32_t timestamp_ms)
{
    if (tracker == NULL) {
        return ENVIRONMENT_COMFORT_UNKNOWN;
    }
    if (!measurement_valid(temperature_c, humidity_percent)) {
        environment_comfort_mark_invalid(tracker);
        return ENVIRONMENT_COMFORT_UNKNOWN;
    }

    uint32_t sample_elapsed_ms = 0U;
    if (tracker->has_sample) {
        sample_elapsed_ms = timestamp_ms - tracker->last_sample_ms;
        if (sample_elapsed_ms > COMFORT_MAX_SAMPLE_GAP_MS) {
            tracker->sample_count = 0U;
            tracker->has_sample = false;
            tracker->candidate_level = ENVIRONMENT_COMFORT_UNKNOWN;
            tracker->candidate_elapsed_ms = 0U;
            sample_elapsed_ms = 0U;
        }
    }
    if (!tracker->has_sample) {
        tracker->has_sample = true;
        tracker->observation_started_ms = timestamp_ms;
    }
    tracker->last_sample_ms = timestamp_ms;
    add_sample(tracker, temperature_c, humidity_percent, timestamp_ms);

    if (timestamp_ms - tracker->observation_started_ms <
        COMFORT_INITIAL_OBSERVATION_MS) {
        return tracker->level;
    }

    float filtered_temperature_c = 0.0f;
    float filtered_humidity_percent = 0.0f;
    filtered_measurement(tracker, &filtered_temperature_c,
                         &filtered_humidity_percent);
    const environment_comfort_level_t next = classify_with_hysteresis(
        tracker->level, filtered_temperature_c,
        filtered_humidity_percent);

    if (tracker->level == ENVIRONMENT_COMFORT_UNKNOWN) {
        tracker->level = next;
        tracker->candidate_level = ENVIRONMENT_COMFORT_UNKNOWN;
        return tracker->level;
    }
    if (next == tracker->level) {
        tracker->candidate_level = ENVIRONMENT_COMFORT_UNKNOWN;
        tracker->candidate_elapsed_ms = 0U;
        return tracker->level;
    }
    if (tracker->candidate_level != next) {
        tracker->candidate_level = next;
        tracker->candidate_elapsed_ms = 0U;
        return tracker->level;
    }

    const uint32_t required_ms =
        severity(next) > severity(tracker->level)
            ? COMFORT_WORSEN_DELAY_MS
            : COMFORT_RECOVER_DELAY_MS;
    const uint32_t candidate_step_ms =
        sample_elapsed_ms > COMFORT_MAX_CANDIDATE_STEP_MS
            ? COMFORT_MAX_CANDIDATE_STEP_MS
            : sample_elapsed_ms;
    /* A long maintenance pause cannot satisfy a transition by itself. */
    tracker->candidate_elapsed_ms = saturated_add(
        tracker->candidate_elapsed_ms, candidate_step_ms);
    if (tracker->candidate_elapsed_ms >= required_ms) {
        tracker->level = next;
        tracker->candidate_level = ENVIRONMENT_COMFORT_UNKNOWN;
        tracker->candidate_elapsed_ms = 0U;
    }
    return tracker->level;
}
