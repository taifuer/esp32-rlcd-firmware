#pragma once

#include "esp_err.h"
#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_settings_init(void);
esp_err_t app_settings_get(app_settings_t *settings);
esp_err_t app_settings_save(const app_settings_t *settings);
esp_err_t app_settings_restore_defaults(void);
esp_err_t app_settings_apply_timezone(const app_settings_t *settings);

#ifdef __cplusplus
}
#endif
