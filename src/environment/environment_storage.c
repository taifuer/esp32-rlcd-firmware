#include "environment_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const uint8_t magic[8] = {'R', 'L', 'C', 'D', 'E', 'N', 'V', '1'};

static void put(uint8_t *to, uint64_t value, size_t count)
{
    for (size_t i = 0; i < count; ++i) to[i] = (uint8_t)(value >> (i * 8));
}

static uint64_t get(const uint8_t *from, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) value |= (uint64_t)from[i] << (i * 8);
    return value;
}

static uint32_t checksum(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        if (i >= 36 && i < 40) continue;
        crc ^= bytes[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
    }
    return ~crc;
}

size_t environment_storage_encode(const environment_history_snapshot_t *snapshot,
    uint32_t sequence, uint8_t *bytes, size_t capacity)
{
    if (snapshot == NULL || bytes == NULL || snapshot->count > ENVIRONMENT_HISTORY_CAPACITY)
        return 0;
    const size_t size = 40U + 36U * snapshot->count;
    if (capacity < size) return 0;
    memset(bytes, 0, size);
    memcpy(bytes, magic, sizeof(magic));
    put(bytes + 8, 1, 4);
    put(bytes + 12, sequence, 4);
    put(bytes + 16, (uint64_t)snapshot->captured_monotonic_ms, 8);
    put(bytes + 24, (uint64_t)snapshot->captured_epoch_seconds, 8);
    put(bytes + 32, snapshot->count, 4);
    for (size_t i = 0; i < snapshot->count; ++i) {
        const environment_history_point_t *point = &snapshot->points[i];
        uint8_t *record = bytes + 40 + i * 36;
        put(record, (uint64_t)point->start_monotonic_ms, 8);
        put(record + 8, (uint64_t)point->epoch_seconds, 8);
        put(record + 16, point->sample_count, 2);
        put(record + 18, point->failure_count, 2);
        put(record + 20, (uint16_t)point->temperature_mean_centi_c, 2);
        put(record + 22, (uint16_t)point->temperature_min_centi_c, 2);
        put(record + 24, (uint16_t)point->temperature_max_centi_c, 2);
        put(record + 26, point->humidity_mean_centi_percent, 2);
        put(record + 28, point->humidity_min_centi_percent, 2);
        put(record + 30, point->humidity_max_centi_percent, 2);
        record[32] = point->restored ? 1U : 0U;
    }
    put(bytes + 36, checksum(bytes, size), 4);
    return size;
}

bool environment_storage_decode(const uint8_t *bytes, size_t length,
    environment_history_snapshot_t *snapshot, uint32_t *sequence)
{
    if (bytes == NULL || snapshot == NULL || sequence == NULL || length < 40 ||
        length > ENVIRONMENT_STORAGE_WIRE_MAX || memcmp(bytes, magic, sizeof(magic)) != 0 ||
        get(bytes + 8, 4) != 1 || get(bytes + 32, 4) > ENVIRONMENT_HISTORY_CAPACITY ||
        length != 40 + 36 * get(bytes + 32, 4) || checksum(bytes, length) != get(bytes + 36, 4))
        return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->captured_monotonic_ms = (int64_t)get(bytes + 16, 8);
    snapshot->captured_epoch_seconds = (int64_t)get(bytes + 24, 8);
    snapshot->count = (size_t)get(bytes + 32, 4);
    if (snapshot->captured_monotonic_ms < 0 || snapshot->captured_monotonic_ms > INT64_C(3155760000000) ||
        (snapshot->captured_epoch_seconds != 0 &&
         (snapshot->captured_epoch_seconds < INT64_C(946684800) || snapshot->captured_epoch_seconds > INT64_C(4102444800))))
        return false;
    for (size_t i = 0; i < snapshot->count; ++i) {
        environment_history_point_t *point = &snapshot->points[i];
        const uint8_t *record = bytes + 40 + i * 36;
        point->start_monotonic_ms = (int64_t)get(record, 8);
        point->epoch_seconds = (int64_t)get(record + 8, 8);
        point->sample_count = (uint16_t)get(record + 16, 2);
        point->failure_count = (uint16_t)get(record + 18, 2);
        point->temperature_mean_centi_c = (int16_t)(uint16_t)get(record + 20, 2);
        point->temperature_min_centi_c = (int16_t)(uint16_t)get(record + 22, 2);
        point->temperature_max_centi_c = (int16_t)(uint16_t)get(record + 24, 2);
        point->humidity_mean_centi_percent = (uint16_t)get(record + 26, 2);
        point->humidity_min_centi_percent = (uint16_t)get(record + 28, 2);
        point->humidity_max_centi_percent = (uint16_t)get(record + 30, 2);
        point->restored = record[32] == 1;
        if (point->start_monotonic_ms < -ENVIRONMENT_HISTORY_WINDOW_MS ||
            point->start_monotonic_ms > snapshot->captured_monotonic_ms ||
            snapshot->captured_monotonic_ms - point->start_monotonic_ms >= ENVIRONMENT_HISTORY_WINDOW_MS ||
            point->start_monotonic_ms % ENVIRONMENT_HISTORY_BUCKET_MS != 0 ||
            (i != 0 && point->start_monotonic_ms != snapshot->points[i - 1].start_monotonic_ms + ENVIRONMENT_HISTORY_BUCKET_MS) ||
            (point->epoch_seconds != 0 && (point->epoch_seconds < INT64_C(946684800) ||
                point->epoch_seconds > INT64_C(4102444800))) || record[32] > 1 || record[33] || record[34] || record[35])
            return false;
        if (point->sample_count != 0 &&
            (point->temperature_min_centi_c < -4000 || point->temperature_max_centi_c > 12500 ||
             point->temperature_min_centi_c > point->temperature_mean_centi_c ||
             point->temperature_mean_centi_c > point->temperature_max_centi_c ||
             point->humidity_min_centi_percent > point->humidity_mean_centi_percent ||
             point->humidity_mean_centi_percent > point->humidity_max_centi_percent ||
             point->humidity_max_centi_percent > 10000)) return false;
    }
    *sequence = (uint32_t)get(bytes + 12, 4);
    return true;
}

