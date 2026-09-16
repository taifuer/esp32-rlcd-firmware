#pragma once

#include "esp_err.h"
#include "market_model.h"

esp_err_t market_config_init(void);
/* Safe before service startup: configuration initialization is independent. */
esp_err_t market_config_get(market_config_t *config);
esp_err_t market_config_get_snapshot(market_config_t *config,
                                    uint32_t *generation);
/* Only writes when changed. Invalid count/duplicate/unknown IDs rejected. */
esp_err_t market_config_set(const market_config_t *config);
