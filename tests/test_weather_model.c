#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "weather_model.h"

static weather_snapshot_t snapshot_at(int64_t fetched_at_epoch_seconds)
{
    weather_snapshot_t snapshot = {0};
    snapshot.fetched_at_epoch_seconds = fetched_at_epoch_seconds;
    return snapshot;
}

static void test_fresh_and_stale_boundaries(void)
{
    const int64_t fetched_at = INT64_C(1788256800);
    const weather_snapshot_t snapshot = snapshot_at(fetched_at);

    assert(weather_snapshot_freshness(
               &snapshot, fetched_at, true) == WEATHER_FRESHNESS_FRESH);
    assert(weather_freshness_evaluate(
               fetched_at, fetched_at, true) == WEATHER_FRESHNESS_FRESH);
    assert(weather_snapshot_freshness(
               &snapshot,
               fetched_at + WEATHER_FRESH_WINDOW_SECONDS,
               true) == WEATHER_FRESHNESS_FRESH);
    assert(weather_snapshot_freshness(
               &snapshot,
               fetched_at + WEATHER_FRESH_WINDOW_SECONDS + 1,
               true) == WEATHER_FRESHNESS_STALE);
    assert(weather_snapshot_freshness(
               &snapshot,
               fetched_at + WEATHER_STALE_WINDOW_SECONDS,
               true) == WEATHER_FRESHNESS_STALE);
    assert(weather_snapshot_freshness(
               &snapshot,
               fetched_at + WEATHER_STALE_WINDOW_SECONDS + 1,
               true) == WEATHER_FRESHNESS_EXPIRED);
}

static void test_invalid_or_rolled_back_clock_is_unknown(void)
{
    const weather_snapshot_t snapshot = snapshot_at(INT64_C(1788256800));
    const weather_snapshot_t missing_timestamp = snapshot_at(0);

    assert(weather_snapshot_freshness(
               &snapshot, INT64_C(1788256799), true) ==
           WEATHER_FRESHNESS_UNKNOWN);
    assert(weather_snapshot_freshness(
               &snapshot, INT64_C(1788256801), false) ==
           WEATHER_FRESHNESS_UNKNOWN);
    assert(weather_snapshot_freshness(
               &snapshot, 0, true) == WEATHER_FRESHNESS_UNKNOWN);
    assert(weather_snapshot_freshness(
               &missing_timestamp, INT64_C(1788256801), true) ==
           WEATHER_FRESHNESS_UNKNOWN);
    assert(weather_snapshot_freshness(
               NULL, INT64_C(1788256801), true) ==
           WEATHER_FRESHNESS_UNKNOWN);
}

static void test_daily_freshness_uses_forecast_windows(void)
{
    const int64_t fetched_at = INT64_C(1788256800);

    assert(weather_daily_freshness_evaluate(
               fetched_at,
               fetched_at + WEATHER_DAILY_FRESH_WINDOW_SECONDS,
               true) == WEATHER_FRESHNESS_FRESH);
    assert(weather_daily_freshness_evaluate(
               fetched_at,
               fetched_at + WEATHER_DAILY_FRESH_WINDOW_SECONDS + 1,
               true) == WEATHER_FRESHNESS_STALE);
    assert(weather_daily_freshness_evaluate(
               fetched_at,
               fetched_at + WEATHER_DAILY_STALE_WINDOW_SECONDS,
               true) == WEATHER_FRESHNESS_STALE);
    assert(weather_daily_freshness_evaluate(
               fetched_at,
               fetched_at + WEATHER_DAILY_STALE_WINDOW_SECONDS + 1,
               true) == WEATHER_FRESHNESS_EXPIRED);
    assert(weather_daily_freshness_evaluate(
               fetched_at, fetched_at + 1, false) ==
           WEATHER_FRESHNESS_UNKNOWN);
}

static void test_clear_and_names(void)
{
    weather_snapshot_t snapshot;
    memset(&snapshot, 0xa5, sizeof(snapshot));

    weather_snapshot_clear(&snapshot);
    const weather_snapshot_t empty = {0};
    assert(memcmp(&snapshot, &empty, sizeof(snapshot)) == 0);
    weather_snapshot_clear(NULL);

    assert(strcmp(weather_freshness_name(WEATHER_FRESHNESS_UNKNOWN),
                  "unknown") == 0);
    assert(strcmp(weather_freshness_name(WEATHER_FRESHNESS_FRESH),
                  "fresh") == 0);
    assert(strcmp(weather_freshness_name(WEATHER_FRESHNESS_STALE),
                  "stale") == 0);
    assert(strcmp(weather_freshness_name(WEATHER_FRESHNESS_EXPIRED),
                  "expired") == 0);
    assert(strcmp(weather_freshness_name((weather_freshness_t)99),
                  "invalid") == 0);
}

int main(void)
{
    test_fresh_and_stale_boundaries();
    test_invalid_or_rolled_back_clock_is_unknown();
    test_daily_freshness_uses_forecast_windows();
    test_clear_and_names();
    puts("weather model tests passed");
    return 0;
}
