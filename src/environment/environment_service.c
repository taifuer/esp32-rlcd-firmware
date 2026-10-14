#include "environment_service.h"
#include "environment_storage.h"
#include "environment_archive.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_image.h"
#include "sd_media.h"

enum {
    STORAGE_RETRY_MS = 15000,
    STORAGE_FLUSH_MS = 5 * 60 * 1000,
    STORAGE_TRANSACTION_MS = 2000,
};
/* CSV verification nests bounded row/tail buffers plus libc/FatFs calls.
 * Keep extra stack margin; this is not a measured hardware high-water mark. */
static const uint32_t ENVIRONMENT_WORKER_STACK_BYTES = 6144;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static environment_history_t *s_history;
static environment_history_snapshot_t *s_work;
static environment_history_status_t s_status;
static bool s_io_allowed;
static bool s_restore_done;
static uint32_t s_storage_sequence;
static uint64_t s_boot_id;
static int64_t s_archive_after_ms;
static int64_t s_archive_cursor_ms;
static uint32_t s_archive_generation;
static bool s_archive_limited;

typedef struct {
    int64_t started_ms;
    bool archive;
    uint32_t generation;
} storage_transaction_t;

static esp_err_t storage_error(environment_storage_result_t result)
{
    switch (result) {
    case ENVIRONMENT_STORAGE_OK:
    case ENVIRONMENT_STORAGE_NOT_FOUND: return ESP_OK;
    case ENVIRONMENT_STORAGE_NO_MEMORY: return ESP_ERR_NO_MEM;
    case ENVIRONMENT_STORAGE_INVALID: return ESP_ERR_INVALID_CRC;
    case ENVIRONMENT_STORAGE_FOREIGN_FILE: return ESP_ERR_NOT_ALLOWED;
    case ENVIRONMENT_STORAGE_CANCELLED: return ESP_ERR_INVALID_STATE;
    case ENVIRONMENT_STORAGE_NO_SPACE: return ESP_ERR_NO_MEM;
    case ENVIRONMENT_STORAGE_LIMIT: return ESP_ERR_INVALID_SIZE;
    default: return ESP_FAIL;
    }
}

static bool storage_checkpoint(void *context)
{
    const storage_transaction_t *transaction = context;
    taskENTER_CRITICAL(&s_lock);
    /* The main loop revokes work only for actual foreground input/activity,
     * not every idle iteration. Yield at the next checkpoint without blocking
     * that caller; the owner still closes files and releases its SD lease. */
    const bool allowed = s_io_allowed && s_status.storage_enabled &&
        (!transaction->archive || (s_status.archive_enabled &&
                                  s_archive_generation == transaction->generation));
    taskEXIT_CRITICAL(&s_lock);
    return allowed && esp_timer_get_time() / 1000 - transaction->started_ms <= STORAGE_TRANSACTION_MS;
}

static bool card_available(void)
{
    sd_image_status_t card;
    sd_image_store_get_status(&card);
    return card.card_capacity_bytes != 0 && card.state != SD_IMAGE_STATE_LOADING &&
        card.state != SD_IMAGE_STATE_CARD_UNAVAILABLE && card.state != SD_IMAGE_STATE_NOT_INITIALIZED;
}

