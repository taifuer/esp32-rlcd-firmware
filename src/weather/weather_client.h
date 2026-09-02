#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "weather_config_model.h"
#include "weather_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t latitude_ten_thousandths;
    int32_t longitude_ten_thousandths;
    char name[49];
} weather_client_location_t;

esp_err_t weather_client_resolve_location(
    const weather_config_t *config, weather_client_location_t *location);
esp_err_t weather_client_fetch_current(
    const weather_config_t *config,
    const weather_client_location_t *location,
    weather_current_t *current);
esp_err_t weather_client_fetch_daily(
    const weather_config_t *config,
    const weather_client_location_t *location,
    weather_daily_t *daily);

#ifdef __cplusplus
}
#endif
