#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} clock_service_datetime_t;

/*
 * Convert an absolute timestamp using the process' current TZ setting. This
 * function contains no device or ESP-IDF dependencies so its boundary and
 * timezone behavior can be covered by host tests.
 */
bool clock_service_unix_to_local(int64_t unix_seconds,
                                 clock_service_datetime_t *datetime);

#ifdef __cplusplus
}
#endif
