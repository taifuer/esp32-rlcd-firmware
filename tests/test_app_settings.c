#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings_model.h"
#include "settings_power_policy.h"

static void test_defaults_and_validation(void)
{
    app_settings_t settings;
    app_settings_defaults(&settings);
    assert(settings.schema_version == APP_SETTINGS_SCHEMA_VERSION);
    assert(settings.power_mode == APP_POWER_MODE_NORMAL);
    assert(settings.utc_offset_minutes == 480);
    assert(settings.temperature_unit == APP_TEMPERATURE_UNIT_CELSIUS);
    assert(settings.audio_playback_volume == 68U);
    assert(settings.update_channel == APP_UPDATE_CHANNEL_STABLE);
    assert(!settings.alarm_enabled);
    assert(settings.alarm_hour == 7U);
    assert(settings.alarm_minute == 30U);
    assert(settings.alarm_weekdays == APP_SETTINGS_ALARM_WEEKDAYS_MASK);
    assert(app_settings_validate(&settings));

    settings.schema_version++;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.power_mode = (app_power_mode_t)2;
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

static void test_power_policy(void)
{
    app_power_policy_t policy;
    assert(app_power_policy_for_mode(APP_POWER_MODE_NORMAL, &policy));
    assert(policy.show_seconds);
    assert(policy.automatic_network);
    assert(policy.rtc_read_interval_ms == 1000U);
    assert(policy.sensor_read_interval_ms == 5000U);
    assert(policy.battery_read_interval_ms == 30000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 0U) == 1000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 45U) == 1000U);

    assert(app_power_policy_for_mode(APP_POWER_MODE_SAVING, &policy));
    assert(!policy.show_seconds);
    assert(!policy.automatic_network);
    assert(policy.rtc_read_interval_ms == 60000U);
    assert(policy.sensor_read_interval_ms == 60000U);
    assert(policy.battery_read_interval_ms == 300000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 0U) == 60000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 45U) == 15000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 59U) == 1000U);
    assert(app_power_policy_next_clock_delay_ms(&policy, 60U) == 0U);
    assert(app_power_policy_next_clock_delay_ms(NULL, 0U) == 0U);
    assert(!app_power_policy_for_mode((app_power_mode_t)9, &policy));
    assert(!app_power_policy_for_mode(APP_POWER_MODE_NORMAL, NULL));
}

static void assert_form(const char *form, app_power_mode_t power,
                        int16_t offset, app_temperature_unit_t unit,
                        uint8_t volume, app_update_channel_t updates,
                        bool alarm_enabled, uint8_t alarm_hour,
                        uint8_t alarm_minute, uint8_t alarm_weekdays)
{
    app_settings_t settings = {0};
    assert(app_settings_parse_form(form, strlen(form), &settings));
    assert(settings.schema_version == APP_SETTINGS_SCHEMA_VERSION);
    assert(settings.power_mode == power);
    assert(settings.utc_offset_minutes == offset);
    assert(settings.temperature_unit == unit);
    assert(settings.audio_playback_volume == volume);
    assert(settings.update_channel == updates);
    assert(settings.alarm_enabled == alarm_enabled);
    assert(settings.alarm_hour == alarm_hour);
    assert(settings.alarm_minute == alarm_minute);
    assert(settings.alarm_weekdays == alarm_weekdays);
}

static void test_form_parser(void)
{
    assert_form("power=normal&timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=62",
                APP_POWER_MODE_NORMAL, 480,
                APP_TEMPERATURE_UNIT_CELSIUS, 75U,
                APP_UPDATE_CHANNEL_STABLE, true, 7U, 30U,
                APP_SETTINGS_ALARM_WEEKDAYS_MASK);
    assert_form("updates=beta&volume=0&unit=f&timezone=-300&power=saving&alarm=off&alarm_hour=0&alarm_minute=0&alarm_days=127",
                APP_POWER_MODE_SAVING, -300,
                APP_TEMPERATURE_UNIT_FAHRENHEIT, 0U,
                APP_UPDATE_CHANNEL_BETA, false, 0U, 0U,
                APP_SETTINGS_ALARM_ALL_DAYS_MASK);
    assert_form("power=%73aving&timezone=%2B330&unit=f&volume=100&updates=%62eta&alarm=%6fn&alarm_hour=23&alarm_minute=59&alarm_days=65",
                APP_POWER_MODE_SAVING, 330,
                APP_TEMPERATURE_UNIT_FAHRENHEIT, 100U,
                APP_UPDATE_CHANNEL_BETA, true, 23U, 59U,
                APP_SETTINGS_ALARM_WEEKENDS_MASK);

    const char *invalid[] = {
        "",
        "power=normal&timezone=480&unit=c&volume=75",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&extra=1",
        "power=normal&power=saving&timezone=480&unit=c&volume=75&updates=stable",
        "power=eco&timezone=480&unit=c&volume=75&updates=stable",
        "power=normal&timezone=481&unit=c&volume=75&updates=stable",
        "power=normal&timezone=900&unit=c&volume=75&updates=stable",
        "power=normal&timezone=+480&unit=c&volume=75&updates=stable",
        "power=normal&timezone=480&unit=k&volume=75&updates=stable",
        "power=normal&timezone=480&unit=c&volume=101&updates=stable",
        "power=normal&timezone=480&unit=c&volume=-1&updates=stable",
        "power=normal&timezone=480&unit=c&volume=75&updates=nightly",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&updates=beta",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&",
        "&power=normal&timezone=480&unit=c&volume=75&updates=stable",
        "power=normal&&timezone=480&unit=c&volume=75&updates=stable",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable=",
        "power=%GG&timezone=480&unit=c&volume=75&updates=stable",
        "power=%00normal&timezone=480&unit=c&volume=75&updates=stable",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&alarm=maybe&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=24&alarm_minute=30&alarm_days=62",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=60&alarm_days=62",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=0",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=128",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=62&alarm_days=62",
        "power=normal&timezone=480&unit=c&volume=75&updates=stable&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=62&images=fixed",
    };
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        app_settings_t settings = {0};
        assert(!app_settings_parse_form(invalid[index],
                                        strlen(invalid[index]), &settings));
    }

    char oversized[APP_SETTINGS_FORM_MAX_LENGTH + 1U];
    memset(oversized, 'a', sizeof(oversized));
    app_settings_t settings = {0};
    assert(!app_settings_parse_form(oversized, sizeof(oversized),
                                    &settings));
    assert(!app_settings_parse_form(NULL, 1U, &settings));
    assert(!app_settings_parse_form("x", 1U, NULL));
}

int main(void)
{
    test_defaults_and_validation();
    test_timezone_format();
    test_power_policy();
    test_form_parser();
    puts("app settings tests passed");
    return 0;
}
