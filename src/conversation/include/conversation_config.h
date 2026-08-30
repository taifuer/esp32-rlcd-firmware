#pragma once

#include "conversation_config_model.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    conversation_config_t config;
    uint32_t generation;
} conversation_config_snapshot_t;

esp_err_t conversation_config_init(void);

/* This copies the API key for use by the cloud-session worker. Callers must
 * erase the snapshot with conversation_config_clear_sensitive() as soon as
 * the request has finished. Presentation code should use get_status(). */
esp_err_t conversation_config_get_snapshot(
    conversation_config_snapshot_t *snapshot);
esp_err_t conversation_config_get_status(
    conversation_config_status_t *status);

/* The update is committed as one validated record. An empty api_key preserves
 * the current key; conversation_config_clear() is the only API that removes
 * stored cloud credentials. */
esp_err_t conversation_config_save(
    const conversation_config_update_t *update);
esp_err_t conversation_config_clear(void);

#ifdef __cplusplus
}
#endif
