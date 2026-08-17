#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_I2C_ADDRESS 0x51

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool clock_integrity;
} pcf85063_datetime_t;

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus);
esp_err_t pcf85063_read(pcf85063_datetime_t *datetime);
esp_err_t pcf85063_write(const pcf85063_datetime_t *datetime);
esp_err_t pcf85063_calculate_weekday(uint16_t year, uint8_t month, uint8_t day,
                                     uint8_t *weekday);
bool pcf85063_datetime_is_valid(const pcf85063_datetime_t *datetime);

#ifdef __cplusplus
}
#endif
