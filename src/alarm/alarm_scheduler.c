#include "alarm_scheduler.h"

#include <stddef.h>
#include <string.h>

enum {
    /* A small backward correction can otherwise replay the same occurrence.
     * A larger gap is treated as an implausible future RTC/history value so
     * one bad clock sample cannot silence this rule for months or years. */
    ALARM_ROLLBACK_GUARD_DAYS = 7U,
};

static bool is_leap_year(uint32_t year)
{
    return year % 4U == 0U &&
           (year % 100U != 0U || year % 400U == 0U);
}

static bool date_key_to_day_index(uint32_t date_key, uint32_t *day_index)
{
    if (day_index == NULL) {
        return false;
    }

    const uint32_t year = date_key / 10000U;
    const uint32_t month = date_key / 100U % 100U;
    const uint32_t day = date_key % 100U;
    if (year < 2000U || year > 2099U || month < 1U || month > 12U) {
        return false;
    }

    static const uint8_t days_per_month[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    uint32_t maximum_day = days_per_month[month - 1U];
    if (month == 2U && is_leap_year(year)) {
        maximum_day = 29U;
    }
    if (day < 1U || day > maximum_day) {
        return false;
    }

    uint32_t result = 0U;
    for (uint32_t current_year = 2000U; current_year < year;
         ++current_year) {
        result += is_leap_year(current_year) ? 366U : 365U;
    }
    for (uint32_t current_month = 1U; current_month < month;
         ++current_month) {
        result += days_per_month[current_month - 1U];
        if (current_month == 2U && is_leap_year(year)) {
            result++;
        }
    }
    *day_index = result + day - 1U;
    return true;
}

static alarm_scheduler_result_t result_for(
    const alarm_scheduler_t *scheduler,
    alarm_scheduler_output_t output)
{
    return (alarm_scheduler_result_t){
        .state = scheduler != NULL ? scheduler->state
                                   : ALARM_SCHEDULER_DISABLED,
        .output = output,
        .snooze_available =
            scheduler != NULL && scheduler->state == ALARM_SCHEDULER_RINGING &&
            scheduler->snooze_available,
    };
}

void alarm_scheduler_init(alarm_scheduler_t *scheduler)
{
    if (scheduler == NULL) {
        return;
    }
    memset(scheduler, 0, sizeof(*scheduler));
    scheduler->state = ALARM_SCHEDULER_DISABLED;
}

bool alarm_scheduler_restore_last_fired(alarm_scheduler_t *scheduler,
                                        uint32_t date_key,
                                        uint32_t schedule_revision)
{
    uint32_t ignored_day_index = 0U;
    if (scheduler == NULL ||
        !date_key_to_day_index(date_key, &ignored_day_index)) {
        return false;
    }
    scheduler->has_last_fired_date = true;
    scheduler->last_fired_date_key = date_key;
    scheduler->last_fired_revision = schedule_revision;
    return true;
}

bool alarm_schedule_is_valid(const alarm_schedule_t *schedule)
{
    if (schedule == NULL || schedule->hour >= 24U ||
        schedule->minute >= 60U ||
        (schedule->repeat_weekdays & (uint8_t)~ALARM_WEEKDAY_ALL) != 0U) {
        return false;
    }
    return !schedule->enabled || schedule->repeat_weekdays != 0U;
}

static bool clock_is_valid(const alarm_clock_observation_t *clock)
{
    uint32_t ignored_day_index = 0U;
    return clock != NULL && clock->valid && clock->weekday < 7U &&
           clock->hour < 24U && clock->minute < 60U &&
           date_key_to_day_index(clock->date_key, &ignored_day_index);
}

static bool schedule_matches(const alarm_schedule_t *schedule,
                             const alarm_clock_observation_t *clock)
{
    const uint8_t weekday_bit = (uint8_t)(1U << clock->weekday);
    return schedule->hour == clock->hour &&
           schedule->minute == clock->minute &&
           (schedule->repeat_weekdays & weekday_bit) != 0U;
}

static void clear_active_occurrence(alarm_scheduler_t *scheduler)
{
    scheduler->phase_started_ms = 0U;
    scheduler->snooze_available = false;
}

static bool scheduled_occurrence_is_new(
    const alarm_scheduler_t *scheduler,
    const alarm_schedule_t *schedule,
    const alarm_clock_observation_t *clock)
{
    if (!scheduler->has_last_fired_date ||
        scheduler->last_fired_revision != schedule->revision) {
        return true;
    }
    if (clock->date_key > scheduler->last_fired_date_key) {
        return true;
    }
    if (clock->date_key == scheduler->last_fired_date_key) {
        return false;
    }

    uint32_t current_day = 0U;
    uint32_t last_fired_day = 0U;
    if (!date_key_to_day_index(clock->date_key, &current_day) ||
        !date_key_to_day_index(scheduler->last_fired_date_key,
                               &last_fired_day)) {
        return true;
    }
    return last_fired_day - current_day > ALARM_ROLLBACK_GUARD_DAYS;
}

alarm_scheduler_result_t alarm_scheduler_update(
    alarm_scheduler_t *scheduler,
    const alarm_schedule_t *schedule,
    const alarm_clock_observation_t *clock,
    uint32_t monotonic_ms,
    alarm_scheduler_input_t input)
{
    if (scheduler == NULL) {
        return result_for(NULL, ALARM_SCHEDULER_OUTPUT_NONE);
    }

    const bool schedule_valid = alarm_schedule_is_valid(schedule);
    if (!schedule_valid || !schedule->enabled) {
        const alarm_scheduler_output_t output =
            scheduler->state == ALARM_SCHEDULER_RINGING
                ? ALARM_SCHEDULER_OUTPUT_STOP_RINGING
                : ALARM_SCHEDULER_OUTPUT_NONE;
        scheduler->initialized = schedule_valid;
        if (schedule_valid) {
            scheduler->config_revision = schedule->revision;
        }
        scheduler->state = ALARM_SCHEDULER_DISABLED;
        clear_active_occurrence(scheduler);
        return result_for(scheduler, output);
    }

    if (!scheduler->initialized) {
        scheduler->initialized = true;
        scheduler->config_revision = schedule->revision;
        scheduler->state = ALARM_SCHEDULER_ARMED;
    } else if (scheduler->config_revision != schedule->revision) {
        const alarm_scheduler_output_t output =
            scheduler->state == ALARM_SCHEDULER_RINGING
                ? ALARM_SCHEDULER_OUTPUT_STOP_RINGING
                : ALARM_SCHEDULER_OUTPUT_NONE;
        scheduler->config_revision = schedule->revision;
        scheduler->state = ALARM_SCHEDULER_ARMED;
        scheduler->has_last_fired_date = false;
        clear_active_occurrence(scheduler);
        return result_for(scheduler, output);
    } else if (scheduler->state == ALARM_SCHEDULER_DISABLED) {
        scheduler->state = ALARM_SCHEDULER_ARMED;
    }

    if (input == ALARM_SCHEDULER_INPUT_STOP &&
        (scheduler->state == ALARM_SCHEDULER_RINGING ||
         scheduler->state == ALARM_SCHEDULER_SNOOZED)) {
        const alarm_scheduler_output_t output =
            scheduler->state == ALARM_SCHEDULER_RINGING
                ? ALARM_SCHEDULER_OUTPUT_STOP_RINGING
                : ALARM_SCHEDULER_OUTPUT_NONE;
        scheduler->state = ALARM_SCHEDULER_ARMED;
        clear_active_occurrence(scheduler);
        return result_for(scheduler, output);
    }

    if (input == ALARM_SCHEDULER_INPUT_SNOOZE &&
        scheduler->state == ALARM_SCHEDULER_RINGING &&
        scheduler->snooze_available) {
        scheduler->state = ALARM_SCHEDULER_SNOOZED;
        scheduler->phase_started_ms = monotonic_ms;
        scheduler->snooze_available = false;
        return result_for(scheduler, ALARM_SCHEDULER_OUTPUT_STOP_RINGING);
    }

    if (scheduler->state == ALARM_SCHEDULER_RINGING) {
        if (monotonic_ms - scheduler->phase_started_ms >=
            ALARM_SCHEDULER_RING_TIMEOUT_MS) {
            scheduler->state = ALARM_SCHEDULER_ARMED;
            clear_active_occurrence(scheduler);
            return result_for(scheduler,
                              ALARM_SCHEDULER_OUTPUT_STOP_RINGING);
        }
        return result_for(scheduler, ALARM_SCHEDULER_OUTPUT_NONE);
    }

    if (scheduler->state == ALARM_SCHEDULER_SNOOZED) {
        if (monotonic_ms - scheduler->phase_started_ms >=
            ALARM_SCHEDULER_SNOOZE_MS) {
            scheduler->state = ALARM_SCHEDULER_RINGING;
            scheduler->phase_started_ms = monotonic_ms;
            scheduler->snooze_available = false;
            return result_for(scheduler,
                              ALARM_SCHEDULER_OUTPUT_START_RINGING);
        }
        return result_for(scheduler, ALARM_SCHEDULER_OUTPUT_NONE);
    }

    if (clock_is_valid(clock) && schedule_matches(schedule, clock) &&
        scheduled_occurrence_is_new(scheduler, schedule, clock)) {
        scheduler->has_last_fired_date = true;
        scheduler->last_fired_date_key = clock->date_key;
        scheduler->last_fired_revision = schedule->revision;
        scheduler->state = ALARM_SCHEDULER_RINGING;
        scheduler->phase_started_ms = monotonic_ms;
        scheduler->snooze_available = true;
        return result_for(scheduler,
                          ALARM_SCHEDULER_OUTPUT_START_RINGING);
    }

    return result_for(scheduler, ALARM_SCHEDULER_OUTPUT_NONE);
}
