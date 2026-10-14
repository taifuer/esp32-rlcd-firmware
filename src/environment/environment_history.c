#include "environment_history.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int64_t bucket_index(int64_t ms)
{
    return ms >= 0 ? ms / ENVIRONMENT_HISTORY_BUCKET_MS :
        (ms - ENVIRONMENT_HISTORY_BUCKET_MS + 1) / ENVIRONMENT_HISTORY_BUCKET_MS;
}

static size_t slot(int64_t index)
{
    const int64_t remainder = index % ENVIRONMENT_HISTORY_CAPACITY;
    return (size_t)(remainder < 0 ? remainder + ENVIRONMENT_HISTORY_CAPACITY : remainder);
}

static bool known_epoch(int64_t epoch)
{
    return epoch >= INT64_C(946684800) && epoch <= INT64_C(4102444800);
}

static void clear_bucket(environment_history_bucket_t *bucket, int64_t index)
{
    memset(bucket, 0, sizeof(*bucket));
    bucket->point.start_monotonic_ms = index * ENVIRONMENT_HISTORY_BUCKET_MS;
}

void environment_history_model_init(environment_history_t *history, int64_t now_ms)
{
    if (history == NULL) return;
    memset(history, 0, sizeof(*history));
    if (now_ms < 0) now_ms = 0;
    history->initialized = true;
    history->newest_index = bucket_index(now_ms);
    history->first_index = history->newest_index;
    history->last_monotonic_ms = now_ms;
    history->recording_started_monotonic_ms = now_ms;
    /* Distinguish never-used slots from index zero. */
    for (size_t i = 0; i < ENVIRONMENT_HISTORY_CAPACITY; ++i)
        history->buckets[i].point.start_monotonic_ms = INT64_MIN;
    clear_bucket(&history->buckets[slot(history->newest_index)], history->newest_index);
}

static int16_t rounded_mean(int64_t sum, uint16_t count)
{
    if (count == 0) return 0;
    return (int16_t)((sum + (sum >= 0 ? count / 2 : -(int64_t)(count / 2))) / count);
}

bool environment_history_model_record(environment_history_t *history,
    int64_t now_ms, int64_t epoch_seconds, bool success,
    float temperature_c, float humidity_percent)
{
    if (history == NULL || now_ms < 0 ||
        (history->initialized && now_ms < history->last_monotonic_ms)) return false;
    if (!history->initialized) environment_history_model_init(history, now_ms);
    const int64_t index = bucket_index(now_ms);
    environment_history_bucket_t *bucket = &history->buckets[slot(index)];
    if (bucket->point.start_monotonic_ms != index * ENVIRONMENT_HISTORY_BUCKET_MS)
        clear_bucket(bucket, index);
    environment_history_point_t *point = &bucket->point;
    /* Clock labels do not determine ordering or elapsed history. A clock jump
     * inside a bucket makes its civil-time label unknown, never shifts data. */
    if (point->sample_count == 0 && point->failure_count == 0) {
        if (known_epoch(epoch_seconds))
            point->epoch_seconds = epoch_seconds - (now_ms - point->start_monotonic_ms) / 1000;
    } else if (point->epoch_seconds != 0 && known_epoch(epoch_seconds)) {
        const int64_t expected = point->epoch_seconds + (now_ms - point->start_monotonic_ms) / 1000;
        if (epoch_seconds - expected > 3 || epoch_seconds - expected < -3)
            point->epoch_seconds = 0;
    }
    history->last_monotonic_ms = now_ms;
    history->last_epoch_seconds = known_epoch(epoch_seconds) ? epoch_seconds : 0;
    history->newest_index = index;
    if (history->first_index < index - (int64_t)ENVIRONMENT_HISTORY_CAPACITY + 1)
        history->first_index = index - (int64_t)ENVIRONMENT_HISTORY_CAPACITY + 1;
    success = success && isfinite(temperature_c) && isfinite(humidity_percent) &&
        temperature_c >= -40.0f && temperature_c <= 125.0f &&
        humidity_percent >= 0.0f && humidity_percent <= 100.0f;
    if (!success) {
        if (point->failure_count < UINT16_MAX) ++point->failure_count;
    } else if (point->sample_count < UINT16_MAX) {
        const int16_t temperature = (int16_t)(temperature_c * 100.0f +
            (temperature_c < 0 ? -0.5f : 0.5f));
        const uint16_t humidity = (uint16_t)(humidity_percent * 100.0f + 0.5f);
        if (point->sample_count == 0) {
            point->temperature_min_centi_c = point->temperature_max_centi_c = temperature;
            point->humidity_min_centi_percent = point->humidity_max_centi_percent = humidity;
        } else {
            if (temperature < point->temperature_min_centi_c) point->temperature_min_centi_c = temperature;
            if (temperature > point->temperature_max_centi_c) point->temperature_max_centi_c = temperature;
            if (humidity < point->humidity_min_centi_percent) point->humidity_min_centi_percent = humidity;
            if (humidity > point->humidity_max_centi_percent) point->humidity_max_centi_percent = humidity;
        }
        ++point->sample_count;
        bucket->temperature_sum += temperature;
        bucket->humidity_sum += humidity;
        point->temperature_mean_centi_c = rounded_mean(bucket->temperature_sum, point->sample_count);
        point->humidity_mean_centi_percent = (uint16_t)((bucket->humidity_sum + point->sample_count / 2) / point->sample_count);
    }
    ++history->revision;
    return true;
}

