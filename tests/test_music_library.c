#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "music_library.h"
#include "sd_media.h"
#include "sd_image.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

static void (*scan_entry)(void *);
static bool mount_fails, unmount_fails, allocation_fails, digest_corrupt, digest_fails, cancelled;
static unsigned leases, random_value;
static uint64_t available_bytes = 100000000;
static uint8_t wav[48];
struct sd_media_read { int unused; };
static struct sd_media_read lease;
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *handle)
{ (void)name; (void)stack; (void)arg; (void)priority; (void)handle; scan_entry = entry; return pdPASS; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &lease; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
void vTaskDelete(TaskHandle_t task) { (void)task; }
void sd_image_store_get_status(sd_image_status_t *status) { status->state = SD_IMAGE_STATE_READY; }
void *heap_caps_calloc(size_t count, size_t bytes, unsigned caps)
{ (void)caps; return allocation_fails ? NULL : calloc(count, bytes); }
void heap_caps_free(void *pointer) { free(pointer); }
esp_err_t sd_media_begin_read(sd_media_read_t **session, uint32_t wait)
{
    (void)wait; *session = NULL;
    if (leases || mount_fails) return ESP_ERR_INVALID_STATE;
    ++leases; *session = &lease; return ESP_OK;
}
esp_err_t sd_media_end_read(sd_media_read_t *session)
{ assert(session == &lease && leases == 1); --leases; return unmount_fails ? ESP_FAIL : ESP_OK; }
esp_err_t sd_media_begin_write(sd_media_write_t **session, uint32_t wait) { return sd_media_begin_read(session, wait); }
esp_err_t sd_media_end_write(sd_media_write_t *session) { return sd_media_end_read(session); }
esp_err_t esp_vfs_fat_info(const char *path, uint64_t *total, uint64_t *available)
{ assert(strcmp(path, SD_MEDIA_MOUNT_PATH) == 0); *total = 16000000000ULL; *available = available_bytes; return ESP_OK; }
uint32_t esp_random(void) { return ++random_value; }
void mbedtls_sha256_init(mbedtls_sha256_context *context) { context->value = 0; }
void mbedtls_sha256_free(mbedtls_sha256_context *context) { context->value = 0; }
int mbedtls_sha256_starts(mbedtls_sha256_context *context, int mode) { assert(mode == 0); context->value = 0; return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *context, const unsigned char *bytes, size_t count)
{ for (size_t i = 0; i < count; ++i) context->value = context->value * 31U + bytes[i]; return digest_fails ? -1 : 0; }
int mbedtls_sha256_finish(mbedtls_sha256_context *context, unsigned char *digest)
{ for (unsigned i = 0; i < 32; ++i) digest[i] = (unsigned char)(context->value >> ((i % 4) * 8)); if (digest_corrupt) digest[0] ^= 1; return 0; }
static bool checkpoint(void *arg)
{ (void)arg; if (digest_corrupt) digest_corrupt = false; return !cancelled; }
static void put32(uint8_t *p, uint32_t value)
{ p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24); }
static unsigned files(void)
{
    DIR *dir = opendir(SD_MEDIA_MOUNT_PATH MUSIC_CARD_DIRECTORY);
    if (dir == NULL) { assert(errno == ENOENT); return 0; }
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        assert(strncmp(entry->d_name, ".incoming-", 10) != 0);
        ++count;
    }
    closedir(dir); return count;
}
static music_library_status_t status(void)
{ music_library_status_t result; music_library_get_status(&result); return result; }
static esp_err_t import(const char *name)
{
    music_import_t *item = NULL;
    esp_err_t error = music_import_begin(name, sizeof(wav), &item);
    if (error == ESP_OK) {
        error = music_import_write(item, wav, 17);
        if (error == ESP_OK) error = music_import_write(item, wav + 17, sizeof(wav) - 17);
        if (error == ESP_OK) error = music_import_commit(item, checkpoint, NULL);
        else (void)music_import_abort(item);
    }
    assert(leases == 0);
    return error;
}
int main(int argc, char **argv)
{
    (void)argv;
    assert(mkdir(SD_MEDIA_MOUNT_PATH, 0700) == 0 || errno == EEXIST);
    mount_fails = argc > 1;
    assert(music_library_init() == ESP_OK && scan_entry != NULL);
    scan_entry(NULL);
    assert(status().scanned && leases == 0);
    if (mount_fails) {
        assert(!status().card_available && status().count == 0);
        assert(import("none.wav") == ESP_ERR_INVALID_STATE);
        puts("Music storage: no-card startup and import refusal passed.");
        return 0;
    }
    assert(status().card_available && status().count == 0);
    memcpy(wav, "RIFF", 4); put32(wav + 4, 40); memcpy(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16); wav[20] = 1; wav[22] = 1; put32(wav + 24, 16000);
    put32(wav + 28, 32000); wav[32] = 2; wav[34] = 16; memcpy(wav + 36, "data", 4); put32(wav + 40, 4);
    allocation_fails = true;
    assert(import("nomem.wav") == ESP_ERR_NO_MEM && files() == 0);
    allocation_fails = false;
    available_bytes = 10;
    assert(import("full.wav") == ESP_ERR_NO_MEM && files() == 0);
    available_bytes = 100000000;
    assert(import("B-音乐.wav") == ESP_OK);
    assert(status().count == 1 && files() == 1);
    const uint32_t revision = status().revision;
    assert(import("B-音乐.wav") == ESP_ERR_NOT_ALLOWED && status().revision == revision);
    assert(import("A + test.wav") == ESP_OK);
    size_t index;
    assert(music_library_find("B-音乐.wav", &index) && index == 1);
    assert(status().count == 2 && files() == 2 && status().revision > revision);
    music_import_t *item = NULL, *second = NULL;
    assert(music_import_begin("partial.wav", sizeof(wav), &item) == ESP_OK);
    assert(music_import_begin("concurrent.wav", sizeof(wav), &second) == ESP_ERR_INVALID_STATE && second == NULL);
    assert(music_import_write(item, wav, 20) == ESP_OK);
    assert(music_import_commit(item, checkpoint, NULL) == ESP_ERR_INVALID_SIZE);
    assert(leases == 0 && files() == 2);
    assert(music_import_begin("overflow.wav", sizeof(wav), &item) == ESP_OK);
    assert(music_import_write(item, wav, sizeof(wav) + 1) == ESP_ERR_INVALID_SIZE);
    assert(music_import_abort(item) == ESP_OK && leases == 0 && files() == 2);
    cancelled = true;
    assert(import("cancel.wav") != ESP_OK && files() == 2);
    cancelled = false;
    digest_corrupt = true;
    assert(import("corrupt.wav") == ESP_ERR_INVALID_CRC && files() == 2);
    digest_fails = true;
    assert(import("hash-fails.wav") != ESP_OK && files() == 2);
    digest_fails = false;
    wav[34] = 24;
    assert(import("24bit.wav") == ESP_ERR_INVALID_RESPONSE && files() == 2);
    wav[34] = 16;
    assert(music_library_delete("../outside.wav") == ESP_ERR_INVALID_ARG);
    assert(music_library_delete("missing.wav") == ESP_ERR_NOT_FOUND);
    mount_fails = true;
    assert(music_library_delete("A + test.wav") != ESP_OK && status().count == 2);
    mount_fails = false;
    unmount_fails = true;
    assert(music_library_delete("A + test.wav") == ESP_OK && files() == 1 && status().count == 1);
    assert(import("C.wav") == ESP_OK && status().count == 2 && files() == 2);
    unmount_fails = false;
    assert(music_library_delete("C.wav") == ESP_OK);
    assert(music_library_delete("B-音乐.wav") == ESP_OK && status().count == 0 && files() == 0);
    for (unsigned i = 0; i < MUSIC_MAX_TRACKS; ++i) {
        char name[32]; snprintf(name, sizeof(name), "%02u.wav", i); assert(import(name) == ESP_OK);
    }
    assert(import("too-many.wav") == ESP_ERR_INVALID_SIZE && status().count == MUSIC_MAX_TRACKS);
    assert(music_library_delete("00.wav") == ESP_OK && import("replacement.wav") == ESP_OK);
    assert(files() == MUSIC_MAX_TRACKS && leases == 0);
    puts("Music storage: real transactions, live catalog, cancellation, no overwrite, capacity and cleanup passed.");
    return 0;
}