static bool make_path(char *path, size_t size, const char *directory, const char *name)
{
    const int length = snprintf(path, size, "%s/%s", directory, name);
    return length >= 0 && (size_t)length < size;
}

static environment_storage_result_t read_file(const char *path,
    environment_history_snapshot_t *snapshot, uint32_t *sequence)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return errno == ENOENT ? ENVIRONMENT_STORAGE_NOT_FOUND : ENVIRONMENT_STORAGE_IO_ERROR;
    if (!S_ISREG(st.st_mode) || st.st_size < 40 || st.st_size > (off_t)ENVIRONMENT_STORAGE_WIRE_MAX)
        return ENVIRONMENT_STORAGE_INVALID;
    uint8_t *bytes = malloc((size_t)st.st_size);
    if (bytes == NULL) return ENVIRONMENT_STORAGE_NO_MEMORY;
    FILE *file = fopen(path, "rb");
    environment_storage_result_t result = ENVIRONMENT_STORAGE_IO_ERROR;
    if (file != NULL) {
        if (fread(bytes, 1, (size_t)st.st_size, file) == (size_t)st.st_size && fgetc(file) == EOF && !ferror(file))
            result = environment_storage_decode(bytes, (size_t)st.st_size, snapshot, sequence) ?
                ENVIRONMENT_STORAGE_OK : ENVIRONMENT_STORAGE_INVALID;
        if (fclose(file) != 0) result = ENVIRONMENT_STORAGE_IO_ERROR;
    }
    free(bytes);
    return result;
}

environment_storage_result_t environment_storage_load(const char *directory,
    environment_history_snapshot_t *snapshot, uint32_t *sequence)
{
    if (directory == NULL || snapshot == NULL || sequence == NULL) return ENVIRONMENT_STORAGE_INVALID;
    environment_history_snapshot_t *candidate = malloc(sizeof(*candidate));
    if (candidate == NULL) return ENVIRONMENT_STORAGE_NO_MEMORY;
    bool found = false;
    environment_storage_result_t result = ENVIRONMENT_STORAGE_NOT_FOUND;
    for (unsigned i = 0; i < 2; ++i) {
        char path[192];
        if (!make_path(path, sizeof(path), directory, i ? "history-1.bin" : "history-0.bin")) {
            result = ENVIRONMENT_STORAGE_INVALID;
            break;
        }
        uint32_t candidate_sequence = 0;
        const environment_storage_result_t read = read_file(path, candidate, &candidate_sequence);
        if (read == ENVIRONMENT_STORAGE_OK && (!found || (int32_t)(candidate_sequence - *sequence) > 0)) {
            *snapshot = *candidate;
            *sequence = candidate_sequence;
            found = true;
        } else if (read != ENVIRONMENT_STORAGE_NOT_FOUND && read != ENVIRONMENT_STORAGE_OK) result = read;
    }
    free(candidate);
    /* An unreadable slot might be the newer valid generation. Do not turn a
     * transient read/allocation failure into permission to replace it. */
    if (result == ENVIRONMENT_STORAGE_IO_ERROR || result == ENVIRONMENT_STORAGE_NO_MEMORY)
        return result;
    return found ? ENVIRONMENT_STORAGE_OK : result;
}

/* A valid magic identifies our private file even if a power failure damaged
 * its body. Unrelated files/directories at the reserved name are never removed. */
