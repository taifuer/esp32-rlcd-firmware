#include "environment_archive.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char header[] =
    "record_id_rlcd_env_v1,bucket_start_ms,utc_epoch_seconds,valid_samples,failed_samples,"
    "temperature_mean_c,temperature_min_c,temperature_max_c,"
    "humidity_mean_percent,humidity_min_percent,humidity_max_percent,crc32\r\n";

static uint32_t crc32(const char *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint8_t)bytes[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
    }
    return ~crc;
}

static bool proceed(environment_storage_checkpoint_t checkpoint, void *context)
{
    return checkpoint == NULL || checkpoint(context);
}

bool environment_archive_valid_month(const char *month)
{
    if (month == NULL || strlen(month) != 7 || month[4] != '-') return false;
    for (unsigned i = 0; i < 7; ++i)
        if (i != 4 && (month[i] < '0' || month[i] > '9')) return false;
    const unsigned year = (unsigned)(month[0] - '0') * 1000U +
        (unsigned)(month[1] - '0') * 100U + (unsigned)(month[2] - '0') * 10U +
        (unsigned)(month[3] - '0');
    const unsigned number = (unsigned)(month[5] - '0') * 10U + (unsigned)(month[6] - '0');
    return year >= 2000 && year <= 2100 && number >= 1 && number <= 12;
}

bool environment_archive_month(int64_t epoch_seconds, char month[8])
{
    if (month == NULL || epoch_seconds < INT64_C(946684800) ||
        epoch_seconds > INT64_C(4102444800)) return false;
    const time_t epoch = (time_t)epoch_seconds;
    struct tm utc;
    if ((int64_t)epoch != epoch_seconds || gmtime_r(&epoch, &utc) == NULL) return false;
    return strftime(month, 8, "%Y-%m", &utc) == 7;
}

const char *environment_archive_header(void) { return header; }

static bool valid_record(const environment_archive_record_t *record)
{
    if (record == NULL || record->boot_id == 0) return false;
    const environment_history_point_t *p = &record->point;
    if (p->restored || p->start_monotonic_ms < 0 ||
        p->start_monotonic_ms > INT64_C(3155760000000) ||
        p->start_monotonic_ms % ENVIRONMENT_HISTORY_BUCKET_MS != 0 ||
        p->epoch_seconds < INT64_C(946684800) || p->epoch_seconds > INT64_C(4102444800) ||
        (p->sample_count == 0 && p->failure_count == 0)) return false;
    if (p->sample_count == 0)
        return p->temperature_mean_centi_c == 0 && p->temperature_min_centi_c == 0 &&
            p->temperature_max_centi_c == 0 && p->humidity_mean_centi_percent == 0 &&
            p->humidity_min_centi_percent == 0 && p->humidity_max_centi_percent == 0;
    return p->temperature_min_centi_c >= -4000 && p->temperature_max_centi_c <= 12500 &&
        p->temperature_min_centi_c <= p->temperature_mean_centi_c &&
        p->temperature_mean_centi_c <= p->temperature_max_centi_c &&
        p->humidity_min_centi_percent <= p->humidity_mean_centi_percent &&
        p->humidity_mean_centi_percent <= p->humidity_max_centi_percent &&
        p->humidity_max_centi_percent <= 10000;
}

static void hundredths(char output[16], int32_t value)
{
    const uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
    (void)snprintf(output, 16, "%s%" PRIu32 ".%02" PRIu32,
        value < 0 ? "-" : "", magnitude / 100U, magnitude % 100U);
}

