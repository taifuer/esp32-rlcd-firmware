#pragma once

#include "environment_history.h"

#define ENVIRONMENT_STORAGE_DIRECTORY "/rlcd/environment"
#define ENVIRONMENT_STORAGE_WIRE_MAX (40U + 36U * ENVIRONMENT_HISTORY_CAPACITY)

typedef enum {
    ENVIRONMENT_STORAGE_OK,
    ENVIRONMENT_STORAGE_NOT_FOUND,
    ENVIRONMENT_STORAGE_INVALID,
    ENVIRONMENT_STORAGE_NO_MEMORY,
    ENVIRONMENT_STORAGE_IO_ERROR,
    ENVIRONMENT_STORAGE_FOREIGN_FILE,
    ENVIRONMENT_STORAGE_CANCELLED,
    ENVIRONMENT_STORAGE_NO_SPACE,
    ENVIRONMENT_STORAGE_LIMIT,
} environment_storage_result_t;

size_t environment_storage_encode(const environment_history_snapshot_t *snapshot,
    uint32_t sequence, uint8_t *bytes, size_t capacity);
bool environment_storage_decode(const uint8_t *bytes, size_t length,
    environment_history_snapshot_t *snapshot, uint32_t *sequence);
/* Caller owns the shared SD lease. Only these two exact application-owned
 * snapshots and our exclusive temporary file are touched. No directory scans,
 * formatting, user media changes, or unbounded history growth. */
environment_storage_result_t environment_storage_load(const char *directory,
    environment_history_snapshot_t *snapshot, uint32_t *sequence);
typedef bool (*environment_storage_checkpoint_t)(void *context);
environment_storage_result_t environment_storage_save(const char *directory,
    const environment_history_snapshot_t *snapshot, uint32_t sequence,
    uint32_t nonce, environment_storage_checkpoint_t checkpoint, void *context);
