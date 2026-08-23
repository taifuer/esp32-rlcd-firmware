#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ALARM_SCHEDULER_SNOOZE_MS = 5U * 60U * 1000U,
    ALARM_SCHEDULER_RING_TIMEOUT_MS = 60U * 1000U,
    ALARM_WEEKDAY_SUNDAY = 1U << 0U,
    ALARM_WEEKDAY_MONDAY = 1U << 1U,
    ALARM_WEEKDAY_TUESDAY = 1U << 2U,
    ALARM_WEEKDAY_WEDNESDAY = 1U << 3U,
    ALARM_WEEKDAY_THURSDAY = 1U << 4U,
    ALARM_WEEKDAY_FRIDAY = 1U << 5U,
    ALARM_WEEKDAY_SATURDAY = 1U << 6U,
    ALARM_WEEKDAY_ALL = 0x7fU,
};

typedef enum {
    ALARM_SCHEDULER_DISABLED = 0,
    ALARM_SCHEDULER_ARMED,
    ALARM_SCHEDULER_RINGING,
    ALARM_SCHEDULER_SNOOZED,
} alarm_scheduler_state_t;

typedef enum {
    ALARM_SCHEDULER_INPUT_NONE = 0,
    ALARM_SCHEDULER_INPUT_STOP,
    ALARM_SCHEDULER_INPUT_SNOOZE,
} alarm_scheduler_input_t;

typedef enum {
    ALARM_SCHEDULER_OUTPUT_NONE = 0,
    ALARM_SCHEDULER_OUTPUT_START_RINGING,
    ALARM_SCHEDULER_OUTPUT_STOP_RINGING,
} alarm_scheduler_output_t;

/* Weekday bits use the PCF85063/struct tm convention: 0 is Sunday. */
typedef struct {
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    uint8_t repeat_weekdays;
    /* Increment whenever any alarm configuration field changes. */
    uint32_t revision;
} alarm_schedule_t;

/* date_key must change whenever the local civil date changes. YYYYMMDD works. */
typedef struct {
    bool valid;
    uint32_t date_key;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
} alarm_clock_observation_t;

typedef struct {
    alarm_scheduler_state_t state;
    alarm_scheduler_output_t output;
    bool snooze_available;
} alarm_scheduler_result_t;

typedef struct {
    bool initialized;
    uint32_t config_revision;
    alarm_scheduler_state_t state;
    bool has_last_fired_date;
    uint32_t last_fired_date_key;
    uint32_t last_fired_revision;
    uint32_t phase_started_ms;
    bool snooze_available;
} alarm_scheduler_t;

void alarm_scheduler_init(alarm_scheduler_t *scheduler);
bool alarm_scheduler_restore_last_fired(alarm_scheduler_t *scheduler,
                                        uint32_t date_key,
                                        uint32_t schedule_revision);
bool alarm_schedule_is_valid(const alarm_schedule_t *schedule);

/*
 * Evaluates one observation. Scheduled occurrences are never caught up after
 * a skipped minute or an invalid clock. For one schedule revision, a short
 * civil-date rollback is suppressed so it cannot repeat an alarm; an
 * implausibly distant future history value is ignored to avoid disabling the
 * rule indefinitely. Active ringing and snooze timers use monotonic_ms and
 * therefore continue to work across uint32_t wraparound.
 */
alarm_scheduler_result_t alarm_scheduler_update(
    alarm_scheduler_t *scheduler,
    const alarm_schedule_t *schedule,
    const alarm_clock_observation_t *clock,
    uint32_t monotonic_ms,
    alarm_scheduler_input_t input);

#ifdef __cplusplus
}
#endif
