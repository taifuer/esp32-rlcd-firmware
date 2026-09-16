#pragma once

#include "esp_err.h"
#include "market_model.h"

typedef bool (*market_cancel_callback_t)(void *context);

/* Fixed HTTPS endpoint, certificate validation, no redirects or credentials. */
esp_err_t market_client_fetch(const market_config_t *config,
                              market_row_t rows[MARKET_MAX_ROWS],
                              market_cancel_callback_t cancelled,
                              void *context);
