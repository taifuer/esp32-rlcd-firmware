#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "weather_display_model.h"

static void test_condition_icons(void)
{
    assert(display_weather_icon_from_qweather_code(100U) ==
           DISPLAY_WEATHER_ICON_CLEAR);
    assert(display_weather_icon_from_qweather_code(150U) ==
           DISPLAY_WEATHER_ICON_CLEAR);
    assert(display_weather_icon_from_qweather_code(104U) ==
           DISPLAY_WEATHER_ICON_CLOUDY);
    assert(display_weather_icon_from_qweather_code(153U) ==
           DISPLAY_WEATHER_ICON_CLOUDY);
    assert(display_weather_icon_from_qweather_code(207U) ==
           DISPLAY_WEATHER_ICON_WIND);
    assert(display_weather_icon_from_qweather_code(300U) ==
           DISPLAY_WEATHER_ICON_RAIN);
    assert(display_weather_icon_from_qweather_code(303U) ==
           DISPLAY_WEATHER_ICON_THUNDER);
    assert(display_weather_icon_from_qweather_code(399U) ==
           DISPLAY_WEATHER_ICON_RAIN);
    assert(display_weather_icon_from_qweather_code(404U) ==
           DISPLAY_WEATHER_ICON_SNOW);
    assert(display_weather_icon_from_qweather_code(509U) ==
           DISPLAY_WEATHER_ICON_FOG);
    assert(display_weather_icon_from_qweather_code(900U) ==
           DISPLAY_WEATHER_ICON_CLEAR);
    assert(display_weather_icon_from_qweather_code(901U) ==
           DISPLAY_WEATHER_ICON_SNOW);
    assert(display_weather_icon_from_qweather_code(999U) ==
           DISPLAY_WEATHER_ICON_UNKNOWN);
}

static void test_temperature_formatting(void)
{
    char text[16];
    char too_short[4] = "old";

    assert(display_weather_format_temperature(
        text, sizeof(text), 184, false, true));
    assert(strcmp(text, "18°C") == 0);
    assert(display_weather_format_temperature(
        text, sizeof(text), -35, false, false));
    assert(strcmp(text, "-4°") == 0);
    assert(display_weather_format_temperature(
        text, sizeof(text), 200, true, true));
    assert(strcmp(text, "68°F") == 0);
    assert(!display_weather_format_temperature(
        too_short, sizeof(too_short), 184, false, true));
    assert(too_short[0] == '\0');
    assert(!display_weather_format_temperature(
        NULL, sizeof(text), 184, false, true));
}

static void test_source_formatting(void)
{
    char text[48];

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_FRESH,
        true, 9U, 2U, 8U, 7U));
    assert(strcmp(text, "QWeather | 更新 09-02 08:07") == 0);

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_STALE,
        true, 9U, 2U, 8U, 7U));
    assert(strcmp(text, "QWeather | 缓存 09-02 08:07") == 0);

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_EXPIRED,
        true, 9U, 2U, 8U, 7U));
    assert(strcmp(text, "QWeather | 已过期 09-02 08:07") == 0);

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_UNKNOWN,
        false, 0U, 0U, 0U, 0U));
    assert(strcmp(text, "QWeather | 缓存时间未知") == 0);

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_UNKNOWN,
        true, 12U, 31U, 23U, 59U));
    assert(strcmp(text, "QWeather | 缓存 12-31 23:59") == 0);

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_FRESH,
        false, 0U, 0U, 0U, 0U));
    assert(strcmp(text, "QWeather | 缓存时间未知") == 0);

    assert(!display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_FRESH,
        true, 13U, 2U, 8U, 7U));
    assert(text[0] == '\0');

    const uint8_t invalid_times[][4] = {
        {0U, 2U, 8U, 7U}, {9U, 0U, 8U, 7U},
        {9U, 32U, 8U, 7U}, {9U, 2U, 24U, 7U},
        {9U, 2U, 8U, 60U},
    };
    for (size_t index = 0U;
         index < sizeof(invalid_times) / sizeof(invalid_times[0]); ++index) {
        assert(!display_weather_format_source(
            text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_STALE,
            true, invalid_times[index][0], invalid_times[index][1],
            invalid_times[index][2], invalid_times[index][3]));
        assert(text[0] == '\0');
    }

    assert(!display_weather_format_source(
        text, 8U, DISPLAY_WEATHER_FRESHNESS_EXPIRED,
        true, 9U, 2U, 8U, 7U));
    assert(text[0] == '\0');
    assert(!display_weather_format_source(
        NULL, sizeof(text), DISPLAY_WEATHER_FRESHNESS_FRESH,
        true, 9U, 2U, 8U, 7U));
    assert(!display_weather_format_source(
        text, 0U, DISPLAY_WEATHER_FRESHNESS_FRESH,
        true, 9U, 2U, 8U, 7U));
}

