#pragma once

#include "environment_storage.h"

#define ENVIRONMENT_ARCHIVE_DIRECTORY "/rlcd/environment/archive"
#define ENVIRONMENT_ARCHIVE_PAGE_SIZE 24U
#define ENVIRONMENT_ARCHIVE_MONTH_MAX (8U * 1024U * 1024U)
#define ENVIRONMENT_ARCHIVE_FREE_RESERVE (UINT64_C(16) * 1024U * 1024U)
#define ENVIRONMENT_ARCHIVE_WRITE_ALLOWANCE (UINT64_C(256) * 1024U)
#define ENVIRONMENT_ARCHIVE_ROW_MAX 192U

typedef struct {
    uint64_t boot_id;
    environment_history_point_t point;
} environment_archive_record_t;

typedef struct {
    char month[8]; /* UTC YYYY-MM, not a path or filename supplied by a client. */
    uint64_t size_bytes; /* Downloadable, fully terminated rows only. */
} environment_archive_entry_t;

typedef struct {
    size_t count;
    bool has_more;
    char next_cursor[8];
    environment_archive_entry_t entries[ENVIRONMENT_ARCHIVE_PAGE_SIZE];
} environment_archive_page_t;

typedef bool (*environment_archive_write_t)(const char *bytes, size_t length, void *context);

bool environment_archive_valid_month(const char *month);
bool environment_archive_month(int64_t epoch_seconds, char month[8]);
const char *environment_archive_header(void);
size_t environment_archive_encode(const environment_archive_record_t *record,
    char *line, size_t capacity);
bool environment_archive_decode(const char *line, size_t length,
    environment_archive_record_t *record);

/* All filesystem functions require a caller-owned SD lease. No automatic
 * mounts, playback cancellation, format, overwrite, history pruning or scans
 * at boot. Directory scans are limited to 4096 entries and checkpointed.
 * before_month is NULL/empty for newest, or an exclusive YYYY-MM cursor.
 * NOT_FOUND means absent directory/month, INVALID includes a damaged complete
 * record, NO_SPACE leaves existing data untouched, LIMIT is a safety bound.
 * A read never repairs files. An incomplete final row is omitted from size.
 */
environment_storage_result_t environment_archive_list(const char *directory,
    const char *before_month, environment_archive_page_t *page,
    environment_storage_checkpoint_t checkpoint, void *context);
environment_storage_result_t environment_archive_inspect(const char *directory,
    const char *month, uint64_t *download_bytes,
    environment_storage_checkpoint_t checkpoint, void *context);
environment_storage_result_t environment_archive_stream(const char *directory,
    const char *month, environment_archive_write_t write, void *write_context,
    environment_storage_checkpoint_t checkpoint, void *context);
/* One completed live bucket. Duplicate retry of the last record is a no-op.
 * Only an unterminated tail following our exact header/valid previous row can
 * be truncated; complete corrupt rows are never truncated or overwritten.
 * free_bytes must come from the same mounted filesystem/lease immediately
 * before the call; 16 MiB plus write allocation allowance is preserved. */
environment_storage_result_t environment_archive_append(const char *directory,
    const environment_archive_record_t *record, uint64_t free_bytes,
    environment_storage_checkpoint_t checkpoint, void *context);
