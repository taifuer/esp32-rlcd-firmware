#include "clock_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/time.h>

#include "clock_service_policy.h"
#include "pcf85063.h"

esp_err_t clock_service_set_unix_time(int64_t unix_seconds)
{
    clock_service_datetime_t local = {0};
    if (!clock_service_unix_to_local(unix_seconds, &local)) {
        return ESP_ERR_INVALID_ARG;
    }

    const pcf85063_datetime_t requested = {
        .year = local.year,
        .month = local.month,
        .day = local.day,
        .weekday = local.weekday,
        .hour = local.hour,
        .minute = local.minute,
        .second = local.second,
        .clock_integrity = true,
    };
    esp_err_t error = pcf85063_write(&requested);
    if (error != ESP_OK) {
        return error;
    }

    pcf85063_datetime_t verified = {0};
    error = pcf85063_read(&verified);
    if (error != ESP_OK) {
        return error;
    }
    if (!verified.clock_integrity || !pcf85063_datetime_is_valid(&verified)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const struct timeval system_time = {
        .tv_sec = (time_t)unix_seconds,
        .tv_usec = 0,
    };
    return settimeofday(&system_time, NULL) == 0 ? ESP_OK : ESP_FAIL;
}
