#pragma once

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SETTINGS_BACKUP_MAX_LENGTH 4096U
#define APP_SETTINGS_BACKUP_FORMAT "esp32-rlcd-preferences"
#define APP_SETTINGS_BACKUP_VERSION 1U

/* Versioned, bounded, canonical ASCII JSON. No credentials, external service
 * configuration, update channel, manual saving or automatic SD-write consent.
 * Parsers reject unknown/duplicate/missing fields and leave output unchanged
 * on failure. Imported ordinary preferences merge with the latest base. */
bool settings_backup_encode(const app_settings_t *settings, char *buffer,
                            size_t capacity, size_t *length);
bool settings_backup_parse(const char *json, size_t length,
                           const app_settings_t *base,
                           app_settings_t *settings);

#ifdef __cplusplus
}
#endif
