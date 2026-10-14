#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ENVIRONMENT_HISTORY_CAPACITY 288U
#define ENVIRONMENT_HISTORY_BUCKET_MS INT64_C(300000)
#define ENVIRONMENT_HISTORY_WINDOW_MS INT64_C(86400000)

/* Chronological five-minute aggregates, never substituted with cached sensor
 * values. A failed attempt marks a discontinuity even when this bucket also
 * contains successful measurements. epoch_seconds == 0 means time unknown. */
typedef struct {
    int64_t start_monotonic_ms;
    int64_t epoch_seconds;
    uint16_t sample_count;
    uint16_t failure_count;
    int16_t temperature_mean_centi_c;
    int16_t temperature_min_centi_c;
    int16_t temperature_max_centi_c;
    uint16_t humidity_mean_centi_percent;
    uint16_t humidity_min_centi_percent;
    uint16_t humidity_max_centi_percent;
    bool restored;
} environment_history_point_t;

/* About 12 KiB: callers allocate this on the heap, not on task stacks. */
typedef struct {
    uint32_t revision;
    size_t count;
    int64_t captured_monotonic_ms;
    int64_t captured_epoch_seconds;
    int64_t recording_started_monotonic_ms;
    bool restored_history;
    bool has_samples;
    int16_t temperature_min_centi_c;
    int16_t temperature_max_centi_c;
    uint16_t humidity_min_centi_percent;
    uint16_t humidity_max_centi_percent;
    environment_history_point_t points[ENVIRONMENT_HISTORY_CAPACITY];
} environment_history_snapshot_t;

typedef struct {
    environment_history_point_t point;
    int64_t temperature_sum;
    uint64_t humidity_sum;
} environment_history_bucket_t;

/* Pure, single-owner model; the runtime below serializes access. */
typedef struct {
    bool initialized;
    int64_t newest_index;
    int64_t first_index;
    int64_t last_monotonic_ms;
    int64_t last_epoch_seconds;
    int64_t recording_started_monotonic_ms;
    uint32_t revision;
    environment_history_bucket_t buckets[ENVIRONMENT_HISTORY_CAPACITY];
} environment_history_t;

void environment_history_model_init(environment_history_t *history,
                                    int64_t now_ms);
bool environment_history_model_record(environment_history_t *history,
    int64_t now_ms, int64_t epoch_seconds, bool success,
    float temperature_c, float humidity_percent);
void environment_history_model_snapshot(const environment_history_t *history,
    int64_t now_ms, environment_history_snapshot_t *snapshot);
/* Merge only known, recent timestamps into empty pre-boot slots. Existing live
 * data always wins; future/unknown/expired records cannot extend the window. */
size_t environment_history_model_restore(environment_history_t *history,
    const environment_history_snapshot_t *saved, int64_t now_ms,
    int64_t epoch_seconds);
/* CSV header and individual rows are bounded, for chunked HTTP responses.
 * Rows use UTC epoch seconds (blank if unknown), monotonic age, and SI units. */
const char *environment_history_csv_header(void);
bool environment_history_csv_row(const environment_history_snapshot_t *snapshot,
    size_t index, char *buffer, size_t capacity);
