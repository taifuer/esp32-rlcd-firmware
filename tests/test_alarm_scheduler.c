#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "alarm_scheduler.h"

static alarm_schedule_t schedule_at(uint8_t hour, uint8_t minute,
                                    uint8_t weekdays, uint32_t revision)
{
    return (alarm_schedule_t){
        .enabled = true,
        .hour = hour,
        .minute = minute,
        .repeat_weekdays = weekdays,
        .revision = revision,
    };
}

static alarm_clock_observation_t clock_at(bool valid, uint32_t date_key,
                                          uint8_t weekday, uint8_t hour,
                                          uint8_t minute)
{
    return (alarm_clock_observation_t){
        .valid = valid,
        .date_key = date_key,
        .weekday = weekday,
        .hour = hour,
        .minute = minute,
    };
}

static alarm_scheduler_result_t update(
    alarm_scheduler_t *scheduler, const alarm_schedule_t *schedule,
    alarm_clock_observation_t clock, uint32_t now_ms,
    alarm_scheduler_input_t input)
{
    return alarm_scheduler_update(scheduler, schedule, &clock, now_ms,
                                  input);
}

static void assert_state(alarm_scheduler_result_t result,
                         alarm_scheduler_state_t state,
                         alarm_scheduler_output_t output,
                         bool snooze_available)
{
    assert(result.state == state);
    assert(result.output == output);
    assert(result.snooze_available == snooze_available);
}

static void test_schedule_validation(void)
{
    alarm_schedule_t schedule =
        schedule_at(7U, 30U, ALARM_WEEKDAY_ALL, 1U);
    assert(alarm_schedule_is_valid(&schedule));
    schedule.enabled = false;
    schedule.repeat_weekdays = 0U;
    assert(alarm_schedule_is_valid(&schedule));
    schedule.enabled = true;
    assert(!alarm_schedule_is_valid(&schedule));
    schedule.repeat_weekdays = ALARM_WEEKDAY_ALL;
    schedule.hour = 24U;
    assert(!alarm_schedule_is_valid(&schedule));
    schedule.hour = 7U;
    schedule.minute = 60U;
    assert(!alarm_schedule_is_valid(&schedule));
    schedule.minute = 30U;
    schedule.repeat_weekdays = 0x80U;
    assert(!alarm_schedule_is_valid(&schedule));
    assert(!alarm_schedule_is_valid(NULL));
}

static void test_weekday_mask_and_minute_deduplication(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule = schedule_at(
        7U, 30U,
        ALARM_WEEKDAY_MONDAY | ALARM_WEEKDAY_WEDNESDAY, 1U);

    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260823U, 0U, 7U, 30U), 0U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 100U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 200U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING, ALARM_SCHEDULER_OUTPUT_NONE,
                 true);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 300U,
                        ALARM_SCHEDULER_INPUT_STOP),
                 ALARM_SCHEDULER_ARMED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 400U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260825U, 2U, 7U, 30U), 500U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260826U, 3U, 7U, 30U), 600U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
}