static void test_current_date_and_weekday(void)
{
    char text[32];
    const struct {
        uint16_t year;
        uint8_t month;
        uint8_t day;
        const char *expected;
    } dates[] = {
        {2026U, 9U, 5U, "9月5日  周六"},
        {2026U, 9U, 6U, "9月6日  周日"},
        {2026U, 9U, 7U, "9月7日  周一"},
        {2026U, 9U, 8U, "9月8日  周二"},
        {2026U, 9U, 9U, "9月9日  周三"},
        {2026U, 9U, 10U, "9月10日  周四"},
        {2026U, 9U, 11U, "9月11日  周五"},
        {2026U, 12U, 31U, "12月31日  周四"},
        {2027U, 1U, 1U, "1月1日  周五"},
        {2024U, 2U, 29U, "2月29日  周四"},
        {2000U, 2U, 29U, "2月29日  周二"},
        {2100U, 3U, 1U, "3月1日  周一"},
        {1970U, 1U, 1U, "1月1日  周四"},
    };
    for (size_t index = 0U; index < sizeof(dates) / sizeof(dates[0]);
         ++index) {
        assert(display_weather_format_current_date(
            text, sizeof(text), true, dates[index].year,
            dates[index].month, dates[index].day));
        assert(strcmp(text, dates[index].expected) == 0);
    }

    const uint16_t invalid_dates[][3] = {
        {0U, 0U, 0U}, {1969U, 12U, 31U}, {10000U, 1U, 1U},
        {2026U, 0U, 5U}, {2026U, 13U, 5U}, {2026U, 9U, 0U},
        {2026U, 9U, 31U}, {2026U, 2U, 29U}, {2100U, 2U, 29U},
    };
    for (size_t index = 0U;
         index < sizeof(invalid_dates) / sizeof(invalid_dates[0]); ++index) {
        assert(display_weather_format_current_date(
            text, sizeof(text), true, invalid_dates[index][0],
            (uint8_t)invalid_dates[index][1],
            (uint8_t)invalid_dates[index][2]));
        assert(strcmp(text, "--月--日  周-") == 0);
    }

    assert(display_weather_format_current_date(
        text, sizeof(text), false, 2026U, 9U, 5U));
    assert(strcmp(text, "--月--日  周-") == 0);
    assert(!display_weather_format_current_date(
        text, 8U, true, 2026U, 9U, 5U));
    assert(text[0] == '\0');
    assert(!display_weather_format_current_date(
        text, 8U, false, 0U, 0U, 0U));
    assert(text[0] == '\0');
    assert(!display_weather_format_current_date(
        NULL, sizeof(text), true, 2026U, 9U, 5U));
    assert(!display_weather_format_current_date(
        text, 0U, true, 2026U, 9U, 5U));
}

static void test_current_date_is_independent_of_cached_weather(void)
{
    char current_date[32];
    char source[48];
    char forecast_label[16];
    assert(display_weather_format_current_date(
        current_date, sizeof(current_date), true, 2026U, 9U, 6U));
    assert(display_weather_format_source(
        source, sizeof(source), DISPLAY_WEATHER_FRESHNESS_STALE,
        true, 9U, 5U, 23U, 50U));
    assert(display_weather_format_day_label(
        forecast_label, sizeof(forecast_label), "2026-09-05",
        true, 2026U, 9U, 6U));
    assert(strcmp(current_date, "9月6日  周日") == 0);
    assert(strcmp(source, "QWeather | 缓存 09-05 23:50") == 0);
    assert(strcmp(forecast_label, "9/5") == 0);

    /* A missing RTC must not borrow the cached weather's date as today. */
    assert(display_weather_format_current_date(
        current_date, sizeof(current_date), false, 2026U, 9U, 6U));
    assert(display_weather_format_day_label(
        forecast_label, sizeof(forecast_label), "2026-09-05",
        false, 2026U, 9U, 6U));
    assert(strcmp(current_date, "--月--日  周-") == 0);
    assert(strcmp(forecast_label, "9/5") == 0);
    assert(strcmp(source, "QWeather | 缓存 09-05 23:50") == 0);
}

static void test_forecast_day_labels_follow_dates(void)
{
    char text[16];

    assert(display_weather_format_day_label(
        text, sizeof(text), "2026-09-02", true, 2026U, 9U, 2U));
    assert(strcmp(text, "今天") == 0);
    assert(display_weather_format_day_label(
        text, sizeof(text), "2026-09-03", true, 2026U, 9U, 2U));
    assert(strcmp(text, "明天") == 0);
    assert(display_weather_format_day_label(
        text, sizeof(text), "2026-09-04", true, 2026U, 9U, 2U));
    assert(strcmp(text, "后天") == 0);
    assert(display_weather_format_day_label(
        text, sizeof(text), "2026-09-01", true, 2026U, 9U, 2U));
    assert(strcmp(text, "9/1") == 0);
    assert(display_weather_format_day_label(
        text, sizeof(text), "2027-01-01", true, 2026U, 12U, 31U));
    assert(strcmp(text, "明天") == 0);
    assert(display_weather_format_day_label(
        text, sizeof(text), "2024-02-29", false, 0U, 0U, 0U));
    assert(strcmp(text, "2/29") == 0);
    assert(!display_weather_format_day_label(
        text, sizeof(text), "2026-02-29", true, 2026U, 2U, 28U));
    assert(text[0] == '\0');
}

int main(void)
{
    test_condition_icons();
    test_temperature_formatting();
    test_source_formatting();
    test_current_date_and_weekday();
    test_current_date_is_independent_of_cached_weather();
    test_forecast_day_labels_follow_dates();
    puts("weather display model tests passed");
    return 0;
}
