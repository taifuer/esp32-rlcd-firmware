#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "environment_service.h"
#include "sd_media.h"
#include "sd_image.h"
#include "freertos/task.h"

static int64_t now_us;
static unsigned steps, leases, attempts, writes;
static bool card_present, music_holds_lease, allocation_fails, task_fails;
static bool check_cancellation;
static void (*worker)(void *);
static jmp_buf finished;
static environment_history_snapshot_t snapshot;
struct sd_media_read { int marker; };
static struct sd_media_read lease;

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{ (void)caps; return allocation_fails ? NULL : calloc(count, size); }
void heap_caps_free(void *allocation) { free(allocation); }
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack,
    void *argument, unsigned priority, TaskHandle_t *handle)
{
    (void)name; (void)argument; (void)priority; (void)handle;
    assert(stack == 6144);
    worker = entry;
    return task_fails ? 0 : pdPASS;
}
void sd_image_store_get_status(sd_image_status_t *status)
{
    status->state = card_present ? SD_IMAGE_STATE_READY : SD_IMAGE_STATE_CARD_UNAVAILABLE;
    status->card_capacity_bytes = card_present ? UINT64_C(16000000000) : 0;
}
int64_t esp_timer_get_time(void)
{
    if (check_cancellation && environment_history_storage_busy()) {
        assert(!environment_history_set_io_allowed(false));
        check_cancellation = false;
    }
    return now_us;
}
esp_err_t sd_media_begin_read(sd_media_read_t **session, uint32_t wait)
{
    assert(wait == 0 && leases == 0);
    ++attempts;
    *session = NULL;
    if (!card_present || music_holds_lease) return ESP_ERR_INVALID_STATE;
    ++leases; *session = &lease;
    return ESP_OK;
}
esp_err_t sd_media_end_read(sd_media_read_t *session)
{
    assert(session == &lease && leases == 1);
    --leases;
    return ESP_OK;
}
esp_err_t sd_media_begin_write(sd_media_write_t **session, uint32_t wait)
{ ++writes; return sd_media_begin_read(session, wait); }
esp_err_t sd_media_end_write(sd_media_write_t *session) { return sd_media_end_read(session); }
esp_err_t sd_media_get_free_bytes(sd_media_read_t *session, uint64_t *free_bytes)
{
    assert(session == &lease && leases == 1);
    *free_bytes = UINT64_C(1000000000);
    return ESP_OK;
}
uint32_t esp_random(void) { return UINT32_C(0x12345678); }

void vTaskDelay(TickType_t ticks)
{
    assert(ticks == 1000 && leases == 0);
    ++steps; now_us += INT64_C(1000000);
    environment_history_status_t status;
    environment_history_get_status(&status);
    assert(!status.storage_busy);
    switch (steps) {
    case 1:
        environment_history_set_storage_enabled(true);
        assert(environment_history_set_io_allowed(true));
        break;
    case 5:
        assert(attempts == 0 && !status.card_available);
        card_present = true;
        break;
    case 10:
        assert(attempts == 0 && status.card_available && status.clock_required);
        environment_history_record(now_us / 1000, INT64_C(1791936000) + steps, true, 25, 50);
        assert(environment_history_set_io_allowed(false));
        break;
    case 15:
        assert(attempts == 0);
        music_holds_lease = true;
        assert(environment_history_set_io_allowed(true));
        break;
    case 20:
        assert(attempts == 1 && !status.saved_this_boot);
        music_holds_lease = false;
        break;
    case 31:
        assert(attempts == 2); /* One busy retry, then a read-only restore. */
        break;
    case 46:
        assert(status.saved_this_boot && status.last_storage_error == ESP_OK);
        assert(writes == 1 && attempts == 3);
        environment_history_record(now_us / 1000, INT64_C(1791936000) + steps, false, 99, 99);
        assert(environment_history_snapshot(&snapshot));
        assert(snapshot.points[0].sample_count == 1 && snapshot.points[0].failure_count == 1);
        break;
    case 60:
        assert(writes == 1); /* Batch writes, never one per measurement. */
        now_us += INT64_C(901000000);
        check_cancellation = true;
        break;
    case 61:
        assert(writes == 2 && leases == 0); /* Cancelled by real foreground activity. */
        environment_history_set_storage_enabled(false);
        assert(environment_history_set_io_allowed(true));
        break;
    case 80:
        assert(writes == 2 && !status.storage_enabled);
        longjmp(finished, 1);
    default:
        break;
    }
}

int main(void)
{
    assert(mkdir(SD_MEDIA_MOUNT_PATH, 0700) == 0 || errno == EEXIST);
    assert(!environment_history_snapshot(&snapshot));
    assert(environment_history_set_io_allowed(false));
    allocation_fails = true;
    assert(environment_history_init() == ESP_ERR_NO_MEM);
    allocation_fails = false; task_fails = true;
    assert(environment_history_init() == ESP_ERR_NO_MEM);
    task_fails = false;
    assert(environment_history_init() == ESP_OK && worker != NULL);
    if (setjmp(finished) == 0) worker(NULL);
    assert(leases == 0);
    puts("Environment service: no card/RTC, resource gates, zero-wait leases, batched persistence and cancellation passed.");
    return 0;
}
