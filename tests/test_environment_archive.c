#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "environment_archive.h"

static char directory[160];
static unsigned checkpoints, cancel_at;
static size_t streamed;
static bool checkpoint(void *context)
{
    (void)context;
    ++checkpoints;
    return cancel_at == 0 || checkpoints < cancel_at;
}
static bool collect(const char *bytes, size_t length, void *context)
{
    (void)context;
    assert(bytes && length > 0 && length <= 1024);
    streamed += length;
    return true;
}
static void path(char output[192], const char *name)
{
    const int count = snprintf(output, 192, "%s/%s", directory, name);
    assert(count > 0 && count < 192);
}
static uint64_t file_size(const char *name)
{
    char target[192]; path(target, name);
    struct stat st;
    assert(stat(target, &st) == 0);
    return (uint64_t)st.st_size;
}
static void append_bytes(const char *name, const char *data, size_t length)
{
    char target[192]; path(target, name);
    FILE *file = fopen(target, "ab");
    assert(file && fwrite(data, 1, length, file) == length && fclose(file) == 0);
}
static void resize_file(const char *name, uint64_t size)
{
    char target[192]; path(target, name);
    assert(truncate(target, (off_t)size) == 0);
}
static void flip_byte(const char *name, size_t offset)
{
    char target[192]; path(target, name);
    FILE *file = fopen(target, "r+b");
    assert(file && fseek(file, (long)offset, SEEK_SET) == 0);
    const int value = fgetc(file);
    assert(value != EOF && fseek(file, (long)offset, SEEK_SET) == 0);
    assert(fputc(value ^ 1, file) != EOF && fclose(file) == 0);
}
static int64_t epoch(unsigned year, unsigned month)
{
    struct tm utc = {.tm_year = (int)year - 1900, .tm_mon = (int)month - 1, .tm_mday = 1};
    return (int64_t)mktime(&utc);
}
static environment_archive_record_t record(uint64_t id, int64_t ms, int64_t seconds)
{
    const environment_archive_record_t result = {
        .boot_id = id,
        .point = {.start_monotonic_ms = ms, .epoch_seconds = seconds,
            .sample_count = 10, .failure_count = 1,
            .temperature_mean_centi_c = 2400, .temperature_min_centi_c = 2300,
            .temperature_max_centi_c = 2500, .humidity_mean_centi_percent = 5000,
            .humidity_min_centi_percent = 4500, .humidity_max_centi_percent = 5500},
    };
    return result;
}

