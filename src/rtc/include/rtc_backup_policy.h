#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RTC_BACKUP_STATUS_UNTESTED = 0,
    RTC_BACKUP_STATUS_VERIFIED,
    RTC_BACKUP_STATUS_FAILED,
} rtc_backup_status_t;

typedef struct {
    bool previous_marker_valid;
    uint64_t previous_marker;
    rtc_backup_status_t previous_status;
    bool power_cycle;
    bool rtc_readable;
    bool clock_integrity;
    uint64_t current_marker;
} rtc_backup_evaluation_t;

rtc_backup_status_t rtc_backup_evaluate(
    const rtc_backup_evaluation_t *evaluation);
const char *rtc_backup_status_name(rtc_backup_status_t status);

#ifdef __cplusplus
}
#endif
