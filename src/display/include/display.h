#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *firmware_version;
    bool time_valid;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool environment_valid;
    float temperature_c;
    float humidity_percent;
    uint16_t sensor_id;
    const char *rtc_status;
    const char *sensor_status;
} display_dashboard_t;

esp_err_t display_init(void);
void display_show_status(const char *title, const char *detail);
void display_show_dashboard(const display_dashboard_t *dashboard);

#ifdef __cplusplus
}
#endif
