#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configure ESP-IDF dynamic frequency scaling without enabling automatic
 * Light-sleep. NORMAL holds the CPU at the configured maximum frequency;
 * SAVING releases that application lock so the RTOS and peripheral drivers
 * can select the required frequency while work is active.
 */
esp_err_t power_manager_init(bool saving);
esp_err_t power_manager_set_saving(bool saving);
bool power_manager_is_ready(void);
bool power_manager_is_saving(void);

#ifdef __cplusplus
}
#endif
