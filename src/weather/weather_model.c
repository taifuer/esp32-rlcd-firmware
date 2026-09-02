#include "weather_model.h"

#include <string.h>

void weather_snapshot_clear(weather_snapshot_t *snapshot)
{
    if (snapshot != NULL) {
        memset(snapshot, 0, sizeof(*snapshot));
    }
}

static weather_freshness_t freshness_evaluate(
    int64_t fetched_at_epoch_seconds,
    int64_t now_epoch_seconds,
    bool rtc_valid,
    int64_t fresh_window_seconds,
    int64_t stale_window_seconds)
{
    int64_t age_seconds;

    if (!rtc_valid || fetched_at_epoch_seconds <= 0 ||
        now_epoch_seconds <= 0 ||
        now_epoch_seconds < fetched_at_epoch_seconds) {
        return WEATHER_FRESHNESS_UNKNOWN;
    }

    age_seconds = now_epoch_seconds - fetched_at_epoch_seconds;
    if (age_seconds <= fresh_window_seconds) {
        return WEATHER_FRESHNESS_FRESH;
    }
    if (age_seconds <= stale_window_seconds) {
        return WEATHER_FRESHNESS_STALE;
    }
    return WEATHER_FRESHNESS_EXPIRED;
}

weather_freshness_t weather_freshness_evaluate(
    int64_t fetched_at_epoch_seconds,
    int64_t now_epoch_seconds,
    bool rtc_valid)
{
    return freshness_evaluate(
        fetched_at_epoch_seconds, now_epoch_seconds, rtc_valid,
        WEATHER_FRESH_WINDOW_SECONDS, WEATHER_STALE_WINDOW_SECONDS);
}

weather_freshness_t weather_snapshot_freshness(
    const weather_snapshot_t *snapshot,
    int64_t now_epoch_seconds,
    bool rtc_valid)
{
    if (snapshot == NULL) {
        return WEATHER_FRESHNESS_UNKNOWN;
    }
    return weather_freshness_evaluate(
        snapshot->fetched_at_epoch_seconds, now_epoch_seconds, rtc_valid);
}

weather_freshness_t weather_daily_freshness_evaluate(
    int64_t fetched_at_epoch_seconds,
    int64_t now_epoch_seconds,
    bool rtc_valid)
{
    return freshness_evaluate(
        fetched_at_epoch_seconds, now_epoch_seconds, rtc_valid,
        WEATHER_DAILY_FRESH_WINDOW_SECONDS,
        WEATHER_DAILY_STALE_WINDOW_SECONDS);
}

const char *weather_freshness_name(weather_freshness_t freshness)
{
    switch (freshness) {
    case WEATHER_FRESHNESS_UNKNOWN:
        return "unknown";
    case WEATHER_FRESHNESS_FRESH:
        return "fresh";
    case WEATHER_FRESHNESS_STALE:
        return "stale";
    case WEATHER_FRESHNESS_EXPIRED:
        return "expired";
    default:
        return "invalid";
    }
}
