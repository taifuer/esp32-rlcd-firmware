#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings_model.h"
#include "settings_power_policy.h"

static void test_defaults_and_validation(void)
{
    app_settings_t settings;
    app_settings_defaults(&settings);
    assert(APP_SETTINGS_SCHEMA_VERSION == 14U);
    assert(APP_DAILY_PAGE_TIMER == 64U && APP_DAILY_PAGES_ALL_MASK == 255U);
    assert(APP_DAILY_PAGE_COUNT == 8U);
    assert(settings.schema_version == APP_SETTINGS_SCHEMA_VERSION);
    assert(!settings.manual_saving_requested);
    assert(!settings.environment_history_save_enabled);
    assert(!settings.environment_archive_save_enabled);
    assert(settings.utc_offset_minutes == 480);
    assert(settings.temperature_unit == APP_TEMPERATURE_UNIT_CELSIUS);
    assert(settings.audio_playback_volume == 68U);
    assert(settings.alarm_volume == 68U);
    assert(settings.update_channel == APP_UPDATE_CHANNEL_STABLE);
    assert(!settings.alarm_enabled);
    assert(settings.alarm_hour == 7U);
    assert(settings.alarm_minute == 30U);
    assert(settings.alarm_weekdays == APP_SETTINGS_ALARM_WEEKDAYS_MASK);
    assert(settings.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
    const uint8_t default_order[] = APP_DAILY_PAGES_DEFAULT_ORDER;
    assert(memcmp(settings.daily_pages_order, default_order, sizeof(default_order)) == 0);
    assert(settings.music_mode == APP_MUSIC_MODE_SEQUENTIAL);
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
    settings.alarm_volume = 101U;
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
    app_settings_defaults(&settings);
    settings.daily_pages_mask = 0U;
    assert(app_settings_validate(&settings));
    settings.daily_pages_mask = 128U;
    assert(app_settings_validate(&settings));
    settings.daily_pages_mask = UINT8_MAX;
    assert(app_settings_validate(&settings));
    app_settings_defaults(&settings);
    settings.music_mode = APP_MUSIC_MODE_REPEAT_ONE;
    assert(app_settings_validate(&settings));
    settings.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    assert(app_settings_validate(&settings));
    settings.music_mode = (app_music_mode_t)-1;
    assert(!app_settings_validate(&settings));
    settings.music_mode = (app_music_mode_t)3;
    assert(!app_settings_validate(&settings));
    app_settings_defaults(NULL);
}

static void test_scoped_forms(void)
{
    app_settings_t base, parsed;
    app_settings_defaults(&base);
    base.manual_saving_requested = true;
    base.update_channel = APP_UPDATE_CHANNEL_BETA;
    base.alarm_enabled = true;
    base.alarm_volume = 19U;
    base.daily_pages_mask = APP_DAILY_PAGE_CALENDAR | APP_DAILY_PAGE_CHAT;
    base.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    const char *general = "section=general&timezone=330&unit=f&volume=42";
    assert(app_settings_parse_form(general, strlen(general), &base, &parsed));
    assert(parsed.utc_offset_minutes == 330 && parsed.audio_playback_volume == 42U);
    assert(parsed.temperature_unit == APP_TEMPERATURE_UNIT_FAHRENHEIT);
    assert(parsed.alarm_enabled && parsed.alarm_volume == 19U);
    assert(parsed.update_channel == APP_UPDATE_CHANNEL_BETA && parsed.manual_saving_requested);
    assert(parsed.daily_pages_mask == base.daily_pages_mask);
    assert(parsed.music_mode == base.music_mode);
    const char *alarm = "alarm=off&alarm_hour=23&alarm_minute=59&alarm_days=65&alarm_volume=0&section=alarm";
    assert(app_settings_parse_form(alarm, strlen(alarm), &base, &parsed));
    assert(!parsed.alarm_enabled && parsed.alarm_hour == 23U && parsed.alarm_minute == 59U);
    assert(parsed.alarm_weekdays == 65U && parsed.alarm_volume == 0U);
    assert(parsed.audio_playback_volume == base.audio_playback_volume);
    assert(parsed.utc_offset_minutes == base.utc_offset_minutes);
    assert(parsed.update_channel == base.update_channel && parsed.manual_saving_requested);
    assert(parsed.daily_pages_mask == base.daily_pages_mask);
    assert(parsed.music_mode == base.music_mode);
    const char *updates = "section=updates&updates=stable";
    assert(app_settings_parse_form(updates, strlen(updates), &base, &parsed));
    assert(parsed.update_channel == APP_UPDATE_CHANNEL_STABLE);
    assert(parsed.alarm_enabled && parsed.alarm_volume == 19U && parsed.manual_saving_requested);
    assert(parsed.daily_pages_mask == base.daily_pages_mask);
    assert(parsed.music_mode == base.music_mode);
    const char *invalid[] = {
        "section=unknown&volume=50", "section=&volume=50",
        "section=general&timezone=480&unit=c", /* required field missing */
        "section=general&timezone=480&unit=c&volume=50&updates=stable",
        "section=updates&updates=beta&volume=50", /* no cross-section writes */
        "section=updates&updates=beta&section=updates",
        "section=updates&updates=beta&section=general",
        "section=updates&updates=beta&updates=stable",
        "section=alarm&alarm=on&alarm_hour=7&alarm_minute=30&alarm_days=62",
        "section=alarm&alarm=on&alarm_hour=24&alarm_minute=30&alarm_days=62&alarm_volume=50",
        "timezone=480&unit=c&volume=50", /* not a complete legacy form */
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        parsed = base;
        assert(!app_settings_parse_form(invalid[i], strlen(invalid[i]), &base, &parsed));
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
}

static void test_daily_page_forms(void)
{
    app_settings_t base, parsed;
    app_settings_defaults(&base);
    base.manual_saving_requested = true;
    base.update_channel = APP_UPDATE_CHANNEL_BETA;
    base.alarm_enabled = true;
    base.alarm_volume = 23U;
    base.daily_pages_mask = APP_DAILY_PAGE_CALENDAR;
    base.music_mode = APP_MUSIC_MODE_REPEAT_ONE;
    for (unsigned mask = 0U; mask <= APP_DAILY_PAGES_ALL_MASK; ++mask) {
        char form[96];
        snprintf(form, sizeof(form), "section=pages&daily_pages_mask=%u&daily_pages_order=1,128,2,4,64,8,16,32", mask);
        assert(app_settings_parse_form(form, strlen(form), &base, &parsed));
        assert(parsed.daily_pages_mask == mask);
        parsed.daily_pages_mask = base.daily_pages_mask;
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
    const char *reordered = "daily_pages_mask=%32%35%35&daily_pages_order=1%2C128%2C2%2C4%2C64%2C8%2C16%2C32&section=pages";
    assert(app_settings_parse_form(reordered, strlen(reordered), &base, &parsed));
    assert(parsed.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
    const char *legacy = "timezone=480&unit=c&volume=50&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62";
    assert(app_settings_parse_form(legacy, strlen(legacy), &base, &parsed));
    assert(parsed.daily_pages_mask == base.daily_pages_mask);
    assert(parsed.music_mode == base.music_mode);
    const char *invalid[] = {
        "section=pages", "daily_pages_mask=63", "section=pages&daily_pages_mask=",
        "section=pages&daily_pages_mask=64", "section=pages&daily_pages_mask=255",
        "section=pages&daily_pages_mask=-1", "section=pages&daily_pages_mask=-0",
        "section=pages&daily_pages_mask=%2B1", "section=pages&daily_pages_mask=+1",
        "section=pages&daily_pages_mask=1+", "section=pages&daily_pages_mask=0x3f",
        "section=pages&daily_pages_mask=1.0", "section=pages&daily_pages_mask=1x",
        "section=pages&daily_pages_mask=2147483648",
        "section=pages&daily_pages_mask=63&daily_pages_mask=0",
        "section=pages&daily_pages_mask=0&%64aily_pages_mask=63",
        "section=pages&daily_pages_mask=63&section=pages",
        "section=pages&daily_pages_mask=63&section=general",
        "section=pages&daily_pages_mask=63&volume=1",
        "section=pages&daily_pages_mask=63&updates=beta",
        "section=pages&daily_pages_mask=63&alarm=on",
        "section=general&timezone=480&unit=c&volume=50&daily_pages_mask=0",
        "section=updates&updates=stable&daily_pages_mask=0",
        "section=alarm&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&alarm_volume=30&daily_pages_mask=0",
        "timezone=480&unit=c&volume=50&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&daily_pages_mask=0",
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        parsed = base;
        assert(!app_settings_parse_form(invalid[i], strlen(invalid[i]), &base, &parsed));
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
}

static void test_music_forms(void)
{
    app_settings_t base, parsed;
    app_settings_defaults(&base);
    base.manual_saving_requested = true;
    base.daily_pages_mask = 0U;
    base.alarm_volume = 23U;
    base.update_channel = APP_UPDATE_CHANNEL_BETA;
    base.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    const char *values[] = {"sequential", "repeat_one", "repeat_all"};
    for (unsigned mode = 0U; mode < 3U; ++mode) {
        char form[64];
        snprintf(form, sizeof(form), "section=music&music_mode=%s", values[mode]);
        assert(app_settings_parse_form(form, strlen(form), &base, &parsed));
        assert(parsed.music_mode == (app_music_mode_t)mode);
        parsed.music_mode = base.music_mode;
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
    const char *reordered = "music_mode=repeat%5fone&section=music";
    assert(app_settings_parse_form(reordered, strlen(reordered), &base, &parsed));
    assert(parsed.music_mode == APP_MUSIC_MODE_REPEAT_ONE);
    const char *invalid[] = {
        "section=music", "music_mode=sequential", "section=music&music_mode=",
        "section=music&music_mode=0", "section=music&music_mode=shuffle",
        "section=music&music_mode=REPEAT_ONE", "section=music&music_mode=repeat_one+",
        "section=music&music_mode=repeat_one&music_mode=repeat_all",
        "section=music&music_mode=repeat_one&%6Dusic_mode=sequential",
        "section=music&music_mode=repeat_all&section=music",
        "section=music&music_mode=repeat_all&section=pages",
        "section=music&music_mode=repeat_all&daily_pages_mask=0",
        "section=music&music_mode=repeat_all&volume=1",
        "section=music&music_mode=repeat_all&updates=stable",
        "section=music&music_mode=repeat_all&alarm_volume=1",
        "section=pages&daily_pages_mask=63&music_mode=repeat_one",
        "section=general&timezone=480&unit=c&volume=50&music_mode=repeat_one",
        "section=updates&updates=stable&music_mode=repeat_one",
        "section=alarm&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&alarm_volume=30&music_mode=repeat_one",
        "timezone=480&unit=c&volume=50&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&music_mode=repeat_one",
    };
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        parsed = base;
        assert(!app_settings_parse_form(invalid[index], strlen(invalid[index]), &base, &parsed));
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
}

static unsigned tested_orders;

static void test_environment_forms(void)
{
    app_settings_t base, parsed;
    app_settings_defaults(&base);
    const char *on = "section=environment&environment_history_save_enabled=on";
    const char *off = "section=environment&environment_history_save_enabled=off";
    assert(app_settings_parse_form(on, strlen(on), &base, &parsed));
    assert(parsed.environment_history_save_enabled);
    parsed.environment_history_save_enabled = false;
    assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    base.environment_history_save_enabled = true;
    assert(app_settings_parse_form(off, strlen(off), &base, &parsed));
    assert(!parsed.environment_history_save_enabled);
    const char *invalid[] = {
        "environment_history_save_enabled=on", "section=environment",
        "section=environment&environment_history_save_enabled=true",
        "section=environment&environment_history_save_enabled=1",
        "section=environment&environment_history_save_enabled=on&volume=10",
        "section=environment&environment_history_save_enabled=on&environment_history_save_enabled=off",
        "section=environment&environment_history_save_enabled=on&section=environment",
        "section=general&timezone=480&unit=c&volume=50&environment_history_save_enabled=on",
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        parsed = base;
        assert(!app_settings_parse_form(invalid[i], strlen(invalid[i]), &base, &parsed));
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
}

static void test_timer_forms(void)
{
    app_settings_t base, parsed;
    app_settings_defaults(&base);
    assert(base.timer_minutes == 5U);
    base.manual_saving_requested = true;
    base.daily_pages_mask = 0U;
    base.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    base.alarm_volume = 23U;
    for (unsigned minutes = 0U; minutes <= UINT8_MAX; ++minutes) {
        char form[64];
        snprintf(form, sizeof(form), "section=timer&timer_minutes=%u", minutes);
        parsed = base;
        const bool valid = minutes >= 1U && minutes <= 180U;
        assert(app_settings_parse_form(form, strlen(form), &base, &parsed) == valid);
        if (valid) {
            assert(parsed.timer_minutes == minutes);
            parsed.timer_minutes = base.timer_minutes;
        }
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
        parsed = base;
        parsed.timer_minutes = (uint8_t)minutes;
        assert(app_settings_validate(&parsed) == valid);
    }
    const char *encoded = "timer_minutes=%31%38%30&section=timer";
    assert(app_settings_parse_form(encoded, strlen(encoded), &base, &parsed));
    assert(parsed.timer_minutes == 180U);
    const char *invalid[] = {
        "section=timer", "timer_minutes=25", "section=timer&timer_minutes=",
        "section=timer&timer_minutes=256", "section=timer&timer_minutes=-1",
        "section=timer&timer_minutes=%2B1", "section=timer&timer_minutes=+1",
        "section=timer&timer_minutes=1.5", "section=timer&timer_minutes=1x",
        "section=timer&timer_minutes=2147483648", "section=timer&timer_minutes=25%00",
        "section=timer&timer_minutes=25&timer_minutes=30",
        "section=timer&timer_minutes=25&%74imer_minutes=30",
        "section=timer&timer_minutes=25&section=timer",
        "section=timer&timer_minutes=25&volume=1",
        "section=timer&timer_minutes=25&daily_pages_mask=0",
        "section=timer&timer_minutes=25&alarm_volume=0",
        "section=timer&timer_minutes=25&music_mode=repeat_all",
        "section=timer&timer_minutes=25&unknown=0",
        "section=updates&updates=stable&timer_minutes=25",
        "section=music&music_mode=repeat_one&timer_minutes=25",
        "section=general&timezone=480&unit=c&volume=50&timer_minutes=25",
        "timezone=480&unit=c&volume=50&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&timer_minutes=25",
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        parsed = base;
        assert(!app_settings_parse_form(invalid[i], strlen(invalid[i]), &base, &parsed));
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
    base.timer_minutes = 180U;
    const char *general = "section=general&timezone=330&unit=f&volume=51";
    assert(app_settings_parse_form(general, strlen(general), &base, &parsed));
    assert(parsed.timer_minutes == base.timer_minutes);
}

static void test_order_permutations(uint8_t *order, size_t position)
{
    if (position < APP_DAILY_PAGE_COUNT) {
        for (size_t i = position; i < APP_DAILY_PAGE_COUNT; ++i) {
            const uint8_t saved = order[position];
            order[position] = order[i];
            order[i] = saved;
            test_order_permutations(order, position + 1U);
            order[i] = order[position];
            order[position] = saved;
        }
        return;
    }
    app_settings_t base, parsed;
    app_settings_defaults(&base);
    base.manual_saving_requested = true;
    base.update_channel = APP_UPDATE_CHANNEL_BETA;
    base.alarm_volume = 13U;
    base.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    char form[128];
    snprintf(form, sizeof(form),
             "section=pages&daily_pages_mask=17&daily_pages_order=%u,%u,%u,%u,%u,%u,%u,%u",
             order[0], order[1], order[2], order[3], order[4], order[5], order[6], order[7]);
    assert(app_settings_parse_form(form, strlen(form), &base, &parsed));
    assert(parsed.daily_pages_mask == 17U);
    assert(memcmp(parsed.daily_pages_order, order, APP_DAILY_PAGE_COUNT) == 0);
    parsed.daily_pages_mask = base.daily_pages_mask;
    memcpy(parsed.daily_pages_order, base.daily_pages_order, APP_DAILY_PAGE_COUNT);
    assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    ++tested_orders;
}

static void test_daily_order_forms(void)
{
    uint8_t order[] = APP_DAILY_PAGES_DEFAULT_ORDER;
    tested_orders = 0U;
    test_order_permutations(order, 0U);
    assert(tested_orders == 40320U);
    assert(!app_daily_pages_order_valid(NULL));
    app_settings_t base, parsed;
    app_settings_defaults(&base);
    const uint8_t reverse[] = {32U, 16U, 8U, 64U, 4U, 2U, 1U, 128U};
    memcpy(base.daily_pages_order, reverse, sizeof(reverse));
    const char *general = "section=general&timezone=330&unit=f&volume=51";
    assert(app_settings_parse_form(general, strlen(general), &base, &parsed));
    assert(memcmp(parsed.daily_pages_order, reverse, sizeof(reverse)) == 0);
    const char *invalid_values[] = {
        "1,2,4,8,16,32", /* An old six-page form cannot reset or omit TIMER. */
        "1,2,4,64,8,16,16", "1,2,4,64,8,16,32,1",
        "0,2,4,64,8,16,32", "128,2,4,64,8,16,32", "3,2,4,64,8,16,32",
        "01,2,4,64,8,16,32", "-1,2,4,64,8,16,32", "%2B1,2,4,64,8,16,32",
        "1,2,4,64,8,16,32,", "1,,4,64,8,16,32", "1,2,4,64,8,16,+32",
        "1,2,4,64,8,16,32+", "1,2,4,64,8,16,032", "1;2;4;64;8;16;32",
        "1,2,4,64,8,16,32%00",
        "", "1,2,4,8,16", "1,2,4,8,16,32,1", "1,2,4,8,16,16",
        "0,2,4,8,16,32", "64,2,4,8,16,32", "3,2,4,8,16,32",
        "01,2,4,8,16,32", "-1,2,4,8,16,32", "%2B1,2,4,8,16,32",
        "1,2,4,8,16,32,", "1,,4,8,16,32", "1,2,4,8,16,+32",
        "1,2,4,8,16,32+", "1,2,4,8,16,032", "1;2;4;8;16;32",
        "weather,market,calendar,image,music,chat", "1,2,4,8,16,32%00",
    };
    for (size_t i = 0U; i < sizeof(invalid_values) / sizeof(invalid_values[0]); ++i) {
        char form[180];
        snprintf(form, sizeof(form), "section=pages&daily_pages_mask=63&daily_pages_order=%s", invalid_values[i]);
        parsed = base;
        assert(!app_settings_parse_form(form, strlen(form), &base, &parsed));
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
    const char *invalid_forms[] = {
        "section=pages&daily_pages_mask=0", /* Order is required, not silently reset. */
        "section=pages&daily_pages_order=1,2,4,64,8,16,32",
        "daily_pages_mask=63&daily_pages_order=1,2,4,64,8,16,32",
        "section=pages&daily_pages_mask=63&daily_pages_order=1,2,4,64,8,16,32&daily_pages_order=32,16,8,4,2,1",
        "section=pages&daily_pages_mask=63&daily_pages_order=1,2,4,64,8,16,32&%64aily_pages_order=32,16,8,4,2,1",
        "section=pages&daily_pages_mask=63&daily_pages_order=1,2,4,64,8,16,32&volume=1",
        "section=pages&daily_pages_mask=63&daily_pages_order=1,2,4,64,8,16,32&unknown=1",
        "section=general&timezone=480&unit=c&volume=50&daily_pages_order=1,2,4,64,8,16,32",
        "section=updates&updates=beta&daily_pages_order=1,2,4,64,8,16,32",
        "section=music&music_mode=repeat_all&daily_pages_order=1,2,4,64,8,16,32",
        "section=alarm&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&alarm_volume=30&daily_pages_order=1,2,4,64,8,16,32",
        "timezone=480&unit=c&volume=50&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&daily_pages_order=1,2,4,64,8,16,32",
    };
    for (size_t i = 0U; i < sizeof(invalid_forms) / sizeof(invalid_forms[0]); ++i) {
        parsed = base;
        assert(!app_settings_parse_form(invalid_forms[i], strlen(invalid_forms[i]), &base, &parsed));
        assert(memcmp(&parsed, &base, sizeof(base)) == 0);
    }
    for (size_t i = 0U; i < APP_DAILY_PAGE_COUNT; ++i) {
        for (unsigned id = 0U; id <= UINT8_MAX; ++id) {
            parsed = base;
            parsed.daily_pages_order[i] = (uint8_t)id;
            assert(app_settings_validate(&parsed) == (id == reverse[i]));
        }
    }
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

static void test_scoped_edits(void)
{
    app_settings_t settings;
    app_settings_defaults(&settings);
    settings.manual_saving_requested = true;
    settings.update_channel = APP_UPDATE_CHANNEL_BETA;
    settings.utc_offset_minutes = -300;
    settings.alarm_hour = 23U;
    settings.alarm_minute = 42U;
    settings.alarm_weekdays = APP_SETTINGS_ALARM_WEEKENDS_MASK;
    settings.daily_pages_mask = 0U;
    settings.music_mode = APP_MUSIC_MODE_REPEAT_ONE;
    assert(app_settings_set_field(&settings, APP_SETTING_VOLUME, 0U));
    assert(settings.audio_playback_volume == 0U);
    assert(app_settings_set_field(&settings, APP_SETTING_ALARM_ENABLED, 1U));
    assert(settings.alarm_enabled);
    assert(settings.alarm_hour == 23U && settings.alarm_minute == 42U);
    assert(settings.alarm_weekdays == APP_SETTINGS_ALARM_WEEKENDS_MASK);
    assert(settings.manual_saving_requested);
    assert(settings.update_channel == APP_UPDATE_CHANNEL_BETA);
    assert(settings.utc_offset_minutes == -300);
    assert(settings.daily_pages_mask == 0U);
    assert(settings.music_mode == APP_MUSIC_MODE_REPEAT_ONE);
    const app_settings_t before = settings;
    assert(!app_settings_set_field(&settings, APP_SETTING_VOLUME, 101U));
    assert(!app_settings_set_field(&settings, APP_SETTING_ALARM_ENABLED, 2U));
    /* The retired DISPLAY field (2) cannot be written through device edits. */
    assert(!app_settings_set_field(&settings, (app_setting_field_t)2, 1U));
    assert(!app_settings_set_field(&settings, (app_setting_field_t)99, 0U));
    assert(memcmp(&settings, &before, sizeof(settings)) == 0);
    assert(!app_settings_set_field(NULL, APP_SETTING_VOLUME, 50U));
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
    assert(policy.sensor_read_interval_ms == 30000U);
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
    assert(settings.alarm_volume == 68U); /* old forms retain this field */
    assert(settings.update_channel == updates);
    assert(settings.alarm_enabled == alarm_enabled);
    assert(settings.alarm_hour == alarm_hour);
    assert(settings.alarm_minute == alarm_minute);
    assert(settings.alarm_weekdays == alarm_weekdays);
}

static void test_form_parser_preserves_device_setting(void)
{
    app_settings_t base_settings, parsed;
    app_settings_defaults(&base_settings);
    const char *with_alarm_volume = "timezone=480&unit=c&volume=0&updates=stable&alarm=off&alarm_hour=7&alarm_minute=30&alarm_days=62&alarm_volume=25";
    assert(app_settings_parse_form(with_alarm_volume, strlen(with_alarm_volume), &base_settings, &parsed));
    assert(parsed.audio_playback_volume == 0U && parsed.alarm_volume == 25U);
    char duplicate[256];
    snprintf(duplicate, sizeof(duplicate), "%s&alarm_volume=50", with_alarm_volume);
    assert(!app_settings_parse_form(duplicate, strlen(duplicate), &base_settings, &parsed));
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
    test_environment_forms();
    test_defaults_and_validation();
    test_scoped_forms();
    test_daily_page_forms();
    test_daily_order_forms();
    test_timer_forms();
    test_music_forms();
    test_scoped_edits();
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