static bool own_or_absent(const char *path, bool *exists)
{
    struct stat st;
    *exists = false;
    if (stat(path, &st) != 0) return errno == ENOENT;
    *exists = true;
    if (!S_ISREG(st.st_mode) || st.st_size < (off_t)sizeof(magic) || st.st_size > (off_t)ENVIRONMENT_STORAGE_WIRE_MAX)
        return false;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    uint8_t prefix[8];
    const bool owned = fread(prefix, 1, sizeof(prefix), file) == sizeof(prefix) && memcmp(prefix, magic, sizeof(prefix)) == 0;
    const bool closed = fclose(file) == 0;
    return owned && closed;
}

environment_storage_result_t environment_storage_save(const char *directory,
    const environment_history_snapshot_t *snapshot, uint32_t sequence,
    uint32_t nonce, environment_storage_checkpoint_t checkpoint, void *context)
{
    if (directory == NULL || snapshot == NULL) return ENVIRONMENT_STORAGE_INVALID;
    if (checkpoint != NULL && !checkpoint(context)) return ENVIRONMENT_STORAGE_CANCELLED;
    uint8_t *bytes = malloc(ENVIRONMENT_STORAGE_WIRE_MAX);
    environment_history_snapshot_t *verified = malloc(sizeof(*verified));
    if (bytes == NULL || verified == NULL) {
        free(bytes); free(verified);
        return ENVIRONMENT_STORAGE_NO_MEMORY;
    }
    const size_t size = environment_storage_encode(snapshot, sequence, bytes, ENVIRONMENT_STORAGE_WIRE_MAX);
    uint32_t verified_sequence = 0;
    environment_storage_result_t result = ENVIRONMENT_STORAGE_INVALID;
    char target[192], temporary[192];
    bool temporary_exists = false, target_exists = false;
    if (!size || !environment_storage_decode(bytes, size, verified, &verified_sequence)) goto done;
    if (!make_path(target, sizeof(target), directory, sequence % 2 ? "history-1.bin" : "history-0.bin")) goto done;
    if (!own_or_absent(target, &target_exists)) { result = ENVIRONMENT_STORAGE_FOREIGN_FILE; goto done; }
    struct stat st;
    if (stat(directory, &st) != 0) {
        if (errno != ENOENT || mkdir(directory, 0755) != 0) { result = ENVIRONMENT_STORAGE_IO_ERROR; goto done; }
    } else if (!S_ISDIR(st.st_mode)) { result = ENVIRONMENT_STORAGE_FOREIGN_FILE; goto done; }
    /* One reserved temporary path bounds power-loss leftovers as well as the
     * two final slots. A foreign file at this name is never overwritten. */
    (void)nonce;
    if (!make_path(temporary, sizeof(temporary), directory, ".history-pending.tmp")) goto done;
    bool stale_exists = false;
    if (!own_or_absent(temporary, &stale_exists)) { result = ENVIRONMENT_STORAGE_FOREIGN_FILE; goto done; }
    if (stale_exists && unlink(temporary) != 0) { result = ENVIRONMENT_STORAGE_IO_ERROR; goto done; }
    const int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { result = ENVIRONMENT_STORAGE_IO_ERROR; goto done; }
    temporary_exists = true;
    FILE *file = fdopen(fd, "wb");
    if (file == NULL) { (void)close(fd); result = ENVIRONMENT_STORAGE_IO_ERROR; goto done; }
    result = ENVIRONMENT_STORAGE_OK;
    for (size_t offset = 0; offset < size; ) {
        if (checkpoint != NULL && !checkpoint(context)) { result = ENVIRONMENT_STORAGE_CANCELLED; break; }
        const size_t count = size - offset > 1024 ? 1024 : size - offset;
        if (fwrite(bytes + offset, 1, count, file) != count) { result = ENVIRONMENT_STORAGE_IO_ERROR; break; }
        offset += count;
    }
    if (result == ENVIRONMENT_STORAGE_OK && (fflush(file) != 0 || fsync(fd) != 0)) result = ENVIRONMENT_STORAGE_IO_ERROR;
    if (fclose(file) != 0) result = ENVIRONMENT_STORAGE_IO_ERROR;
    if (result != ENVIRONMENT_STORAGE_OK) goto done;
    result = read_file(temporary, verified, &verified_sequence);
    if (result != ENVIRONMENT_STORAGE_OK) goto done;
    if (verified_sequence != sequence) { result = ENVIRONMENT_STORAGE_INVALID; goto done; }
    if (checkpoint != NULL && !checkpoint(context)) { result = ENVIRONMENT_STORAGE_CANCELLED; goto done; }
    /* Recheck the exact target immediately before replacement. The other slot
     * remains intact until after a fully synced and checked new file exists. */
    if (!own_or_absent(target, &target_exists)) { result = ENVIRONMENT_STORAGE_FOREIGN_FILE; goto done; }
    if ((target_exists && unlink(target) != 0) || rename(temporary, target) != 0) {
        result = ENVIRONMENT_STORAGE_IO_ERROR;
        goto done;
    }
    temporary_exists = false;
done:
    if (temporary_exists) (void)unlink(temporary);
    free(bytes);
    free(verified);
    return result;
}
