#pragma once

#include <stdbool.h>

#include "alarm_history_record.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A missing or corrupt record is reported as found=false, not as an error. */
esp_err_t alarm_history_load(alarm_history_record_t *record, bool *found);

/* The new record becomes visible only after the NVS commit succeeds. */
esp_err_t alarm_history_store(const alarm_history_record_t *record);

#ifdef __cplusplus
}
#endif
