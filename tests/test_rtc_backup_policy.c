#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rtc_backup_policy.h"

int main(void)
{
    rtc_backup_evaluation_t evaluation = {0};
    assert(rtc_backup_evaluate(NULL) == RTC_BACKUP_STATUS_UNTESTED);
    assert(rtc_backup_evaluate(&evaluation) ==
           RTC_BACKUP_STATUS_UNTESTED);

    evaluation.previous_marker_valid = true;
    evaluation.previous_marker = 20260821090000ULL;
    evaluation.previous_status = RTC_BACKUP_STATUS_VERIFIED;
    assert(rtc_backup_evaluate(&evaluation) ==
           RTC_BACKUP_STATUS_VERIFIED);

    evaluation.previous_status = RTC_BACKUP_STATUS_UNTESTED;
    evaluation.rtc_readable = true;
    evaluation.clock_integrity = false;
    assert(rtc_backup_evaluate(&evaluation) == RTC_BACKUP_STATUS_FAILED);

    evaluation.clock_integrity = true;
    evaluation.current_marker = 20260821090500ULL;
    assert(rtc_backup_evaluate(&evaluation) ==
           RTC_BACKUP_STATUS_UNTESTED);

    evaluation.power_cycle = true;
    evaluation.current_marker = evaluation.previous_marker;
    assert(rtc_backup_evaluate(&evaluation) ==
           RTC_BACKUP_STATUS_UNTESTED);

    evaluation.current_marker = 20260821091000ULL;
    assert(rtc_backup_evaluate(&evaluation) ==
           RTC_BACKUP_STATUS_VERIFIED);

    evaluation.previous_status = RTC_BACKUP_STATUS_FAILED;
    evaluation.current_marker = 20260821083000ULL;
    assert(rtc_backup_evaluate(&evaluation) ==
           RTC_BACKUP_STATUS_VERIFIED);

    evaluation.previous_status = (rtc_backup_status_t)99;
    evaluation.rtc_readable = false;
    assert(rtc_backup_evaluate(&evaluation) ==
           RTC_BACKUP_STATUS_UNTESTED);

    assert(strcmp(rtc_backup_status_name(RTC_BACKUP_STATUS_UNTESTED),
                  "UNTESTED") == 0);
    assert(strcmp(rtc_backup_status_name(RTC_BACKUP_STATUS_VERIFIED),
                  "VERIFIED") == 0);
    assert(strcmp(rtc_backup_status_name(RTC_BACKUP_STATUS_FAILED),
                  "FAILED") == 0);

    puts("RTC backup policy tests passed");
    return 0;
}