static void test_stop_and_ring_timeout(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule =
        schedule_at(6U, 45U, ALARM_WEEKDAY_ALL, 2U);
    const alarm_clock_observation_t due =
        clock_at(true, 20260824U, 1U, 6U, 45U);

    assert_state(update(&scheduler, &schedule, due, 1000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &schedule, due, 60999U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING, ALARM_SCHEDULER_OUTPUT_NONE,
                 true);
    assert_state(update(&scheduler, &schedule, due, 61000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);
    assert_state(update(&scheduler, &schedule, due, 62000U,
                        ALARM_SCHEDULER_INPUT_STOP),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
}

static void test_five_minute_one_shot_snooze(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule =
        schedule_at(7U, 0U, ALARM_WEEKDAY_ALL, 3U);
    const alarm_clock_observation_t due =
        clock_at(true, 20260824U, 1U, 7U, 0U);
    const alarm_clock_observation_t later =
        clock_at(true, 20260824U, 1U, 7U, 5U);

    assert_state(update(&scheduler, &schedule, due, 1000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &schedule, due, 2000U,
                        ALARM_SCHEDULER_INPUT_SNOOZE),
                 ALARM_SCHEDULER_SNOOZED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);
    assert_state(update(&scheduler, &schedule, later, 301999U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_SNOOZED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule, later, 302000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, false);
    assert_state(update(&scheduler, &schedule, later, 302100U,
                        ALARM_SCHEDULER_INPUT_SNOOZE),
                 ALARM_SCHEDULER_RINGING, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule, later, 302200U,
                        ALARM_SCHEDULER_INPUT_STOP),
                 ALARM_SCHEDULER_ARMED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);
}

static void test_invalid_clock_and_time_jumps(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule =
        schedule_at(7U, 30U, ALARM_WEEKDAY_ALL, 4U);

    assert_state(alarm_scheduler_update(
                     &scheduler, &schedule, NULL, 0U,
                     ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);

    assert_state(update(&scheduler, &schedule,
                        clock_at(false, 20260824U, 1U, 7U, 30U), 0U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260230U, 1U, 7U, 30U), 50U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 29U), 1000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 31U), 2000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 3000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 4000U,
                        ALARM_SCHEDULER_INPUT_STOP),
                 ALARM_SCHEDULER_ARMED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 29U), 5000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 6000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
}

static void test_cross_midnight(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule =
        schedule_at(23U, 58U, ALARM_WEEKDAY_ALL, 5U);

    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 23U, 58U), 100U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 23U, 58U), 200U,
                        ALARM_SCHEDULER_INPUT_SNOOZE),
                 ALARM_SCHEDULER_SNOOZED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260825U, 2U, 0U, 3U),
                        300200U, ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, false);

    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t midnight =
        schedule_at(0U, 0U, ALARM_WEEKDAY_MONDAY, 6U);
    assert_state(update(&scheduler, &midnight,
                        clock_at(true, 20260823U, 0U, 23U, 59U), 0U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &midnight,
                        clock_at(true, 20260824U, 1U, 0U, 0U), 1000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
}

static void test_uint32_wraparound(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule =
        schedule_at(8U, 0U, ALARM_WEEKDAY_ALL, 7U);
    const alarm_clock_observation_t due =
        clock_at(true, 20260824U, 1U, 8U, 0U);
    const uint32_t ring_started_ms = UINT32_MAX - 10000U;

    assert_state(update(&scheduler, &schedule, due, ring_started_ms,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &schedule, due,
                        ring_started_ms + 59999U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING, ALARM_SCHEDULER_OUTPUT_NONE,
                 true);
    assert_state(update(&scheduler, &schedule, due,
                        ring_started_ms + 60000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);

    alarm_scheduler_init(&scheduler);
    const uint32_t snooze_started_ms = UINT32_MAX - 1000U;
    assert_state(update(&scheduler, &schedule, due,
                        snooze_started_ms - 100U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &schedule, due, snooze_started_ms,
                        ALARM_SCHEDULER_INPUT_SNOOZE),
                 ALARM_SCHEDULER_SNOOZED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);
    assert_state(update(&scheduler, &schedule, due,
                        snooze_started_ms +
                            ALARM_SCHEDULER_SNOOZE_MS - 1U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_SNOOZED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule, due,
                        snooze_started_ms +
                            ALARM_SCHEDULER_SNOOZE_MS,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, false);
}

static void test_config_revision_cancels_snooze(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t original =
        schedule_at(7U, 30U, ALARM_WEEKDAY_ALL, 10U);
    const alarm_schedule_t revised =
        schedule_at(8U, 0U, ALARM_WEEKDAY_ALL, 11U);

    assert_state(update(&scheduler, &original,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 1000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &original,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 2000U,
                        ALARM_SCHEDULER_INPUT_SNOOZE),
                 ALARM_SCHEDULER_SNOOZED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);
    assert_state(update(&scheduler, &revised,
                        clock_at(true, 20260824U, 1U, 7U, 31U), 3000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &revised,
                        clock_at(true, 20260824U, 1U, 7U, 36U),
                        302000U, ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &revised,
                        clock_at(true, 20260824U, 1U, 8U, 0U),
                        303000U, ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
}

static void test_persisted_occurrence_survives_restart(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule =
        schedule_at(7U, 30U, ALARM_WEEKDAY_ALL, 21U);
    const alarm_clock_observation_t due =
        clock_at(true, 20260824U, 1U, 7U, 30U);

    assert(alarm_scheduler_restore_last_fired(
        &scheduler, 20260824U, schedule.revision));
    assert_state(update(&scheduler, &schedule, due, 1000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);

    const alarm_schedule_t revised =
        schedule_at(7U, 30U, ALARM_WEEKDAY_ALL, 22U);
    assert_state(update(&scheduler, &revised, due, 2000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &revised, due, 3000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);

    assert(!alarm_scheduler_restore_last_fired(NULL, 20260824U, 1U));
    assert(!alarm_scheduler_restore_last_fired(&scheduler, 0U, 1U));
    assert(!alarm_scheduler_restore_last_fired(
        &scheduler, 20260230U, 1U));
}

static void test_civil_date_rollback_does_not_repeat(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule =
        schedule_at(7U, 30U, ALARM_WEEKDAY_ALL, 31U);

    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260826U, 3U, 7U, 30U), 1000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260826U, 3U, 7U, 30U), 2000U,
                        ALARM_SCHEDULER_INPUT_STOP),
                 ALARM_SCHEDULER_ARMED,
                 ALARM_SCHEDULER_OUTPUT_STOP_RINGING, false);

    /* A corrected-back RTC and a later jump to the fired date stay quiet. */
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 3000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260826U, 3U, 7U, 30U), 4000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_ARMED, ALARM_SCHEDULER_OUTPUT_NONE,
                 false);
    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260827U, 4U, 7U, 30U), 5000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
}

static void test_implausible_future_history_does_not_disable_alarm(void)
{
    alarm_scheduler_t scheduler;
    alarm_scheduler_init(&scheduler);
    const alarm_schedule_t schedule =
        schedule_at(7U, 30U, ALARM_WEEKDAY_ALL, 32U);
    assert(alarm_scheduler_restore_last_fired(
        &scheduler, 20991231U, schedule.revision));

    assert_state(update(&scheduler, &schedule,
                        clock_at(true, 20260824U, 1U, 7U, 30U), 1000U,
                        ALARM_SCHEDULER_INPUT_NONE),
                 ALARM_SCHEDULER_RINGING,
                 ALARM_SCHEDULER_OUTPUT_START_RINGING, true);
}

int main(void)
{
    test_schedule_validation();
    test_weekday_mask_and_minute_deduplication();
    test_stop_and_ring_timeout();
    test_five_minute_one_shot_snooze();
    test_invalid_clock_and_time_jumps();
    test_cross_midnight();
    test_uint32_wraparound();
    test_config_revision_cancels_snooze();
    test_persisted_occurrence_survives_restart();
    test_civil_date_rollback_does_not_repeat();
    test_implausible_future_history_does_not_disable_alarm();
    puts("alarm scheduler tests passed");
    return 0;
}
