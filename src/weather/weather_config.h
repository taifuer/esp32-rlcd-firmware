#pragma once

#include "esp_err.h"
#include "weather_config_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    weather_config_t config;
    uint32_t generation;
} weather_config_snapshot_t;

esp_err_t weather_config_init(void);

/* This copies the API key for use by the weather worker. Erase the snapshot
 * with weather_config_clear_sensitive() after the request finishes.
 * Presentation code must use weather_config_get_status(). */
esp_err_t weather_config_get_snapshot(weather_config_snapshot_t *snapshot);
esp_err_t weather_config_get_status(weather_config_status_t *status);

/* An empty update API key preserves the stored key. The clear operation is
 * the only API that removes stored weather credentials. */
esp_err_t weather_config_save(const weather_config_update_t *update);
esp_err_t weather_config_clear(void);

#ifdef __cplusplus
}
#endif
