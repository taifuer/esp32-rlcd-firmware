#pragma once

#include "esp_err.h"
#include "weather_cache_record.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t weather_cache_init(void);
esp_err_t weather_cache_get(weather_cache_data_t *data);
esp_err_t weather_cache_save(const weather_cache_data_t *data);
esp_err_t weather_cache_clear(void);

#ifdef __cplusplus
}
#endif
