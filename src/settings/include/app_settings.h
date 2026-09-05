#pragma once

#include "esp_err.h"
#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    app_settings_t settings;
    /*
     * Opaque persisted-record generation. Compare it for equality only: a
     * successful save may wrap this value after UINT32_MAX.
     */
    uint32_t generation;
} app_settings_snapshot_t;

esp_err_t app_settings_init(void);
/*
 * Copy the settings and their generation while holding the same lock. This
 * is the race-free read API for consumers which hot-apply saved changes.
 */
esp_err_t app_settings_get_snapshot(app_settings_snapshot_t *snapshot);
/* Convenience wrapper for callers which do not need change detection. */
esp_err_t app_settings_get(app_settings_t *settings);
esp_err_t app_settings_save(const app_settings_t *settings);
/* Save exactly one device-editable field under the settings lock. */
esp_err_t app_settings_save_field(app_setting_field_t field, uint8_t value);
/* Atomically toggle only the persisted manual saving request while
 * preserving every other field from the latest record. */
esp_err_t app_settings_toggle_manual_saving(bool *saved_requested);
esp_err_t app_settings_restore_defaults(void);
esp_err_t app_settings_apply_timezone(const app_settings_t *settings);

#ifdef __cplusplus
}
#endif