static esp_err_t ensure_root(void)
{
    struct stat st;
    if (stat(SD_MEDIA_MOUNT_PATH "/rlcd", &st) == 0)
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_NOT_ALLOWED;
    return errno == ENOENT && mkdir(SD_MEDIA_MOUNT_PATH "/rlcd", 0755) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t ensure_environment_directory(void)
{
    esp_err_t error = ensure_root();
    if (error != ESP_OK) return error;
    struct stat st;
    const char *directory = SD_MEDIA_MOUNT_PATH ENVIRONMENT_STORAGE_DIRECTORY;
    if (stat(directory, &st) == 0)
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_NOT_ALLOWED;
    return errno == ENOENT && mkdir(directory, 0755) == 0 ? ESP_OK : ESP_FAIL;
}

static void update_storage_space(uint64_t free_bytes)
{
    taskENTER_CRITICAL(&s_lock);
    s_status.storage_full = s_status.storage_enabled &&
        free_bytes < ENVIRONMENT_ARCHIVE_FREE_RESERVE + ENVIRONMENT_ARCHIVE_WRITE_ALLOWANCE;
    s_status.archive_full = s_status.archive_enabled && (s_status.storage_full || s_archive_limited);
    taskEXIT_CRITICAL(&s_lock);
}

static esp_err_t archive_completed(sd_media_read_t *lease,
    storage_transaction_t *transaction, bool *saved)
{
    *saved = false;
    taskENTER_CRITICAL(&s_lock);
    environment_history_model_snapshot(s_history, transaction->started_ms, s_work);
    taskEXIT_CRITICAL(&s_lock);
    esp_err_t error = ESP_OK;
    unsigned appended = 0;
    for (size_t i = 0; i < s_work->count; ++i) {
        const environment_history_point_t *point = &s_work->points[i];
        if (!storage_checkpoint(transaction)) return ESP_ERR_INVALID_STATE;
        taskENTER_CRITICAL(&s_lock);
        const bool eligible = point->start_monotonic_ms >= s_archive_after_ms &&
            point->start_monotonic_ms > s_archive_cursor_ms &&
            point->start_monotonic_ms + ENVIRONMENT_HISTORY_BUCKET_MS <= transaction->started_ms;
        taskEXIT_CRITICAL(&s_lock);
        if (!eligible) continue;
        /* Never turn pre-consent/restored measurements, gaps or unknown dates
         * into permanent records. Invalid-time buckets remain visible in RAM. */
        if (!point->restored && point->epoch_seconds != 0 &&
            (point->sample_count != 0 || point->failure_count != 0)) {
            uint64_t free_bytes = 0;
            error = sd_media_get_free_bytes(lease, &free_bytes);
            if (error != ESP_OK) return error;
            update_storage_space(free_bytes);
            if (free_bytes < ENVIRONMENT_ARCHIVE_FREE_RESERVE + ENVIRONMENT_ARCHIVE_WRITE_ALLOWANCE) {
                return ESP_ERR_NO_MEM;
            }
            if (!storage_checkpoint(transaction)) return ESP_ERR_INVALID_STATE;
            error = ensure_environment_directory();
            if (error != ESP_OK) return error;
            const environment_archive_record_t record = {.boot_id = s_boot_id, .point = *point};
            const environment_storage_result_t result = environment_archive_append(
                SD_MEDIA_MOUNT_PATH ENVIRONMENT_ARCHIVE_DIRECTORY, &record, free_bytes,
                storage_checkpoint, transaction);
            taskENTER_CRITICAL(&s_lock);
            if (s_archive_generation == transaction->generation) {
                if (result == ENVIRONMENT_STORAGE_LIMIT) s_archive_limited = true;
                if (result == ENVIRONMENT_STORAGE_OK) s_archive_limited = false;
                if (result == ENVIRONMENT_STORAGE_NO_SPACE) s_status.storage_full = true;
                s_status.archive_full = s_status.storage_full || s_archive_limited;
            }
            taskEXIT_CRITICAL(&s_lock);
            if (result != ENVIRONMENT_STORAGE_OK) return storage_error(result);
            *saved = true;
            ++appended;
        }
        taskENTER_CRITICAL(&s_lock);
        if (s_archive_generation == transaction->generation)
            s_archive_cursor_ms = point->start_monotonic_ms;
        taskEXIT_CRITICAL(&s_lock);
        /* Drain backlog in short transactions; never an unlimited card job.
         * At most the live 24h RAM window is recoverable after a long busy gap. */
        if (appended == 8) break;
    }
    return ESP_OK;
}

static void history_worker(void *argument)
{
    (void)argument;
    int64_t last_attempt_ms = -STORAGE_RETRY_MS;
    int64_t last_saved_ms = -STORAGE_FLUSH_MS;
    int64_t last_archive_attempt_ms = -STORAGE_RETRY_MS;
    uint32_t saved_revision = UINT32_MAX;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const int64_t now_ms = esp_timer_get_time() / 1000;
        const bool available = card_available();
        taskENTER_CRITICAL(&s_lock);
        s_status.card_available = available;
        const int64_t epoch = s_history->last_epoch_seconds == 0 ? 0 :
            s_history->last_epoch_seconds + (now_ms - s_history->last_monotonic_ms) / 1000;
        s_status.clock_required = s_status.storage_enabled && epoch == 0;
        const bool restore = !s_restore_done;
        const bool snapshot_due = restore ||
            (s_history->revision != saved_revision && now_ms - last_saved_ms >= STORAGE_FLUSH_MS);
        const bool archive_due = s_status.archive_enabled && !restore &&
            (now_ms / ENVIRONMENT_HISTORY_BUCKET_MS - 1) * ENVIRONMENT_HISTORY_BUCKET_MS > s_archive_cursor_ms &&
            now_ms - last_archive_attempt_ms >= (s_status.archive_full ? STORAGE_FLUSH_MS : STORAGE_RETRY_MS);
        const bool archive = !snapshot_due && archive_due;
        const bool due = snapshot_due || archive_due;
        storage_transaction_t transaction = {.started_ms = now_ms, .archive = archive,
            .generation = s_archive_generation};
        const bool start = s_status.storage_enabled && available && s_io_allowed &&
            epoch != 0 && due && now_ms - last_attempt_ms >=
                (s_status.storage_full ? STORAGE_FLUSH_MS : STORAGE_RETRY_MS);
        if (start) s_status.storage_busy = true;
        taskEXIT_CRITICAL(&s_lock);
        if (!start) continue;
        last_attempt_ms = now_ms;
        if (archive) last_archive_attempt_ms = now_ms;

        /* Acquisition is zero-wait. Music, imports and any other SD owner win;
         * this worker never stops playback or asks the user to leave a page. */
        sd_media_read_t *lease = NULL;
        esp_err_t error = restore ? sd_media_begin_read(&lease, 0) : sd_media_begin_write(&lease, 0);
        bool completed = false;
        bool archived = false;
        if (error == ESP_OK && storage_checkpoint(&transaction)) {
            if (restore) {
                uint32_t sequence = 0;
                const environment_storage_result_t result = environment_storage_load(
                    SD_MEDIA_MOUNT_PATH ENVIRONMENT_STORAGE_DIRECTORY, s_work, &sequence);
                error = storage_error(result);
                if (storage_checkpoint(&transaction)) {
                    taskENTER_CRITICAL(&s_lock);
                    /* A late background load never replaces measurements made
                     * by this boot. Unknown, future and >24h records are ignored. */
                    if (result == ENVIRONMENT_STORAGE_OK) {
                        const size_t restored = environment_history_model_restore(s_history,
                            s_work, now_ms, epoch);
                        s_status.restored_history = restored != 0;
                        s_storage_sequence = sequence;
                    }
                    /* Transient I/O/allocation failures do not authorize
                     * replacing an unread older generation with a fresh ring. */
                    if (result == ENVIRONMENT_STORAGE_OK ||
                        result == ENVIRONMENT_STORAGE_NOT_FOUND ||
                        result == ENVIRONMENT_STORAGE_INVALID)
                        s_restore_done = true;
                    taskEXIT_CRITICAL(&s_lock);
                    completed = true;
                }
            } else if (archive) {
                error = archive_completed(lease, &transaction, &archived);
            } else {
                taskENTER_CRITICAL(&s_lock);
                environment_history_model_snapshot(s_history, now_ms, s_work);
                taskEXIT_CRITICAL(&s_lock);
                uint64_t free_bytes = 0;
                error = sd_media_get_free_bytes(lease, &free_bytes);
                if (error == ESP_OK) update_storage_space(free_bytes);
                if (error == ESP_OK &&
                    free_bytes < ENVIRONMENT_ARCHIVE_FREE_RESERVE + ENVIRONMENT_ARCHIVE_WRITE_ALLOWANCE)
                    error = ESP_ERR_NO_MEM;
                if (error == ESP_OK && !storage_checkpoint(&transaction)) error = ESP_ERR_INVALID_STATE;
                if (error == ESP_OK) error = ensure_root();
                if (error == ESP_OK) {
                    const environment_storage_result_t result = environment_storage_save(
                        SD_MEDIA_MOUNT_PATH ENVIRONMENT_STORAGE_DIRECTORY, s_work,
                        s_storage_sequence + 1U, 0, storage_checkpoint, &transaction);
                    error = storage_error(result);
                    completed = result == ENVIRONMENT_STORAGE_OK;
                    if (completed) {
                        ++s_storage_sequence;
                        saved_revision = s_work->revision;
                        last_saved_ms = now_ms;
                    }
                }
            }
        } else if (error == ESP_OK) {
            error = ESP_ERR_INVALID_STATE;
        }
        if (lease != NULL) {
            const esp_err_t close_error = sd_media_end_read(lease);
            if (error == ESP_OK) error = close_error;
        }
        taskENTER_CRITICAL(&s_lock);
        /* Resource contention/cancellation is normal background deferral, not
         * a persistent card failure. Real I/O/validation errors remain visible. */
        if (error != ESP_ERR_INVALID_STATE) {
            if (archive) s_status.archive_last_error = error;
            else s_status.last_storage_error = error;
        }
        if (!restore && !archive && completed && error == ESP_OK) s_status.saved_this_boot = true;
        if (archive && archived && error == ESP_OK) s_status.archive_saved_this_boot = true;
        s_status.storage_busy = false;
        taskEXIT_CRITICAL(&s_lock);
    }
}

