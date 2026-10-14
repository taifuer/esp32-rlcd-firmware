#pragma once

#include "environment_history.h"
#include "esp_err.h"

typedef struct {
    bool ready;
    bool storage_enabled;
    bool card_available;
    bool storage_busy;
    bool restored_history;
    bool saved_this_boot;
    bool clock_required;
    bool storage_full;
    bool archive_enabled;
    bool archive_saved_this_boot;
    bool archive_full;
    esp_err_t archive_last_error;
    esp_err_t last_storage_error;
} environment_history_status_t;

/* No card I/O on the caller. Call only in normal (not recovery) boot. */
esp_err_t environment_history_init(void);
void environment_history_record(int64_t now_ms, int64_t epoch_seconds,
    bool success, float temperature_c, float humidity_percent);
bool environment_history_snapshot(environment_history_snapshot_t *snapshot);
void environment_history_get_status(environment_history_status_t *status);
void environment_history_set_storage_enabled(bool enabled);
/* Separate explicit consent; old 24h-only preference never enables archive.
 * Only live completed buckets starting after enable are eligible. */
void environment_history_set_archive_enabled(bool enabled);
/* Gate background I/O for actual input/audio/OTA/card/portal activity, not on
 * every otherwise-idle loop iteration. false prevents new leases and cancels
 * existing work at its next checkpoint. Return false while an already-started
 * lease closes/releases resources; caller should defer acquiring resources.
 * This function itself never waits and never performs card I/O. */
bool environment_history_set_io_allowed(bool allowed);
bool environment_history_storage_busy(void);
