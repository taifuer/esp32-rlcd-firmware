#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "environment_archive.h"
#include "environment_service.h"
#include "esp_random.h"
#include "sd_media.h"
#include "sd_image.h"
#include "freertos/task.h"

static int64_t now_us;
static unsigned steps, leases, writes, records;
static unsigned full_writes;
static uint64_t free_bytes = UINT64_C(1000000000);
static bool music_holds_lease, cancel_on_free;
static void (*worker)(void *);
static jmp_buf finished;
struct sd_media_read { int marker; };
static struct sd_media_read lease;

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{ (void)caps; return calloc(count, size); }
void heap_caps_free(void *allocation) { free(allocation); }
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack,
    void *argument, unsigned priority, TaskHandle_t *handle)
{
    (void)name; (void)argument; (void)priority; (void)handle;
    assert(stack == 6144); worker = entry; return pdPASS;
}
void sd_image_store_get_status(sd_image_status_t *status)
{ status->state = SD_IMAGE_STATE_READY; status->card_capacity_bytes = UINT64_C(16000000000); }
int64_t esp_timer_get_time(void) { return now_us; }
uint32_t esp_random(void) { return UINT32_C(0xabcdef12); }
esp_err_t sd_media_begin_read(sd_media_read_t **session, uint32_t wait)
{
    assert(wait == 0 && leases == 0); *session = NULL;
    if (music_holds_lease) return ESP_ERR_INVALID_STATE;
    ++leases; *session = &lease; return ESP_OK;
}
esp_err_t sd_media_end_read(sd_media_read_t *session)
{ assert(session == &lease && leases == 1); --leases; return ESP_OK; }
esp_err_t sd_media_begin_write(sd_media_write_t **session, uint32_t wait)
{ ++writes; return sd_media_begin_read(session, wait); }
esp_err_t sd_media_end_write(sd_media_write_t *session) { return sd_media_end_read(session); }
esp_err_t sd_media_get_free_bytes(sd_media_read_t *session, uint64_t *bytes)
{
    assert(session == &lease && leases == 1);
    *bytes = free_bytes;
    if (cancel_on_free) {
        cancel_on_free = false;
        environment_history_set_archive_enabled(false);
    }
    return ESP_OK;
}

static bool count_records(const char *bytes, size_t length, void *context)
{
    (void)context;
    for (size_t i = 0; i < length; ++i) if (bytes[i] == '\n') ++records;
    return true;
}
static void assert_records(unsigned expected)
{
    records = 0;
    const environment_storage_result_t result = environment_archive_stream(
        SD_MEDIA_MOUNT_PATH ENVIRONMENT_ARCHIVE_DIRECTORY, "2026-10", count_records, NULL, NULL, NULL);
    if (expected == 0) assert(result == ENVIRONMENT_STORAGE_NOT_FOUND);
    else assert(result == ENVIRONMENT_STORAGE_OK && records == expected + 1);
}

void vTaskDelay(TickType_t ticks)
{
    assert(ticks == 1000 && leases == 0);
    ++steps; now_us += INT64_C(1000000);
    if (steps % 30 == 0 || steps == 1)
        environment_history_record(now_us / 1000, INT64_C(1791936000) + steps, true, 24, 50);
    environment_history_status_t status;
    environment_history_get_status(&status);
    assert(!status.storage_busy);
    switch (steps) {
    case 1:
        environment_history_set_archive_enabled(true);
        environment_history_get_status(&status);
        assert(!status.archive_enabled); /* Cannot imply SD consent. */
        environment_history_set_storage_enabled(true);
        assert(environment_history_set_io_allowed(true));
        break;
    case 20:
        assert(status.saved_this_boot && !status.archive_enabled);
        environment_history_set_archive_enabled(true);
        break;
    case 299:
    case 599:
        assert_records(0); /* Excludes partial/pre-consent current bucket. */
        break;
    case 601:
        assert(status.archive_enabled && status.archive_saved_this_boot);
        assert_records(1);
        environment_history_set_archive_enabled(false);
        break;
    case 620:
        environment_history_set_archive_enabled(true); /* First new bucket 900s. */
        break;
    case 1200:
        music_holds_lease = true;
        break;
    case 1210:
        assert_records(1); /* Busy music is not stopped for logging. */
        music_holds_lease = false;
        break;
    case 1235:
        assert_records(2);
        free_bytes = ENVIRONMENT_ARCHIVE_FREE_RESERVE;
        break;
    case 1501:
        assert(status.archive_full && status.archive_last_error == ESP_ERR_NO_MEM);
        assert_records(2);
        free_bytes = UINT64_C(1000000000);
        break;
    case 1600:
        assert_records(2); /* Full-card retries are not a 1-second loop. */
        break;
    case 1835:
        assert(!status.archive_full && status.archive_last_error == ESP_OK);
        assert_records(4);
        break;
    case 2090:
        cancel_on_free = true;
        break;
    case 2135:
        assert(!status.archive_enabled);
        assert_records(4); /* Consent revoked before append, no partial record. */
        environment_history_set_storage_enabled(false);
        environment_history_set_archive_enabled(true);
        break;
    case 2200:
        assert(!status.storage_enabled && !status.archive_enabled);
        assert_records(4);
        assert(writes < 30); /* Never one write per 30-second sample. */
        environment_history_set_storage_enabled(true);
        environment_history_set_archive_enabled(true);
        free_bytes = ENVIRONMENT_ARCHIVE_FREE_RESERVE;
        break;
    case 2420:
        assert(status.storage_full && status.archive_full);
        full_writes = writes;
        break;
    case 2450:
        assert(writes == full_writes); /* Snapshot-first card-full also backs off. */
        break;
    case 2500:
        free_bytes = UINT64_C(1000000000);
        break;
    case 2720:
        assert(!status.storage_full && status.last_storage_error == ESP_OK);
        break;
    case 2740:
        assert_records(5);
        assert(!status.archive_full && status.archive_last_error == ESP_OK);
        break;
    case 2800:
        assert(writes < 35);
        longjmp(finished, 1);
    default: break;
    }
}

int main(void)
{
    assert(mkdir(SD_MEDIA_MOUNT_PATH, 0700) == 0 || errno == EEXIST);
    assert(environment_history_init() == ESP_OK && worker != NULL);
    if (setjmp(finished) == 0) worker(NULL);
    assert(leases == 0);
    puts("Environment archive worker: explicit consent, completed-only buckets, opt-out/re-enable, music priority, low-space backoff and cancellation passed.");
    return 0;
}