void environment_history_model_snapshot(const environment_history_t *history,
    int64_t now_ms, environment_history_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    memset(snapshot, 0, sizeof(*snapshot));
    if (history == NULL || !history->initialized) return;
    if (now_ms < history->last_monotonic_ms) now_ms = history->last_monotonic_ms;
    const int64_t end = bucket_index(now_ms);
    int64_t start = history->first_index;
    if (start < end - (int64_t)ENVIRONMENT_HISTORY_CAPACITY + 1)
        start = end - (int64_t)ENVIRONMENT_HISTORY_CAPACITY + 1;
    snapshot->revision = history->revision;
    snapshot->captured_monotonic_ms = now_ms;
    snapshot->captured_epoch_seconds = history->last_epoch_seconds == 0 ? 0 :
        history->last_epoch_seconds + (now_ms - history->last_monotonic_ms) / 1000;
    snapshot->recording_started_monotonic_ms = history->recording_started_monotonic_ms;
    for (int64_t index = start; index <= end; ++index) {
        environment_history_point_t *point = &snapshot->points[snapshot->count++];
        const environment_history_point_t *stored = &history->buckets[slot(index)].point;
        point->start_monotonic_ms = index * ENVIRONMENT_HISTORY_BUCKET_MS;
        if (stored->start_monotonic_ms == point->start_monotonic_ms) *point = *stored;
        if (point->restored) snapshot->restored_history = true;
        if (point->sample_count == 0) continue;
        if (!snapshot->has_samples) {
            snapshot->has_samples = true;
            snapshot->temperature_min_centi_c = point->temperature_min_centi_c;
            snapshot->temperature_max_centi_c = point->temperature_max_centi_c;
            snapshot->humidity_min_centi_percent = point->humidity_min_centi_percent;
            snapshot->humidity_max_centi_percent = point->humidity_max_centi_percent;
        } else {
            if (point->temperature_min_centi_c < snapshot->temperature_min_centi_c) snapshot->temperature_min_centi_c = point->temperature_min_centi_c;
            if (point->temperature_max_centi_c > snapshot->temperature_max_centi_c) snapshot->temperature_max_centi_c = point->temperature_max_centi_c;
            if (point->humidity_min_centi_percent < snapshot->humidity_min_centi_percent) snapshot->humidity_min_centi_percent = point->humidity_min_centi_percent;
            if (point->humidity_max_centi_percent > snapshot->humidity_max_centi_percent) snapshot->humidity_max_centi_percent = point->humidity_max_centi_percent;
        }
    }
}

