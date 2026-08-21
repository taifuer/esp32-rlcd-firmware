#include "rtc_backup_policy.h"

#include <stddef.h>

static rtc_backup_status_t normalize_status(rtc_backup_status_t status)
{
    if (status < RTC_BACKUP_STATUS_UNTESTED ||
        status > RTC_BACKUP_STATUS_FAILED) {
        return RTC_BACKUP_STATUS_UNTESTED;
    }
    return status;
}

rtc_backup_status_t rtc_backup_evaluate(
    const rtc_backup_evaluation_t *evaluation)
{
    if (evaluation == NULL || !evaluation->previous_marker_valid) {
        return RTC_BACKUP_STATUS_UNTESTED;
    }

    const rtc_backup_status_t previous =
        normalize_status(evaluation->previous_status);
    if (!evaluation->rtc_readable) {
        return previous;
    }
    if (!evaluation->clock_integrity) {
        return RTC_BACKUP_STATUS_FAILED;
    }
    if (!evaluation->power_cycle) {
        return previous;
    }
    if (evaluation->current_marker != evaluation->previous_marker) {
        return RTC_BACKUP_STATUS_VERIFIED;
    }
    return previous;
}

const char *rtc_backup_status_name(rtc_backup_status_t status)
{
    switch (status) {
    case RTC_BACKUP_STATUS_VERIFIED:
        return "VERIFIED";
    case RTC_BACKUP_STATUS_FAILED:
        return "FAILED";
    case RTC_BACKUP_STATUS_UNTESTED:
    default:
        return "UNTESTED";
    }
}
