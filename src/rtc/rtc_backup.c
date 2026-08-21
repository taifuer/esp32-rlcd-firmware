#include "rtc_backup.h"

#include <stdint.h>

#include "nvs.h"

#define RTC_BACKUP_NAMESPACE "rlcd_rtc"
#define RTC_BACKUP_MARKER_KEY "marker"
#define RTC_BACKUP_STATUS_KEY "status"

static bool s_initialized;
static bool s_armed;
static rtc_backup_status_t s_status = RTC_BACKUP_STATUS_UNTESTED;

static uint64_t datetime_marker(const pcf85063_datetime_t *datetime)
{
    uint64_t marker = datetime->year;
    marker = marker * 100U + datetime->month;
    marker = marker * 100U + datetime->day;
    marker = marker * 100U + datetime->hour;
    marker = marker * 100U + datetime->minute;
    marker = marker * 100U + datetime->second;
    return marker;
}

static bool usable_datetime(bool rtc_readable,
                            const pcf85063_datetime_t *datetime)
{
    return rtc_readable && datetime != NULL &&
           pcf85063_datetime_is_valid(datetime);
}

esp_err_t rtc_backup_monitor_init(bool power_cycle, bool rtc_readable,
                                  const pcf85063_datetime_t *datetime)
{
    s_initialized = false;
    s_armed = false;
    s_status = RTC_BACKUP_STATUS_UNTESTED;

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(RTC_BACKUP_NAMESPACE, NVS_READWRITE,
                               &handle);
    if (error != ESP_OK) {
        return error;
    }

    uint64_t previous_marker = 0U;
    bool previous_marker_valid = false;
    error = nvs_get_u64(handle, RTC_BACKUP_MARKER_KEY, &previous_marker);
    if (error == ESP_OK) {
        previous_marker_valid = true;
    } else if (error != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return error;
    }

    uint8_t stored_status = RTC_BACKUP_STATUS_UNTESTED;
    const esp_err_t status_error =
        nvs_get_u8(handle, RTC_BACKUP_STATUS_KEY, &stored_status);
    if (status_error != ESP_OK && status_error != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return status_error;
    }

    const bool current_usable = usable_datetime(rtc_readable, datetime);
    const uint64_t current_marker =
        current_usable ? datetime_marker(datetime) : 0U;
    const rtc_backup_evaluation_t evaluation = {
        .previous_marker_valid = previous_marker_valid,
        .previous_marker = previous_marker,
        .previous_status = (rtc_backup_status_t)stored_status,
        .power_cycle = power_cycle,
        .rtc_readable = current_usable,
        .clock_integrity = current_usable && datetime->clock_integrity,
        .current_marker = current_marker,
    };
    s_status = rtc_backup_evaluate(&evaluation);

    bool changed = status_error == ESP_ERR_NVS_NOT_FOUND ||
                   stored_status != (uint8_t)s_status;
    if (changed) {
        error = nvs_set_u8(handle, RTC_BACKUP_STATUS_KEY,
                           (uint8_t)s_status);
    }
    if (error == ESP_OK && current_usable && datetime->clock_integrity) {
        if ((!previous_marker_valid || power_cycle) &&
            previous_marker != current_marker) {
            error = nvs_set_u64(handle, RTC_BACKUP_MARKER_KEY,
                                current_marker);
            changed = true;
        }
        if (error == ESP_OK) {
            s_armed = true;
        }
    }
    if (error == ESP_OK && changed) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);

    if (error == ESP_OK) {
        s_initialized = true;
    }
    return error;
}

esp_err_t rtc_backup_monitor_arm(const pcf85063_datetime_t *datetime)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_armed) {
        return ESP_OK;
    }
    if (datetime == NULL || !datetime->clock_integrity ||
        !pcf85063_datetime_is_valid(datetime)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(RTC_BACKUP_NAMESPACE, NVS_READWRITE,
                               &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_u64(handle, RTC_BACKUP_MARKER_KEY,
                        datetime_marker(datetime));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    if (error == ESP_OK) {
        s_armed = true;
    }
    return error;
}

rtc_backup_status_t rtc_backup_monitor_status(void)
{
    return s_status;
}
