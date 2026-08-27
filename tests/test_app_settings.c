#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings_model.h"
#include "settings_power_policy.h"

static void test_defaults_and_validation(void)
{
    app_settings_t settings;
    app_settings_defaults(&settings);
    assert(APP_SETTINGS_SCHEMA_VERSION == 6U);
    assert(settings.schema_version == APP_SETTINGS_SCHEMA_VERSION);
    assert(!settings.manual_saving_requested);
    assert(settings.utc_offset_minutes == 480);
    assert(settings.temperature_unit == APP_TEMPERATURE_UNIT_CELSIUS);
    assert(settings.audio_playback_volume == 68U);
    assert(settings.update_channel == APP_UPDATE_CHANNEL_STABLE);
    assert(!settings.alarm_enabled);
    assert(settings.alarm_hour == 7U);
    assert(settings.alarm_minute == 30U);
    assert(settings.alarm_weekdays == APP_SETTINGS_ALARM_WEEKDAYS_MASK);
    assert(app_settings_validate(&settings));

    settings.manual_saving_requested = true;
    assert(app_settings_validate(&settings));
    settings.schema_version++;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.utc_offset_minutes = 481;
    assert(!app_settings_validate(&settings));
    settings.utc_offset_minutes = -720;
    assert(app_settings_validate(&settings));
    settings.utc_offset_minutes = 840;
    assert(app_settings_validate(&settings));
    settings.utc_offset_minutes = 855;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.temperature_unit = (app_temperature_unit_t)2;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.audio_playback_volume = 101U;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.update_channel = (app_update_channel_t)2;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.alarm_hour = 24U;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.alarm_minute = 60U;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.alarm_weekdays = 0U;
    assert(!app_settings_validate(&settings));
    settings.alarm_weekdays = 0x80U;
    assert(!app_settings_validate(&settings));
    assert(!app_settings_validate(NULL));
    app_settings_defaults(NULL);
}

static void test_legacy_power_mapping(void)
{
    for (uint16_t schema = 1U; schema <= 4U; ++schema) {
        bool manual = true;
        assert(app_manual_saving_from_legacy_power(schema, 0U,
                                                   &manual));
        assert(!manual);
        assert(app_manual_saving_from_legacy_power(schema, 1U,
                                                   &manual));
        assert(manual);
        assert(!app_manual_saving_from_legacy_power(schema, 2U,
                                                    &manual));
        assert(manual);
    }

    bool manual = true;
    assert(app_manual_saving_from_legacy_power(5U, 0U, &manual));
    assert(!manual);
    assert(app_manual_saving_from_legacy_power(5U, 1U, &manual));
    assert(manual);
    assert(app_manual_saving_from_legacy_power(5U, 2U, &manual));
    assert(!manual);
    assert(!app_manual_saving_from_legacy_power(5U, 3U, &manual));
    assert(!manual);
    assert(!app_manual_saving_from_legacy_power(0U, 0U, &manual));
    assert(!app_manual_saving_from_legacy_power(6U, 0U, &manual));
    assert(!app_manual_saving_from_legacy_power(5U, 0U, NULL));
}

static void test_timezone_format(void)
{
    char timezone[APP_SETTINGS_POSIX_TZ_CAPACITY];
    assert(app_settings_format_posix_tz(480, timezone, sizeof(timezone)));
    assert(strcmp(timezone, "UTC-8") == 0);
    assert(app_settings_format_posix_tz(-300, timezone, sizeof(timezone)));
    assert(strcmp(timezone, "UTC+5") == 0);
    assert(app_settings_format_posix_tz(330, timezone, sizeof(timezone)));
    assert(strcmp(timezone, "UTC-5:30") == 0);
    assert(app_settings_format_posix_tz(-210, timezone, sizeof(timezone)));
    assert(strcmp(timezone, "UTC+3:30") == 0);
    assert(app_settings_format_posix_tz(0, timezone, sizeof(timezone)));
    assert(strcmp(timezone, "UTC0") == 0);
    assert(app_settings_format_posix_tz(840, timezone, sizeof(timezone)));
    assert(strcmp(timezone, "UTC-14") == 0);
    assert(!app_settings_format_posix_tz(481, timezone, sizeof(timezone)));
    assert(!app_settings_format_posix_tz(480, timezone, 5U));
    assert(!app_settings_format_posix_tz(480, NULL, sizeof(timezone)));
}