size_t environment_history_model_restore(environment_history_t *history,
    const environment_history_snapshot_t *saved, int64_t now_ms, int64_t epoch_seconds)
{
    if (history == NULL || saved == NULL || !history->initialized || now_ms < 0 ||
        !known_epoch(epoch_seconds) || saved->count > ENVIRONMENT_HISTORY_CAPACITY) return 0;
    const int64_t boot_index = bucket_index(history->recording_started_monotonic_ms);
    const int64_t minimum_index = bucket_index(now_ms) - (int64_t)ENVIRONMENT_HISTORY_CAPACITY + 1;
    size_t restored = 0;
    for (size_t i = 0; i < saved->count; ++i) {
        const environment_history_point_t *point = &saved->points[i];
        if (!known_epoch(point->epoch_seconds) || point->epoch_seconds > epoch_seconds ||
            epoch_seconds - point->epoch_seconds >= ENVIRONMENT_HISTORY_WINDOW_MS / 1000) continue;
        const int64_t index = bucket_index(now_ms - (epoch_seconds - point->epoch_seconds) * 1000);
        if (index < minimum_index || index >= boot_index) continue;
        environment_history_bucket_t *bucket = &history->buckets[slot(index)];
        /* Do not merge potentially overlapping aggregates or live buckets. */
        if (bucket->point.start_monotonic_ms == index * ENVIRONMENT_HISTORY_BUCKET_MS &&
            (bucket->point.sample_count != 0 || bucket->point.failure_count != 0)) continue;
        clear_bucket(bucket, index);
        bucket->point = *point;
        bucket->point.start_monotonic_ms = index * ENVIRONMENT_HISTORY_BUCKET_MS;
        bucket->point.restored = true;
        bucket->temperature_sum = (int64_t)point->temperature_mean_centi_c * point->sample_count;
        bucket->humidity_sum = (uint64_t)point->humidity_mean_centi_percent * point->sample_count;
        if (index < history->first_index) history->first_index = index;
        ++restored;
    }
    if (restored != 0) ++history->revision;
    return restored;
}

const char *environment_history_csv_header(void)
{
    return "utc_epoch_seconds,age_seconds,valid_samples,failed_samples,temperature_mean_c,temperature_min_c,temperature_max_c,humidity_mean_percent,humidity_min_percent,humidity_max_percent,restored\r\n";
}

bool environment_history_csv_row(const environment_history_snapshot_t *snapshot,
    size_t index, char *buffer, size_t capacity)
{
    if (snapshot == NULL || buffer == NULL || index >= snapshot->count ||
        snapshot->count > ENVIRONMENT_HISTORY_CAPACITY || capacity == 0) return false;
    const environment_history_point_t *point = &snapshot->points[index];
    char epoch[24] = "";
    if (point->epoch_seconds != 0)
        (void)snprintf(epoch, sizeof(epoch), "%" PRId64, point->epoch_seconds);
    int length;
    const int64_t age = (snapshot->captured_monotonic_ms - point->start_monotonic_ms) / 1000;
    if (point->sample_count == 0) {
        length = snprintf(buffer, capacity, "%s,%" PRId64 ",0,%u,,,,,,,%u\r\n",
            epoch, age, (unsigned)point->failure_count, point->restored ? 1U : 0U);
    } else {
        length = snprintf(buffer, capacity, "%s,%" PRId64 ",%u,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u\r\n",
            epoch, age, (unsigned)point->sample_count, (unsigned)point->failure_count,
            (double)point->temperature_mean_centi_c / 100,
            (double)point->temperature_min_centi_c / 100,
            (double)point->temperature_max_centi_c / 100,
            (double)point->humidity_mean_centi_percent / 100,
            (double)point->humidity_min_centi_percent / 100,
            (double)point->humidity_max_centi_percent / 100,
            point->restored ? 1U : 0U);
    }
    return length >= 0 && (size_t)length < capacity;
}