size_t environment_archive_encode(const environment_archive_record_t *record,
    char *line, size_t capacity)
{
    if (!valid_record(record) || line == NULL || capacity == 0) return 0;
    const environment_history_point_t *p = &record->point;
    char measures[6][16];
    hundredths(measures[0], p->temperature_mean_centi_c);
    hundredths(measures[1], p->temperature_min_centi_c);
    hundredths(measures[2], p->temperature_max_centi_c);
    hundredths(measures[3], p->humidity_mean_centi_percent);
    hundredths(measures[4], p->humidity_min_centi_percent);
    hundredths(measures[5], p->humidity_max_centi_percent);
    if (p->sample_count == 0)
        for (unsigned i = 0; i < 6; ++i) measures[i][0] = '\0';
    const int prefix = snprintf(line, capacity,
        "%016" PRIx64 ",%" PRId64 ",%" PRId64 ",%u,%u,%s,%s,%s,%s,%s,%s,",
        record->boot_id, p->start_monotonic_ms, p->epoch_seconds,
        (unsigned)p->sample_count, (unsigned)p->failure_count,
        measures[0], measures[1], measures[2], measures[3], measures[4], measures[5]);
    if (prefix <= 0 || (size_t)prefix + 11U > capacity) return 0;
    const uint32_t crc = crc32(line, (size_t)prefix);
    if (snprintf(line + prefix, capacity - (size_t)prefix, "%08" PRIx32 "\r\n", crc) != 10)
        return 0;
    return (size_t)prefix + 10U;
}

static bool decimal(char **cursor, int64_t minimum, int64_t maximum, int64_t *value)
{
    const char first = **cursor;
    if ((first < '0' || first > '9') && !(minimum < 0 && first == '-')) return false;
    errno = 0;
    char *end;
    const int64_t parsed = strtoll(*cursor, &end, 10);
    if (errno != 0 || end == *cursor || *end != ',' || parsed < minimum || parsed > maximum)
        return false;
    *value = parsed;
    *cursor = end + 1;
    return true;
}

static bool fixed_hundredths(char **cursor, int64_t minimum, int64_t maximum, int64_t *value)
{
    char *end = strchr(*cursor, ',');
    if (end == NULL) return false;
    const ptrdiff_t length = end - *cursor;
    if (length < 4 || length > 7 || end[-3] != '.') return false;
    char *number = *cursor;
    const bool negative = *number == '-';
    if (negative) ++number;
    if (number == end - 3) return false;
    int64_t parsed = 0;
    for (char *part = number; part != end; ++part) {
        if (part == end - 3) continue;
        if (*part < '0' || *part > '9') return false;
        parsed = parsed * 10 + (*part - '0');
    }
    if (negative) parsed = -parsed;
    if (parsed < minimum || parsed > maximum) return false;
    *value = parsed; *cursor = end + 1;
    return true;
}

bool environment_archive_decode(const char *line, size_t length,
    environment_archive_record_t *record)
{
    if (line == NULL || record == NULL || length < 30 || length >= ENVIRONMENT_ARCHIVE_ROW_MAX ||
        line[length - 2] != '\r' || line[length - 1] != '\n') return false;
    char copy[ENVIRONMENT_ARCHIVE_ROW_MAX];
    memcpy(copy, line, length); copy[length] = '\0';
    for (unsigned i = 0; i < 16; ++i)
        if (!((copy[i] >= '0' && copy[i] <= '9') || (copy[i] >= 'a' && copy[i] <= 'f')))
            return false;
    if (copy[16] != ',') return false;
    char *cursor;
    errno = 0;
    const uint64_t boot = strtoull(copy, &cursor, 16);
    if (errno != 0 || cursor != copy + 16) return false;
    ++cursor;
    int64_t values[10];
    const int64_t minimum[10] = {0, 946684800, 0, 0, -4000, -4000, -4000, 0, 0, 0};
    const int64_t maximum[10] = {3155760000000, 4102444800, UINT16_MAX, UINT16_MAX,
        12500, 12500, 12500, 10000, 10000, 10000};
    for (unsigned i = 0; i < 4; ++i)
        if (!decimal(&cursor, minimum[i], maximum[i], &values[i])) return false;
    for (unsigned i = 4; i < 10; ++i) {
        if (values[2] == 0) {
            if (*cursor != ',') return false;
            ++cursor; values[i] = 0;
        } else if (!fixed_hundredths(&cursor, minimum[i], maximum[i], &values[i])) return false;
    }
    if (cursor != copy + length - 10) return false;
    for (unsigned i = 0; i < 8; ++i)
        if (!((cursor[i] >= '0' && cursor[i] <= '9') || (cursor[i] >= 'a' && cursor[i] <= 'f')))
            return false;
    const uint32_t expected = (uint32_t)strtoul(cursor, NULL, 16);
    if (crc32(line, length - 10) != expected) return false;
    memset(record, 0, sizeof(*record));
    record->boot_id = boot;
    environment_history_point_t *p = &record->point;
    p->start_monotonic_ms = values[0]; p->epoch_seconds = values[1];
    p->sample_count = (uint16_t)values[2]; p->failure_count = (uint16_t)values[3];
    p->temperature_mean_centi_c = (int16_t)values[4];
    p->temperature_min_centi_c = (int16_t)values[5];
    p->temperature_max_centi_c = (int16_t)values[6];
    p->humidity_mean_centi_percent = (uint16_t)values[7];
    p->humidity_min_centi_percent = (uint16_t)values[8];
    p->humidity_max_centi_percent = (uint16_t)values[9];
    if (!valid_record(record)) return false;
    /* Canonical form also rejects embedded NUL, whitespace, leading signs,
     * overflow, leading zeros and nonstandard-but-equivalent records. */
    char canonical[ENVIRONMENT_ARCHIVE_ROW_MAX];
    const size_t encoded = environment_archive_encode(record, canonical, sizeof(canonical));
    return encoded == length && memcmp(line, canonical, length) == 0;
}