static void test_power_policy_profiles(void)
{
    app_power_runtime_t runtime;
    app_power_policy_t policy;
    assert(app_power_runtime_init(&runtime, false));
    assert(!runtime.manual_saving_requested);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);
    assert(app_power_policy_for_runtime(&runtime, &policy));
    assert(policy.show_seconds);
    assert(policy.automatic_network);
    assert(policy.rtc_read_interval_ms == 1000U);
    assert(policy.sensor_read_interval_ms == 5000U);
    assert(policy.battery_read_interval_ms == 30000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 0U) == 1000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 45U) == 1000U);

    assert(app_power_runtime_init(&runtime, true));
    assert(runtime.manual_saving_requested);
    assert(runtime.effective_state == APP_POWER_STATE_SAVING);
    assert(app_power_policy_for_runtime(&runtime, &policy));
    assert(!policy.show_seconds);
    assert(!policy.automatic_network);
    assert(policy.rtc_read_interval_ms == 60000U);
    assert(policy.sensor_read_interval_ms == 60000U);
    assert(policy.battery_read_interval_ms == 10000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 0U) == 60000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 45U) == 15000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 59U) == 1000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 60U) == 0U);
    assert(app_power_policy_next_clock_delay_ms(NULL, 0U) == 0U);

    assert(!app_power_runtime_init(NULL, false));
    assert(!app_power_policy_for_runtime(NULL, &policy));
    assert(!app_power_policy_for_runtime(&runtime, NULL));
    runtime.effective_state = (app_power_state_t)9;
    assert(!app_power_policy_for_runtime(&runtime, &policy));
}

static void test_automatic_battery_policy(void)
{
    app_power_runtime_t runtime;
    bool changed = false;

    assert(app_power_runtime_init(&runtime, false));
    assert(app_power_runtime_observe_battery(&runtime, false, 0U,
                                             &changed));
    assert(!changed);
    assert(!runtime.battery_observed);
    assert(app_power_runtime_observe_battery(&runtime, true, 21U,
                                             &changed));
    assert(!changed);
    assert(runtime.battery_observed);
    assert(!runtime.automatic_saving_active);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);

    assert(app_power_runtime_observe_battery(&runtime, true, 20U,
                                             &changed));
    assert(!changed);
    assert(runtime.pending_samples == 1U);
    assert(app_power_runtime_observe_battery(&runtime, true, 21U,
                                             &changed));
    assert(!changed);
    assert(runtime.pending_samples == 0U);
    assert(app_power_runtime_observe_battery(&runtime, true, 20U,
                                             &changed));
    assert(!changed);
    assert(app_power_runtime_observe_battery(&runtime, true, 19U,
                                             &changed));
    assert(changed);
    assert(runtime.automatic_saving_active);
    assert(runtime.effective_state == APP_POWER_STATE_SAVING);

    assert(app_power_runtime_observe_battery(&runtime, true, 24U,
                                             &changed));
    assert(!changed);
    assert(runtime.pending_samples == 0U);
    assert(app_power_runtime_observe_battery(&runtime, true, 25U,
                                             &changed));
    assert(!changed);
    assert(runtime.pending_samples == 1U);
    assert(app_power_runtime_observe_battery(&runtime, false, 0U,
                                             &changed));
    assert(!changed);
    assert(runtime.pending_samples == 0U);
    assert(app_power_runtime_observe_battery(&runtime, true, 25U,
                                             &changed));
    assert(!changed);
    assert(app_power_runtime_observe_battery(&runtime, true, 26U,
                                             &changed));
    assert(changed);
    assert(!runtime.automatic_saving_active);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);

    assert(app_power_runtime_init(&runtime, false));
    assert(app_power_runtime_observe_battery(&runtime, true, 20U,
                                             &changed));
    assert(changed);
    assert(runtime.automatic_saving_active);
    assert(runtime.effective_state == APP_POWER_STATE_SAVING);

    assert(!app_power_runtime_observe_battery(NULL, true, 20U, &changed));
    assert(!app_power_runtime_observe_battery(&runtime, true, 101U,
                                              &changed));
    assert(!app_power_runtime_observe_battery(&runtime, true, 20U, NULL));
    runtime.effective_state = (app_power_state_t)9;
    assert(!app_power_runtime_observe_battery(&runtime, true, 20U,
                                              &changed));
}

