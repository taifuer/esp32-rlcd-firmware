#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "environment_history.h"

static environment_history_t history;
static environment_history_snapshot_t snapshot;
static environment_history_snapshot_t saved;
static const int64_t epoch = INT64_C(1791936000);

static void sample(int64_t ms, bool valid, float temperature, float humidity)
{
    assert(environment_history_model_record(&history, ms, epoch + ms / 1000,
        valid, temperature, humidity));
}

int main(void)
{
    environment_history_model_init(&history, 1000);
    environment_history_model_snapshot(&history, 1000, &snapshot);
    assert(snapshot.count == 1 && !snapshot.has_samples);
    sample(1000, true, 20, 40);
    sample(6000, true, 22, 60);
    sample(11000, false, 99, 99);
    environment_history_model_snapshot(&history, 11000, &snapshot);
    assert(snapshot.has_samples && snapshot.count == 1);
    assert(snapshot.points[0].sample_count == 2 && snapshot.points[0].failure_count == 1);
    assert(snapshot.points[0].temperature_mean_centi_c == 2100);
    assert(snapshot.points[0].humidity_mean_centi_percent == 5000);
    assert(snapshot.temperature_min_centi_c == 2000 && snapshot.temperature_max_centi_c == 2200);
    assert(snapshot.humidity_min_centi_percent == 4000 && snapshot.humidity_max_centi_percent == 6000);
    assert(!environment_history_model_record(&history, 10000, epoch, true, 50, 50));
    sample(12000, true, NAN, 50);
    sample(13000, true, 30, INFINITY);
    sample(14000, true, -41, 50);
    sample(15000, true, 20, 101);
    sample(600000, true, -10.25f, 99.25f);
    environment_history_model_snapshot(&history, 600000, &snapshot);
    assert(snapshot.count == 3 && snapshot.points[1].sample_count == 0 && snapshot.points[1].failure_count == 0);
    assert(snapshot.points[0].failure_count == 5 && snapshot.points[2].temperature_mean_centi_c == -1025);
    assert(snapshot.temperature_min_centi_c == -1025 && snapshot.temperature_max_centi_c == 2200);
    char csv[256];
    assert(environment_history_csv_row(&snapshot, 1, csv, sizeof(csv)));
    assert(strcmp(csv, ",300,0,0,,,,,,,0\r\n") == 0);
    assert(!environment_history_csv_row(&snapshot, 2, csv, 2));
    assert(strstr(environment_history_csv_header(), "utc_epoch_seconds") != NULL);

    /* Time correction never changes monotonic bucket positions. */
    const uint32_t previous_revision = snapshot.revision;
    assert(environment_history_model_record(&history, 605000, epoch + 3600, true, 10, 10));
    environment_history_model_snapshot(&history, 605000, &snapshot);
    assert(snapshot.count == 3 && snapshot.revision > previous_revision);
    assert(snapshot.points[2].epoch_seconds == 0);
    assert(environment_history_model_record(&history, 610000, epoch + 3605, true, 10, 10));
    environment_history_model_snapshot(&history, 610000, &snapshot);
    assert(snapshot.points[2].epoch_seconds == 0);

    /* Sparse saving-mode samples are real aggregates; no cached repetition. */
    environment_history_model_init(&history, 0);
    for (int64_t ms = 0; ms < 2 * ENVIRONMENT_HISTORY_WINDOW_MS; ms += 60000)
        sample(ms, ms % 600000 != 0, 24, 55);
    environment_history_model_snapshot(&history, 2 * ENVIRONMENT_HISTORY_WINDOW_MS - 1, &snapshot);
    assert(snapshot.count == 288);
    assert(snapshot.points[0].start_monotonic_ms == ENVIRONMENT_HISTORY_WINDOW_MS);
    assert(snapshot.points[0].sample_count == 4 && snapshot.points[0].failure_count == 1);
    assert(snapshot.points[1].sample_count == 5 && snapshot.points[1].failure_count == 0);
    assert(snapshot.temperature_min_centi_c == 2400 && snapshot.temperature_max_centi_c == 2400);
    environment_history_model_snapshot(&history, 4 * ENVIRONMENT_HISTORY_WINDOW_MS, &snapshot);
    assert(snapshot.count == 288 && !snapshot.has_samples);

    /* No RTC is useful in RAM/CSV, but not across a reboot. */
    environment_history_model_init(&history, 0);
    assert(environment_history_model_record(&history, 0, 0, true, 20, 50));
    assert(environment_history_model_record(&history, 300000, 0, false, 20, 50));
    environment_history_model_snapshot(&history, 300000, &saved);
    assert(saved.captured_epoch_seconds == 0);
    assert(environment_history_csv_row(&saved, 0, csv, sizeof(csv)));
    assert(strncmp(csv, ",300,1,0,20.00", 14) == 0);
    environment_history_model_init(&history, 1000);
    assert(environment_history_model_restore(&history, &saved, 1000, epoch) == 0);

    /* A previous boot cannot replace the first live bucket. */
    environment_history_model_init(&history, 0);
    for (int64_t ms = 0; ms < 3600000; ms += 300000) sample(ms, true, 23, 40);
    environment_history_model_snapshot(&history, 3600000, &saved);
    environment_history_model_init(&history, 1000);
    assert(environment_history_model_record(&history, 1000, epoch + 3901, true, 27, 60));
    assert(environment_history_model_restore(&history, &saved, 1000, epoch + 3901) == 12);
    environment_history_model_snapshot(&history, 1000, &snapshot);
    assert(snapshot.restored_history && snapshot.has_samples);
    assert(snapshot.points[snapshot.count - 1].temperature_mean_centi_c == 2700);
    assert(snapshot.points[snapshot.count - 1].restored == false);
    assert(snapshot.temperature_min_centi_c == 2300 && snapshot.temperature_max_centi_c == 2700);
    assert(environment_history_model_restore(&history, &saved, 1000, epoch + 3901) == 0);
    environment_history_model_init(&history, 1000);
    assert(environment_history_model_restore(&history, &saved, 1000, epoch - 1) == 0);
    assert(environment_history_model_restore(&history, &saved, 1000, epoch + 2 * 86400) == 0);
    assert(environment_history_model_restore(&history, &saved, 1000, 0) == 0);

    puts("Environment history: aggregates, gaps, clock jumps, 24h expiry, reboot restore and CSV passed.");
    return 0;
}