static bool file_path(char path[192], const char *directory, const char *month)
{
    if (directory == NULL || !environment_archive_valid_month(month)) return false;
    const int size = snprintf(path, 192, "%s/%s.csv", directory, month);
    return size >= 0 && size < 192;
}

typedef struct {
    uint64_t bytes;
    uint64_t complete_bytes;
    bool has_last;
    environment_archive_record_t last;
} archive_tail_t;

/* A bounded tail read verifies the last complete record. Older complete rows
 * are never modified and are validated individually during download. */
static environment_storage_result_t inspect_file(FILE *file, archive_tail_t *tail,
    environment_storage_checkpoint_t checkpoint, void *context)
{
    if (!proceed(checkpoint, context)) return ENVIRONMENT_STORAGE_CANCELLED;
    struct stat st;
    if (fstat(fileno(file), &st) != 0) return ENVIRONMENT_STORAGE_IO_ERROR;
    if (!S_ISREG(st.st_mode) || st.st_size < (off_t)(sizeof(header) - 1))
        return ENVIRONMENT_STORAGE_FOREIGN_FILE;
    if ((uint64_t)st.st_size > ENVIRONMENT_ARCHIVE_MONTH_MAX) return ENVIRONMENT_STORAGE_LIMIT;
    char check[sizeof(header)];
    if (fseek(file, 0, SEEK_SET) != 0 || fread(check, 1, sizeof(header) - 1, file) != sizeof(header) - 1)
        return ENVIRONMENT_STORAGE_IO_ERROR;
    if (memcmp(check, header, sizeof(header) - 1) != 0) return ENVIRONMENT_STORAGE_FOREIGN_FILE;
    memset(tail, 0, sizeof(*tail));
    tail->bytes = (uint64_t)st.st_size;
    tail->complete_bytes = sizeof(header) - 1;
    const uint64_t body = tail->bytes - (sizeof(header) - 1);
    if (body == 0) return ENVIRONMENT_STORAGE_OK;
    if (!proceed(checkpoint, context)) return ENVIRONMENT_STORAGE_CANCELLED;
    char buffer[ENVIRONMENT_ARCHIVE_ROW_MAX * 2U];
    const size_t length = body < sizeof(buffer) ? (size_t)body : sizeof(buffer);
    const uint64_t start = tail->bytes - length;
    if (fseek(file, (long)start, SEEK_SET) != 0 || fread(buffer, 1, length, file) != length)
        return ENVIRONMENT_STORAGE_IO_ERROR;
    size_t end = length;
    while (end != 0 && buffer[end - 1] != '\n') --end;
    if (length - end >= ENVIRONMENT_ARCHIVE_ROW_MAX) return ENVIRONMENT_STORAGE_INVALID;
    if (end == 0) {
        /* Only a short, unterminated first row; exact header remains intact. */
        return start == sizeof(header) - 1 ? ENVIRONMENT_STORAGE_OK : ENVIRONMENT_STORAGE_INVALID;
    }
    size_t begin = end - 1;
    while (begin != 0 && buffer[begin - 1] != '\n') --begin;
    if ((begin == 0 && start != sizeof(header) - 1) ||
        !environment_archive_decode(buffer + begin, end - begin, &tail->last))
        return ENVIRONMENT_STORAGE_INVALID;
    tail->complete_bytes = start + end;
    tail->has_last = true;
    return ENVIRONMENT_STORAGE_OK;
}

