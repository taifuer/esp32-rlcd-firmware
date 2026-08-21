#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "pcf85063.h"
#include "rtc_backup_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rtc_backup_monitor_init(bool power_cycle, bool rtc_readable,
                                  const pcf85063_datetime_t *datetime);
esp_err_t rtc_backup_monitor_arm(const pcf85063_datetime_t *datetime);
rtc_backup_status_t rtc_backup_monitor_status(void);

#ifdef __cplusplus
}
#endif