static void test_manual_request_and_automatic_latch(void)
{
    app_power_runtime_t runtime;
    bool changed = false;

    assert(app_power_runtime_init(&runtime, false));
    assert(app_power_runtime_observe_battery(&runtime, true, 80U,
                                             &changed));
    assert(!changed);
    assert(app_power_runtime_set_manual_saving_requested(
        &runtime, true, &changed));
    assert(changed);
    assert(runtime.effective_state == APP_POWER_STATE_SAVING);

    /* Automatic observations continue while the manual request masks their
     * effective result. */
    assert(app_power_runtime_observe_battery(&runtime, true, 20U,
                                             &changed));
    assert(!changed);
    assert(app_power_runtime_observe_battery(&runtime, true, 20U,
                                             &changed));
    assert(!changed);
    assert(runtime.automatic_saving_active);
    assert(app_power_runtime_set_manual_saving_requested(
        &runtime, false, &changed));
    assert(!changed);
    assert(runtime.effective_state == APP_POWER_STATE_SAVING);

    assert(app_power_runtime_observe_battery(&runtime, true, 25U,
                                             &changed));
    assert(!changed);
    assert(app_power_runtime_observe_battery(&runtime, true, 25U,
                                             &changed));
    assert(changed);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);

    assert(app_power_runtime_set_manual_saving_requested(
        &runtime, true, &changed));
    assert(changed);
    assert(app_power_runtime_observe_battery(&runtime, true, 90U,
                                             &changed));
    assert(!changed);
    assert(app_power_runtime_set_manual_saving_requested(
        &runtime, false, &changed));
    assert(changed);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);
    assert(app_power_runtime_set_manual_saving_requested(
        &runtime, false, &changed));
    assert(!changed);

    assert(!app_power_runtime_set_manual_saving_requested(
        NULL, true, &changed));
    assert(!app_power_runtime_set_manual_saving_requested(
        &runtime, true, NULL));
    runtime.effective_state = (app_power_state_t)9;
    assert(!app_power_runtime_set_manual_saving_requested(
        &runtime, true, &changed));
}

static void test_usb_data_host_override(void)
{
    app_power_runtime_t runtime;
    app_power_policy_t policy;
    bool changed = false;

    assert(app_power_runtime_init(&runtime, false));
    assert(app_power_runtime_observe_battery(&runtime, true, 20U,
                                             &changed));
    assert(changed);
    assert(runtime.automatic_saving_active);
    assert(runtime.effective_state == APP_POWER_STATE_SAVING);

    assert(app_power_runtime_observe_usb_data_host(&runtime, true,
                                                   &changed));
    assert(changed);
    assert(runtime.usb_data_host_connected);
    assert(!runtime.automatic_saving_active);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);
    assert(app_power_policy_for_runtime(&runtime, &policy));
    assert(policy.automatic_network);

    /* A manual request is persisted but cannot defeat the USB override. */
    assert(app_power_runtime_set_manual_saving_requested(
        &runtime, true, &changed));
    assert(!changed);
    assert(runtime.manual_saving_requested);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);
    assert(app_power_runtime_observe_battery(&runtime, true, 10U,
                                             &changed));
    assert(!changed);
    assert(!runtime.automatic_saving_active);

    assert(app_power_runtime_observe_usb_data_host(&runtime, false,
                                                   &changed));
    assert(changed);
    assert(runtime.effective_state == APP_POWER_STATE_SAVING);
    assert(app_power_runtime_set_manual_saving_requested(
        &runtime, false, &changed));
    assert(changed);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);

    /* The pre-USB low latch is not reused. Two fresh low samples are needed
     * after the host disappears. */
    assert(app_power_runtime_observe_battery(&runtime, true, 20U,
                                             &changed));
    assert(!changed);
    assert(runtime.pending_samples == 1U);
    assert(app_power_runtime_observe_battery(&runtime, true, 19U,
                                             &changed));
    assert(changed);
    assert(runtime.effective_state == APP_POWER_STATE_SAVING);

    assert(app_power_runtime_observe_usb_data_host(&runtime, true,
                                                   &changed));
    assert(changed);
    assert(app_power_runtime_observe_usb_data_host(&runtime, true,
                                                   &changed));
    assert(!changed);
    assert(app_power_runtime_observe_usb_data_host(&runtime, false,
                                                   &changed));
    assert(!changed);
    assert(runtime.effective_state == APP_POWER_STATE_NORMAL);

    assert(!app_power_runtime_observe_usb_data_host(NULL, true, &changed));
    assert(!app_power_runtime_observe_usb_data_host(&runtime, true, NULL));
    runtime.effective_state = (app_power_state_t)9;
    assert(!app_power_runtime_observe_usb_data_host(&runtime, true,
                                                    &changed));
}