static FILE *open_file(const char *path, bool writable, bool exclusive)
{
    int flags = writable ? O_RDWR : O_RDONLY;
    if (exclusive) flags |= O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path, flags, 0600);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, writable ? "r+b" : "rb");
    if (file == NULL) (void)close(fd);
    return file;
}

static bool tail_month_matches(const archive_tail_t *tail, const char *month)
{
    char last_month[8];
    return !tail->has_last ||
        (environment_archive_month(tail->last.point.epoch_seconds, last_month) &&
         strcmp(last_month, month) == 0);
}

environment_storage_result_t environment_archive_inspect(const char *directory,
    const char *month, uint64_t *download_bytes,
    environment_storage_checkpoint_t checkpoint, void *context)
{
    char path[192];
    if (download_bytes == NULL || !file_path(path, directory, month)) return ENVIRONMENT_STORAGE_INVALID;
    *download_bytes = 0;
    if (!proceed(checkpoint, context)) return ENVIRONMENT_STORAGE_CANCELLED;
    FILE *file = open_file(path, false, false);
    if (file == NULL) return errno == ENOENT ? ENVIRONMENT_STORAGE_NOT_FOUND : ENVIRONMENT_STORAGE_IO_ERROR;
    archive_tail_t tail;
    environment_storage_result_t result = inspect_file(file, &tail, checkpoint, context);
    if (result == ENVIRONMENT_STORAGE_OK && !tail_month_matches(&tail, month))
        result = ENVIRONMENT_STORAGE_INVALID;
    if (result == ENVIRONMENT_STORAGE_OK) *download_bytes = tail.complete_bytes;
    if (fclose(file) != 0) result = ENVIRONMENT_STORAGE_IO_ERROR;
    return result;
}

environment_storage_result_t environment_archive_list(const char *directory,
    const char *before_month, environment_archive_page_t *page,
    environment_storage_checkpoint_t checkpoint, void *context)
{
    if (directory == NULL || page == NULL ||
        (before_month != NULL && *before_month && !environment_archive_valid_month(before_month)))
        return ENVIRONMENT_STORAGE_INVALID;
    memset(page, 0, sizeof(*page));
    if (!proceed(checkpoint, context)) return ENVIRONMENT_STORAGE_CANCELLED;
    DIR *dir = opendir(directory);
    if (dir == NULL) return errno == ENOENT ? ENVIRONMENT_STORAGE_NOT_FOUND : ENVIRONMENT_STORAGE_IO_ERROR;
    environment_storage_result_t result = ENVIRONMENT_STORAGE_OK;
    size_t matching = 0;
    unsigned visited = 0;
    for (;;) {
        if (!proceed(checkpoint, context)) { result = ENVIRONMENT_STORAGE_CANCELLED; break; }
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) result = ENVIRONMENT_STORAGE_IO_ERROR;
            break;
        }
        if (++visited > 4096) { result = ENVIRONMENT_STORAGE_LIMIT; break; }
        if (strlen(entry->d_name) != 11 || strcmp(entry->d_name + 7, ".csv") != 0) continue;
        char month[8]; memcpy(month, entry->d_name, 7); month[7] = '\0';
        if (!environment_archive_valid_month(month) ||
            (before_month != NULL && *before_month && strcmp(month, before_month) >= 0)) continue;
        ++matching;
        size_t insert = 0;
        while (insert < page->count && strcmp(page->entries[insert].month, month) > 0) ++insert;
        if (insert == ENVIRONMENT_ARCHIVE_PAGE_SIZE) continue;
        const size_t count = page->count < ENVIRONMENT_ARCHIVE_PAGE_SIZE ? page->count + 1 : page->count;
        for (size_t i = count - 1; i > insert; --i) page->entries[i] = page->entries[i - 1];
        memcpy(page->entries[insert].month, month, sizeof(month));
        page->entries[insert].size_bytes = 0;
        page->count = count;
    }
    if (closedir(dir) != 0) result = ENVIRONMENT_STORAGE_IO_ERROR;
    if (result != ENVIRONMENT_STORAGE_OK) { memset(page, 0, sizeof(*page)); return result; }
    page->has_more = matching > page->count;
    if (page->count != 0) memcpy(page->next_cursor, page->entries[page->count - 1].month, 8);
    /* Only the selected page is opened. Unrelated files are never returned.
     * Retain the raw cursor even if a malformed selected month is omitted. */
    size_t accepted = 0;
    for (size_t i = 0; i < page->count; ++i) {
        uint64_t bytes = 0;
        result = environment_archive_inspect(directory, page->entries[i].month, &bytes, checkpoint, context);
        if (result == ENVIRONMENT_STORAGE_CANCELLED || result == ENVIRONMENT_STORAGE_IO_ERROR)
            return result;
        if (result != ENVIRONMENT_STORAGE_OK) continue;
        page->entries[accepted] = page->entries[i];
        page->entries[accepted++].size_bytes = bytes;
    }
    page->count = accepted;
    return ENVIRONMENT_STORAGE_OK;
}

