#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_i2c_init(void);
i2c_master_bus_handle_t board_i2c_bus(void);
esp_err_t board_i2c_probe(uint16_t address, int timeout_ms);

#ifdef __cplusplus
}
#endif
