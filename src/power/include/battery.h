#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t voltage_mv;
    uint8_t percent;
    bool calibrated;
} battery_measurement_t;

esp_err_t battery_init(void);
esp_err_t battery_read(battery_measurement_t *measurement);

#ifdef __cplusplus
}
#endif