environment_storage_result_t environment_archive_stream(const char *directory,
    const char *month, environment_archive_write_t write, void *write_context,
    environment_storage_checkpoint_t checkpoint, void *context)
{
    char path[192];
    if (write == NULL || !file_path(path, directory, month)) return ENVIRONMENT_STORAGE_INVALID;
    if (!proceed(checkpoint, context)) return ENVIRONMENT_STORAGE_CANCELLED;
    FILE *file = open_file(path, false, false);
    if (file == NULL) return errno == ENOENT ? ENVIRONMENT_STORAGE_NOT_FOUND : ENVIRONMENT_STORAGE_IO_ERROR;
    archive_tail_t tail;
    environment_storage_result_t result = inspect_file(file, &tail, checkpoint, context);
    if (result == ENVIRONMENT_STORAGE_OK && !tail_month_matches(&tail, month))
        result = ENVIRONMENT_STORAGE_INVALID;
    if (result != ENVIRONMENT_STORAGE_OK) goto done;
    if (fseek(file, (long)(sizeof(header) - 1), SEEK_SET) != 0) { result = ENVIRONMENT_STORAGE_IO_ERROR; goto done; }
    if (!proceed(checkpoint, context) || !write(header, sizeof(header) - 1, write_context)) {
        result = ENVIRONMENT_STORAGE_CANCELLED; goto done;
    }
    char line[ENVIRONMENT_ARCHIVE_ROW_MAX];
    char output[1024];
    size_t used = 0;
    uint64_t read_bytes = sizeof(header) - 1;
    while (read_bytes < tail.complete_bytes) {
        if (!proceed(checkpoint, context)) { result = ENVIRONMENT_STORAGE_CANCELLED; break; }
        if (fgets(line, sizeof(line), file) == NULL) { result = ENVIRONMENT_STORAGE_IO_ERROR; break; }
        const size_t length = strlen(line);
        environment_archive_record_t record;
        char record_month[8];
        if (length > tail.complete_bytes - read_bytes || !environment_archive_decode(line, length, &record)) {
            result = ENVIRONMENT_STORAGE_INVALID; break;
        }
        if (!environment_archive_month(record.point.epoch_seconds, record_month) || strcmp(record_month, month)) {
            result = ENVIRONMENT_STORAGE_INVALID; break;
        }
        if (used + length > sizeof(output)) {
            if (!write(output, used, write_context)) { result = ENVIRONMENT_STORAGE_CANCELLED; break; }
            used = 0;
        }
        memcpy(output + used, line, length); used += length; read_bytes += length;
    }
    if (result == ENVIRONMENT_STORAGE_OK && used != 0 &&
        (!proceed(checkpoint, context) || !write(output, used, write_context)))
        result = ENVIRONMENT_STORAGE_CANCELLED;
done:
    if (fclose(file) != 0) result = ENVIRONMENT_STORAGE_IO_ERROR;
    return result;
}

