#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHTC3_I2C_ADDRESS 0x70

typedef struct {
    float temperature_c;
    float humidity_percent;
    uint16_t sensor_id;
} shtc3_measurement_t;

esp_err_t shtc3_init(i2c_master_bus_handle_t bus, uint16_t *sensor_id);
esp_err_t shtc3_read(shtc3_measurement_t *measurement);

#ifdef __cplusplus
}
#endif
