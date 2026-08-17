#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rtc_time_sync_init(void);
void rtc_time_sync_poll(bool rtc_available);

#ifdef __cplusplus
}
#endif