int main(int argc, char **argv)
{
    assert(argc == 2 && strlen(argv[1]) < sizeof(directory));
    strcpy(directory, argv[1]);
    assert(setenv("TZ", "UTC0", 1) == 0); tzset();
    assert(!environment_archive_valid_month(NULL));
    const char *invalid[] = {"", "2026-00", "2026-13", "../2026", "2026-1", "2026-10.csv",
        "1999-12", "2101-01", "2026-10/..", "2026-1x", "2026\n10"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!environment_archive_valid_month(invalid[i]));
    assert(environment_archive_valid_month("2026-10"));
    char month[8];
    assert(!environment_archive_month(0, month));
    assert(environment_archive_month(epoch(2026, 10), month) && !strcmp(month, "2026-10"));
    environment_archive_record_t item = record(1, 0, epoch(2026, 10)), decoded;
    char line[ENVIRONMENT_ARCHIVE_ROW_MAX];
    size_t length = environment_archive_encode(&item, line, sizeof(line));
    assert(length > 60 && length < sizeof(line));
    assert(environment_archive_decode(line, length, &decoded));
    assert(decoded.boot_id == 1 && decoded.point.temperature_mean_centi_c == 2400);
    assert(strstr(line, "24.00,23.00,25.00,50.00,45.00,55.00,") != NULL);
    for (size_t i = 0; i < length; ++i) {
        line[i] ^= 1;
        assert(!environment_archive_decode(line, length, &decoded));
        line[i] ^= 1;
    }
    assert(!environment_archive_encode(&item, line, 10));
    item.point.temperature_mean_centi_c = -50;
    item.point.temperature_min_centi_c = -4000;
    item.point.temperature_max_centi_c = 12500;
    length = environment_archive_encode(&item, line, sizeof(line));
    assert(strstr(line, "-0.50,-40.00,125.00,") != NULL);
    assert(environment_archive_decode(line, length, &decoded));
    assert(decoded.point.temperature_mean_centi_c == -50 && decoded.point.temperature_min_centi_c == -4000);
    item = record(1, 0, epoch(2026, 10));
    item.point.sample_count = 0;
    item.point.temperature_mean_centi_c = item.point.temperature_min_centi_c = item.point.temperature_max_centi_c = 0;
    item.point.humidity_mean_centi_percent = item.point.humidity_min_centi_percent = item.point.humidity_max_centi_percent = 0;
    length = environment_archive_encode(&item, line, sizeof(line));
    assert(strstr(line, ",0,1,,,,,,,") != NULL);
    assert(environment_archive_decode(line, length, &decoded) && decoded.point.sample_count == 0 && decoded.point.failure_count == 1);
    item = record(1, 0, epoch(2026, 10));
    item.point.restored = true;
    assert(!environment_archive_encode(&item, line, sizeof(line)));
    item.point.restored = false;
    item.point.epoch_seconds = 0;
    assert(!environment_archive_encode(&item, line, sizeof(line)));
    item.point.epoch_seconds = epoch(2026, 10);
    uint64_t downloadable = 0;
    assert(environment_archive_inspect(directory, "../2026", &downloadable, checkpoint, NULL) == ENVIRONMENT_STORAGE_INVALID);
    assert(environment_archive_append(directory, &item, ENVIRONMENT_ARCHIVE_FREE_RESERVE,
        checkpoint, NULL) == ENVIRONMENT_STORAGE_NO_SPACE);
    struct stat st;
    assert(stat(directory, &st) != 0 && errno == ENOENT);
    const uint64_t free_bytes = UINT64_C(1000000000);
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    const uint64_t first_size = file_size("2026-10.csv");
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(file_size("2026-10.csv") == first_size);
    ++item.point.temperature_mean_centi_c;
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_INVALID);
    --item.point.temperature_mean_centi_c;
    item.point.start_monotonic_ms += ENVIRONMENT_HISTORY_BUCKET_MS;
    item.point.epoch_seconds += 300;
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    const uint64_t two_size = file_size("2026-10.csv");

    /* An interrupted append is excluded from export; only its unterminated
     * own tail is removed on the next explicit archive write. */
    item.point.start_monotonic_ms += ENVIRONMENT_HISTORY_BUCKET_MS;
    item.point.epoch_seconds += 300;
    length = environment_archive_encode(&item, line, sizeof(line));
    append_bytes("2026-10.csv", line, length / 2);
    assert(environment_archive_inspect(directory, "2026-10", &downloadable, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(downloadable == two_size);
    streamed = 0;
    assert(environment_archive_stream(directory, "2026-10", collect, NULL, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(streamed == two_size && file_size("2026-10.csv") > two_size);
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    const uint64_t three_size = file_size("2026-10.csv");
    assert(three_size == two_size + length);

    /* A damaged complete row is not treated as a disposable partial tail. */
    line[20] ^= 1;
    append_bytes("2026-10.csv", line, length);
    item.point.start_monotonic_ms += ENVIRONMENT_HISTORY_BUCKET_MS;
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_INVALID);
    assert(file_size("2026-10.csv") == three_size + length);
    resize_file("2026-10.csv", three_size);
    flip_byte("2026-10.csv", strlen(environment_archive_header()) + 20);
    streamed = 0;
    assert(environment_archive_stream(directory, "2026-10", collect, NULL, checkpoint, NULL) == ENVIRONMENT_STORAGE_INVALID);
    flip_byte("2026-10.csv", strlen(environment_archive_header()) + 20);

    /* Clock rollback and reboot cannot overwrite an older aggregate. IDs use
     * boot nonce + monotonic bucket, independently of repeated UTC labels. */
    item.point.epoch_seconds = epoch(2026, 10);
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    item.boot_id = 2; item.point.start_monotonic_ms = 0;
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);

    /* A card-full short append is recoverable without deleting valid rows. */
    const uint64_t saved_size = file_size("2026-10.csv");
    struct rlimit previous;
    assert(getrlimit(RLIMIT_FSIZE, &previous) == 0);
    struct rlimit limited = previous;
    limited.rlim_cur = (rlim_t)(saved_size + 10);
    assert(signal(SIGXFSZ, SIG_IGN) != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limited) == 0);
    item.point.start_monotonic_ms = ENVIRONMENT_HISTORY_BUCKET_MS;
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_IO_ERROR);
    assert(setrlimit(RLIMIT_FSIZE, &previous) == 0);
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(environment_archive_inspect(directory, "2026-10", &downloadable, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);

    /* Cancel at every append checkpoint; retry is idempotent. */
    for (unsigned cancel = 1; cancel <= 8; ++cancel) {
        item.point.start_monotonic_ms += ENVIRONMENT_HISTORY_BUCKET_MS;
        checkpoints = 0; cancel_at = cancel;
        environment_storage_result_t result = environment_archive_append(directory, &item, free_bytes, checkpoint, NULL);
        assert(result == ENVIRONMENT_STORAGE_CANCELLED || result == ENVIRONMENT_STORAGE_OK);
        cancel_at = 0;
        assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
        const uint64_t once = file_size("2026-10.csv");
        assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
        assert(file_size("2026-10.csv") == once);
    }

    /* Foreign headers and unreasonable individual files are refused in place. */
    append_bytes("2026-11.csv", "private data", 12);
    item.point.epoch_seconds = epoch(2026, 11);
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_FOREIGN_FILE);
    assert(file_size("2026-11.csv") == 12);
    item.point.epoch_seconds = epoch(2026, 12);
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    resize_file("2026-12.csv", ENVIRONMENT_ARCHIVE_MONTH_MAX + 1U);
    assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_LIMIT);
    assert(file_size("2026-12.csv") == ENVIRONMENT_ARCHIVE_MONTH_MAX + 1U);

    /* Newest-first pagination is bounded; history older than a year remains
     * available. Files are not pruned when creating a later month. */
    for (unsigned n = 0; n < 26; ++n) {
        item.point.epoch_seconds = epoch(2023 + n / 12, n % 12 + 1);
        assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    }
    environment_archive_page_t page;
    assert(environment_archive_list(directory, NULL, &page, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(page.has_more && page.count == 22 && !strcmp(page.entries[0].month, "2026-10"));
    char cursor[8]; memcpy(cursor, page.next_cursor, 8);
    assert(environment_archive_list(directory, cursor, &page, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(!page.has_more && page.count == 5 && !strcmp(page.entries[4].month, "2023-01"));
    checkpoints = 0; cancel_at = 3;
    assert(environment_archive_list(directory, NULL, &page, checkpoint, NULL) == ENVIRONMENT_STORAGE_CANCELLED);
    cancel_at = 0;
    assert(file_size("2026-11.csv") == 12);
    /* Cancellation around initial exclusive creation leaves either no file or
     * the complete identifying header, not an empty ambiguous placeholder. */
    for (unsigned cancel = 1; cancel <= 6; ++cancel) {
        item.point.epoch_seconds = epoch(2027, cancel);
        checkpoints = 0; cancel_at = cancel;
        environment_storage_result_t result = environment_archive_append(directory, &item, free_bytes, checkpoint, NULL);
        assert(result == ENVIRONMENT_STORAGE_CANCELLED || result == ENVIRONMENT_STORAGE_OK);
        cancel_at = 0;
        assert(environment_archive_append(directory, &item, free_bytes, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    }
    /* Directory pollution cannot turn one request into an unlimited scan. */
    for (unsigned n = 0; n < 4100; ++n) {
        char name[32];
        assert(snprintf(name, sizeof(name), "foreign-%04u.txt", n) > 0);
        append_bytes(name, "", 0);
    }
    assert(environment_archive_list(directory, NULL, &page, checkpoint, NULL) == ENVIRONMENT_STORAGE_LIMIT);
    assert(page.count == 0 && !page.has_more);
    puts("Environment archive: monthly append, CRC, power-loss tails, duplicate retries, UTC rollback, full card, bounded pagination and foreign-file protection passed.");
    return 0;
}