static void assert_form(const char *form, bool manual_saving,
                        int16_t offset, app_temperature_unit_t unit,
                        uint8_t volume, app_update_channel_t updates,
                        bool alarm_enabled, uint8_t alarm_hour,
                        uint8_t alarm_minute, uint8_t alarm_weekdays)
{
    app_settings_t base;
    app_settings_defaults(&base);
    base.manual_saving_requested = manual_saving;
    app_settings_t settings = {0};
    assert(app_settings_parse_form(form, strlen(form), &base, &settings));
    assert(settings.schema_version == APP_SETTINGS_SCHEMA_VERSION);
    assert(settings.manual_saving_requested == manual_saving);
    assert(settings.utc_offset_minutes == offset);
    assert(settings.temperature_unit == unit);
    assert(settings.audio_playback_volume == volume);
    assert(settings.update_channel == updates);
    assert(settings.alarm_enabled == alarm_enabled);
    assert(settings.alarm_hour == alarm_hour);
    assert(settings.alarm_minute == alarm_minute);
    assert(settings.alarm_weekdays == alarm_weekdays);
}

static void test_form_parser_preserves_device_setting(void)
{
    assert_form("timezone=480&unit=c&volume=68&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
                true, 480, APP_TEMPERATURE_UNIT_CELSIUS, 68U,
                APP_UPDATE_CHANNEL_STABLE, false, 7U, 30U,
                APP_SETTINGS_ALARM_WEEKDAYS_MASK);
    assert_form("updates=beta&volume=0&unit=f&timezone=-300&alarm=off&alarm_hour=0&alarm_minute=0&alarm_days=127",
                false, -300, APP_TEMPERATURE_UNIT_FAHRENHEIT, 0U,
                APP_UPDATE_CHANNEL_BETA, false, 0U, 0U,
                APP_SETTINGS_ALARM_ALL_DAYS_MASK);
    assert_form("timezone=%2B330&unit=f&volume=100&updates=%62eta&alarm=%6fn&alarm_hour=23&alarm_minute=59&alarm_days=65",
                true, 330, APP_TEMPERATURE_UNIT_FAHRENHEIT, 100U,
                APP_UPDATE_CHANNEL_BETA, true, 23U, 59U,
                APP_SETTINGS_ALARM_WEEKENDS_MASK);

    const char *invalid[] = {
        "",
        "timezone=480&unit=c&volume=75",
        "timezone=480&unit=c&volume=75&updates=stable&extra=1",
        "power=auto&timezone=480&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&timezone=480&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=481&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=900&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=+480&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=k&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=101&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=-1&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=75&updates=nightly&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=75&updates=stable&updates=beta&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&",
        "&timezone=480&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=75&updates=stable=&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=%GG&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=%00480&unit=c&volume=75&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=75&updates=stable&alarm=maybe&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=24&alarm_minute=30&alarm_days=62",
        "timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=60&alarm_days=62",
        "timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=0",
        "timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=128",
        "timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=62&alarm_days=62",
    };
    app_settings_t base;
    app_settings_defaults(&base);
    base.manual_saving_requested = true;
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        app_settings_t settings = {.manual_saving_requested = false};
        assert(!app_settings_parse_form(invalid[index],
                                        strlen(invalid[index]),
                                        &base, &settings));
        assert(!settings.manual_saving_requested);
    }

    char oversized[APP_SETTINGS_FORM_MAX_LENGTH + 1U];
    memset(oversized, 'a', sizeof(oversized));
    app_settings_t settings = {0};
    assert(!app_settings_parse_form(oversized, sizeof(oversized),
                                    &base, &settings));
    assert(!app_settings_parse_form(NULL, 1U, &base, &settings));
    assert(!app_settings_parse_form("x", 1U, NULL, &settings));
    assert(!app_settings_parse_form("x", 1U, &base, NULL));
    base.schema_version++;
    assert(!app_settings_parse_form("x", 1U, &base, &settings));
}

int main(void)
{
    test_defaults_and_validation();
    test_legacy_power_mapping();
    test_timezone_format();
    test_power_policy_profiles();
    test_automatic_battery_policy();
    test_manual_request_and_automatic_latch();
    test_usb_data_host_override();
    test_form_parser_preserves_device_setting();
    puts("settings model and power policy tests passed");
    return 0;
}
