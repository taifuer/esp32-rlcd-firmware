#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t network_time_begin_online_session(uint32_t timeout_ms);
esp_err_t network_time_end_online_session(void);
