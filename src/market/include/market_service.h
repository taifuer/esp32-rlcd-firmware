#pragma once

#include "esp_err.h"
#include "market_model.h"

typedef enum {
    MARKET_SERVICE_STATE_DISABLED = 0,
    MARKET_SERVICE_STATE_NO_DATA,
    MARKET_SERVICE_STATE_REFRESHING,
    MARKET_SERVICE_STATE_READY,
    MARKET_SERVICE_STATE_FAILED,
} market_service_state_t;

typedef struct {
    market_service_state_t state;
    market_config_t config;
    market_row_t rows[MARKET_MAX_ROWS];
    bool visible;
    bool automatic_refresh_enabled;
    esp_err_t last_error;
    uint32_t revision;
} market_service_status_t;

esp_err_t market_service_init(void);
esp_err_t market_service_set_activity(bool visible,
                                      bool automatic_refresh_enabled);
esp_err_t market_service_request_refresh(void);
esp_err_t market_service_notify_configuration_changed(void);
esp_err_t market_service_get_status(market_service_status_t *status);
