#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

#include "environment_storage.h"

static environment_history_t history;
static environment_history_snapshot_t snapshot, decoded;
static uint8_t bytes[ENVIRONMENT_STORAGE_WIRE_MAX];
static char directory[192];
static unsigned checkpoints, cancel_at;

static bool checkpoint(void *context)
{
    (void)context;
    return cancel_at == 0 || ++checkpoints < cancel_at;
}

static void path(char *output, const char *name)
{
    const int length = snprintf(output, 256, "%s/%s", directory, name);
    assert(length > 0 && length < 256);
}

static void write_file(const char *name, const void *data, size_t length)
{
    char target[256]; path(target, name);
    FILE *file = fopen(target, "wb");
    assert(file && fwrite(data, 1, length, file) == length && fclose(file) == 0);
}

static void remove_file(const char *name)
{
    char target[256]; path(target, name);
    assert(unlink(target) == 0);
}

static unsigned file_count(void)
{
    DIR *dir = opendir(directory);
    assert(dir);
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        ++count;
    }
    closedir(dir);
    return count;
}

int main(int argc, char **argv)
{
    assert(argc == 2 && strlen(argv[1]) < sizeof(directory));
    strcpy(directory, argv[1]);
    environment_history_model_init(&history, 0);
    for (int64_t ms = 0; ms < ENVIRONMENT_HISTORY_WINDOW_MS; ms += 60000)
        assert(environment_history_model_record(&history, ms, INT64_C(1791936000) + ms / 1000,
            ms % 600000 != 0, 25.5f, 40.5f));
    environment_history_model_snapshot(&history, ENVIRONMENT_HISTORY_WINDOW_MS - 1, &snapshot);
    assert(snapshot.count == 288);
    const size_t size = environment_storage_encode(&snapshot, 31, bytes, sizeof(bytes));
    assert(size == ENVIRONMENT_STORAGE_WIRE_MAX);
    uint32_t sequence = 0;
    assert(environment_storage_decode(bytes, size, &decoded, &sequence) && sequence == 31);
    assert(decoded.count == snapshot.count);
    assert(memcmp(decoded.points, snapshot.points, sizeof(decoded.points)) == 0);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] ^= 1;
        assert(!environment_storage_decode(bytes, size, &decoded, &sequence));
        bytes[i] ^= 1;
    }
    for (size_t i = 0; i < size; i += 97)
        assert(!environment_storage_decode(bytes, i, &decoded, &sequence));
    assert(!environment_storage_encode(&snapshot, 31, bytes, 100));
    assert(environment_storage_load(directory, &decoded, &sequence) == ENVIRONMENT_STORAGE_NOT_FOUND);
    assert(environment_storage_save(directory, &snapshot, 1, 1, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(file_count() == 1);
    assert(environment_storage_load(directory, &decoded, &sequence) == ENVIRONMENT_STORAGE_OK && sequence == 1);
    assert(environment_storage_save(directory, &snapshot, 2, 2, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(file_count() == 2);
    assert(environment_storage_load(directory, &decoded, &sequence) == ENVIRONMENT_STORAGE_OK && sequence == 2);

    /* Power interruption at each bounded write checkpoint preserves at least
     * the previously published snapshot, and cleans this call's temporary. */
    for (unsigned i = 1; i <= 14; ++i) {
        checkpoints = 0; cancel_at = i;
        const environment_storage_result_t result = environment_storage_save(directory,
            &snapshot, 3, 3, checkpoint, NULL);
        assert(result == ENVIRONMENT_STORAGE_CANCELLED || result == ENVIRONMENT_STORAGE_OK);
        assert(environment_storage_load(directory, &decoded, &sequence) == ENVIRONMENT_STORAGE_OK);
        assert(sequence == 2 || sequence == 3);
        assert(file_count() == 2);
    }
    cancel_at = 0;

    /* A short write (a full card's observable outcome) is not published. */
    struct rlimit original_limit;
    assert(getrlimit(RLIMIT_FSIZE, &original_limit) == 0);
    struct rlimit short_file = original_limit;
    short_file.rlim_cur = 4096;
    assert(signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
    assert(setrlimit(RLIMIT_FSIZE, &short_file) == 0);
    assert(environment_storage_save(directory, &snapshot, 4, 4, checkpoint, NULL) == ENVIRONMENT_STORAGE_IO_ERROR);
    assert(setrlimit(RLIMIT_FSIZE, &original_limit) == 0);
    assert(environment_storage_load(directory, &decoded, &sequence) == ENVIRONMENT_STORAGE_OK && sequence == 3);
    assert(file_count() == 2);

    /* An invalid recent slot does not hide the older valid one. */
    assert(environment_storage_encode(&snapshot, 100, bytes, sizeof(bytes)) == size);
    bytes[100] ^= 1;
    write_file("history-0.bin", bytes, size);
    assert(environment_storage_load(directory, &decoded, &sequence) == ENVIRONMENT_STORAGE_OK && sequence == 3);
    assert(environment_storage_save(directory, &snapshot, 4, 4, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(environment_storage_load(directory, &decoded, &sequence) == ENVIRONMENT_STORAGE_OK && sequence == 4);

    /* Own incomplete temporary is reclaimable; unrelated exact-name files are
     * refused, not overwritten or deleted. No directory scan touches media. */
    write_file(".history-pending.tmp", "RLCDENV1partial", 15);
    assert(environment_storage_save(directory, &snapshot, 5, 5, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(file_count() == 2);
    write_file("private.txt", "user", 4);
    write_file("history-0.bin", "private", 7);
    assert(environment_storage_save(directory, &snapshot, 6, 6, checkpoint, NULL) == ENVIRONMENT_STORAGE_FOREIGN_FILE);
    assert(environment_storage_load(directory, &decoded, &sequence) == ENVIRONMENT_STORAGE_OK && sequence == 5);
    assert(file_count() == 3);
    remove_file("history-0.bin");
    write_file(".history-pending.tmp", "", 0);
    assert(environment_storage_save(directory, &snapshot, 6, 6, checkpoint, NULL) == ENVIRONMENT_STORAGE_FOREIGN_FILE);
    assert(file_count() == 3);
    remove_file(".history-pending.tmp");
    assert(environment_storage_save(directory, &snapshot, 6, 6, checkpoint, NULL) == ENVIRONMENT_STORAGE_OK);
    assert(file_count() == 3);

    /* A file in place of the directory is never converted or removed. */
    char wrong[256]; path(wrong, "private.txt");
    assert(environment_storage_save(wrong, &snapshot, 7, 7, checkpoint, NULL) != ENVIRONMENT_STORAGE_OK);
    assert(file_count() == 3);
    puts("Environment storage: bounded codec, corruption, dual-slot recovery, cancellation and foreign-file refusal passed.");
    return 0;
}
