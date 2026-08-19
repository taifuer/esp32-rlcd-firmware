#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_NETWORK_UNCONFIGURED = 0,
    DISPLAY_NETWORK_CONNECTING,
    DISPLAY_NETWORK_SYNCHRONIZED,
    DISPLAY_NETWORK_ERROR,
} display_network_state_t;

typedef struct {
    bool time_valid;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool lunar_valid;
    const char *lunar_text;
    bool environment_valid;
    float temperature_c;
    float humidity_percent;
    bool battery_valid;
    uint8_t battery_percent;
    display_network_state_t network_state;
} display_dashboard_t;

esp_err_t display_init(void);
void display_show_status(const char *title, const char *detail);
void display_show_network_setup(const char *ssid, const char *password, const char *url);
void display_show_dashboard(const display_dashboard_t *dashboard);

#ifdef __cplusplus
}
#endif