esp_err_t environment_history_init(void)
{
    if (s_history != NULL) return ESP_OK;
    s_history = heap_caps_calloc(1, sizeof(*s_history), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_work = heap_caps_calloc(1, sizeof(*s_work), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_history == NULL || s_work == NULL) {
        heap_caps_free(s_history); heap_caps_free(s_work);
        s_history = NULL; s_work = NULL;
        return ESP_ERR_NO_MEM;
    }
    environment_history_model_init(s_history, esp_timer_get_time() / 1000);
    s_boot_id = ((uint64_t)esp_random() << 32) | esp_random();
    if (s_boot_id == 0) s_boot_id = 1;
    s_status.ready = true;
    if (xTaskCreate(history_worker, "environment", ENVIRONMENT_WORKER_STACK_BYTES, NULL, 1, NULL) != pdPASS) {
        s_status.ready = false;
        heap_caps_free(s_history); heap_caps_free(s_work);
        s_history = NULL; s_work = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void environment_history_record(int64_t now_ms, int64_t epoch_seconds,
    bool success, float temperature_c, float humidity_percent)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_history != NULL)
        (void)environment_history_model_record(s_history, now_ms, epoch_seconds,
            success, temperature_c, humidity_percent);
    taskEXIT_CRITICAL(&s_lock);
}

bool environment_history_snapshot(environment_history_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    taskENTER_CRITICAL(&s_lock);
    const bool ready = s_status.ready && s_history != NULL;
    environment_history_model_snapshot(s_history, now_ms, snapshot);
    taskEXIT_CRITICAL(&s_lock);
    return ready;
}

void environment_history_get_status(environment_history_status_t *status)
{
    if (status == NULL) return;
    taskENTER_CRITICAL(&s_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_lock);
}

void environment_history_set_storage_enabled(bool enabled)
{
    taskENTER_CRITICAL(&s_lock);
    s_status.storage_enabled = enabled;
    if (!enabled) {
        s_status.clock_required = false;
        s_status.storage_full = false;
        if (s_status.archive_enabled) ++s_archive_generation;
        s_status.archive_enabled = false;
        s_status.archive_full = false;
        s_archive_limited = false;
    }
    taskEXIT_CRITICAL(&s_lock);
}

void environment_history_set_archive_enabled(bool enabled)
{
    const int64_t now_ms = esp_timer_get_time() / 1000;
    taskENTER_CRITICAL(&s_lock);
    enabled = enabled && s_status.storage_enabled;
    if (enabled != s_status.archive_enabled) {
        ++s_archive_generation;
        /* Exclude the partially sampled current bucket; no pre-consent data
         * is silently converted into permanent archive. Re-enable starts a
         * fresh boundary without deleting previous files or restoring them. */
        s_archive_after_ms = (now_ms / ENVIRONMENT_HISTORY_BUCKET_MS + 1) * ENVIRONMENT_HISTORY_BUCKET_MS;
        s_archive_cursor_ms = s_archive_after_ms - ENVIRONMENT_HISTORY_BUCKET_MS;
        s_status.archive_enabled = enabled;
        s_status.archive_full = false;
        s_archive_limited = false;
        s_status.archive_last_error = ESP_OK;
    }
    taskEXIT_CRITICAL(&s_lock);
}

bool environment_history_set_io_allowed(bool allowed)
{
    taskENTER_CRITICAL(&s_lock);
    s_io_allowed = allowed;
    const bool idle = !s_status.storage_busy;
    taskEXIT_CRITICAL(&s_lock);
    return idle;
}

bool environment_history_storage_busy(void)
{
    taskENTER_CRITICAL(&s_lock);
    const bool busy = s_status.storage_busy;
    taskEXIT_CRITICAL(&s_lock);
    return busy;
}
