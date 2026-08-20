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

typedef struct {
    const char *firmware_version;
    bool rtc_ready;
    bool time_valid;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    bool sensor_ready;
    bool environment_valid;
    float temperature_c;
    float humidity_percent;
    bool battery_ready;
    bool battery_valid;
    uint16_t battery_voltage_mv;
    uint8_t battery_percent;
    bool network_ready;
    bool network_configured;
    const char *network_state;
    bool last_sync_valid;
    uint16_t last_sync_year;
    uint8_t last_sync_month;
    uint8_t last_sync_day;
    uint8_t last_sync_hour;
    uint8_t last_sync_minute;
} display_device_status_t;

esp_err_t display_init(void);
void display_show_status(const char *title, const char *detail);
void display_show_network_setup(const char *ssid, const char *password, const char *url);
void display_show_dashboard(const display_dashboard_t *dashboard);
void display_show_device_status(const display_device_status_t *status);

#ifdef __cplusplus
}
#endif
