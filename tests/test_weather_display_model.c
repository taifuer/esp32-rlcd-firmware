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
    assert(strcmp(text, "QWeather 08:07") == 0);

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_STALE,
        true, 9U, 2U, 8U, 7U));
    assert(strcmp(text, "QWeather | CACHED 08:07") == 0);

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_EXPIRED,
        true, 9U, 2U, 8U, 7U));
    assert(strcmp(text, "QWeather | OLD 09-02 08:07") == 0);

    assert(display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_UNKNOWN,
        false, 0U, 0U, 0U, 0U));
    assert(strcmp(text, "QWeather | CACHED") == 0);

    assert(!display_weather_format_source(
        text, sizeof(text), DISPLAY_WEATHER_FRESHNESS_FRESH,
        true, 13U, 2U, 8U, 7U));
    assert(text[0] == '\0');
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
    test_forecast_day_labels_follow_dates();
    puts("weather display model tests passed");
    return 0;
}
