#pragma once

#include "conversation_config_model.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    conversation_config_t config;
    /* Advances on successful save, migration or clear. After clear this is
     * only a RAM identity until another save; an empty cold boot starts at 0. */
    uint32_t generation;
} conversation_config_snapshot_t;

/* Migrates a legacy model or schema record locally without network access. If the
 * migration cannot be committed, initialization still succeeds with the new
 * model in RAM and status.migration == PENDING; save or the next boot retries
 * persistence, retaining the previous valid slot and credentials. */
esp_err_t conversation_config_init(void);

/* This copies the API key for use by the cloud-session worker. Callers must
 * erase the snapshot with conversation_config_clear_sensitive() as soon as
 * the request has finished. Presentation code should use get_status(). */
esp_err_t conversation_config_get_snapshot(
    conversation_config_snapshot_t *snapshot);
esp_err_t conversation_config_get_status(
    conversation_config_status_t *status);

/* The update is committed as one validated record. An empty api_key preserves
 * the current key; preserve_enabled and omitted voice/style choices merge
 * against the latest record while locked. conversation_config_clear() is the
 * only API that removes stored cloud credentials. */
esp_err_t conversation_config_save(
    const conversation_config_update_t *update);
/* Saves only non-secret preferences against the latest config.
 * Returns ESP_ERR_INVALID_STATE if no usable API key/config is saved. */
esp_err_t conversation_config_save_preferences(
    const conversation_preferences_t *preferences);
/* Changes only the key against the latest config. A first save stays disabled;
 * an empty key preserves the current key or returns ESP_ERR_INVALID_STATE if
 * none is saved. The caller must clear the sensitive update after use. */
esp_err_t conversation_config_save_api_key(
    const conversation_api_key_update_t *update);
esp_err_t conversation_config_clear(void);

#ifdef __cplusplus
}
#endif