environment_storage_result_t environment_archive_append(const char *directory,
    const environment_archive_record_t *record, uint64_t free_bytes,
    environment_storage_checkpoint_t checkpoint, void *context)
{
    char line[ENVIRONMENT_ARCHIVE_ROW_MAX], month[8], path[192];
    const size_t length = environment_archive_encode(record, line, sizeof(line));
    if (length == 0 || !environment_archive_month(record->point.epoch_seconds, month) ||
        !file_path(path, directory, month)) return ENVIRONMENT_STORAGE_INVALID;
    if (!proceed(checkpoint, context)) return ENVIRONMENT_STORAGE_CANCELLED;
    /* Allow four large FAT clusters for new directories/file allocation in
     * addition to the fixed reserve. Never create a directory below this. */
    if (free_bytes < ENVIRONMENT_ARCHIVE_FREE_RESERVE + ENVIRONMENT_ARCHIVE_WRITE_ALLOWANCE)
        return ENVIRONMENT_STORAGE_NO_SPACE;
    struct stat st;
    if (stat(directory, &st) != 0) {
        if (errno != ENOENT || mkdir(directory, 0755) != 0) return ENVIRONMENT_STORAGE_IO_ERROR;
    } else if (!S_ISDIR(st.st_mode)) return ENVIRONMENT_STORAGE_FOREIGN_FILE;
    FILE *file = open_file(path, true, false);
    bool created = false;
    if (file == NULL && errno == ENOENT) {
        file = open_file(path, true, true);
        created = file != NULL;
    }
    if (file == NULL) return ENVIRONMENT_STORAGE_IO_ERROR;
    environment_storage_result_t result = ENVIRONMENT_STORAGE_OK;
    if (created) {
        /* No cancellation gap between exclusive creation and the short
         * identifying header. Ordinary cancellation must not strand an empty
         * ambiguous file; real power loss can still require user repair. */
        if (fwrite(header, 1, sizeof(header) - 1, file) != sizeof(header) - 1 ||
            fflush(file) != 0 || fsync(fileno(file)) != 0) {
            result = ENVIRONMENT_STORAGE_IO_ERROR; goto done;
        }
    }
    archive_tail_t tail;
    result = inspect_file(file, &tail, checkpoint, context);
    if (result == ENVIRONMENT_STORAGE_OK && !tail_month_matches(&tail, month))
        result = ENVIRONMENT_STORAGE_INVALID;
    if (result != ENVIRONMENT_STORAGE_OK) goto done;
    if (tail.has_last && tail.last.boot_id == record->boot_id &&
        tail.last.point.start_monotonic_ms == record->point.start_monotonic_ms) {
        char previous[ENVIRONMENT_ARCHIVE_ROW_MAX];
        const size_t old_length = environment_archive_encode(&tail.last, previous, sizeof(previous));
        /* Never silently accept a conflicting aggregate with the same ID. */
        result = old_length == length && memcmp(previous, line, length) == 0 ?
            ENVIRONMENT_STORAGE_OK : ENVIRONMENT_STORAGE_INVALID;
        goto done;
    }
    if (tail.complete_bytes + length > ENVIRONMENT_ARCHIVE_MONTH_MAX) {
        result = ENVIRONMENT_STORAGE_LIMIT; goto done;
    }
    if (!proceed(checkpoint, context)) { result = ENVIRONMENT_STORAGE_CANCELLED; goto done; }
    if (tail.bytes != tail.complete_bytes &&
        ftruncate(fileno(file), (off_t)tail.complete_bytes) != 0) {
        result = ENVIRONMENT_STORAGE_IO_ERROR; goto done;
    }
    if (fseek(file, (long)tail.complete_bytes, SEEK_SET) != 0 ||
        fwrite(line, 1, length, file) != length || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        result = ENVIRONMENT_STORAGE_IO_ERROR; goto done;
    }
    /* Validate the append before publishing progress. A failed fsync/close
     * retries this same ID; an already-complete matching row is a no-op. */
    archive_tail_t verified;
    result = inspect_file(file, &verified, checkpoint, context);
    if (result == ENVIRONMENT_STORAGE_OK &&
        (!verified.has_last || verified.complete_bytes != tail.complete_bytes + length ||
         verified.last.boot_id != record->boot_id ||
         verified.last.point.start_monotonic_ms != record->point.start_monotonic_ms))
        result = ENVIRONMENT_STORAGE_INVALID;
done:
    if (fclose(file) != 0) result = ENVIRONMENT_STORAGE_IO_ERROR;
    return result;
}
