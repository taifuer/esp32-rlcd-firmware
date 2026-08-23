#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Set the board RTC from an absolute Unix timestamp. The timestamp is
 * converted with the process' current TZ setting before it is written to the
 * PCF85063, whose registers contain local civil time.
 */
esp_err_t clock_service_set_unix_time(int64_t unix_seconds);

#ifdef __cplusplus
}
#endif
