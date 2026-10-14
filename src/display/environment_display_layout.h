#pragma once

#include <stdio.h>
#include <string.h>
#include "environment_history.h"
#include "display_interaction_model.h"
#include "u8g2.h"

enum {
    ENV_CHART_LEFT = 20, ENV_CHART_RIGHT = 380,
    ENV_TEMP_TOP = 78, ENV_TEMP_BOTTOM = 131,
    ENV_HUM_TOP = 175, ENV_HUM_BOTTOM = 228,
};

static inline int32_t environment_display_temperature(int32_t centi_c,
                                                      bool fahrenheit)
{
    return fahrenheit ? centi_c * 9 / 5 + 3200 : centi_c;
}

static inline void environment_display_number(char *text, size_t capacity,
                                              int32_t hundredths)
{
    const int32_t tenths = hundredths < 0 ? -((-hundredths + 5) / 10)
                                         : (hundredths + 5) / 10;
    const int32_t magnitude = tenths < 0 ? -tenths : tenths;
    snprintf(text, capacity, "%s%ld.%ld", tenths < 0 ? "-" : "",
             (long)(magnitude / 10), (long)(magnitude % 10));
}

typedef struct {
    int64_t start_monotonic_ms;
    bool rolling;
} environment_display_axis_t;

static inline environment_display_axis_t environment_display_axis(
    const environment_history_snapshot_t *history)
{
    environment_display_axis_t axis = {0};
    if (history == NULL) return axis;
    const int64_t now_ms = history->captured_monotonic_ms;
    int64_t origin_ms = history->recording_started_monotonic_ms;
    if (origin_ms > now_ms) origin_ms = now_ms;
    if (history->restored_history) {
        const size_t count = history->count < ENVIRONMENT_HISTORY_CAPACITY
            ? history->count : ENVIRONMENT_HISTORY_CAPACITY;
        for (size_t i = 0; i < count; ++i) {
            const environment_history_point_t *point = &history->points[i];
            const int64_t age = now_ms - point->start_monotonic_ms;
            if (point->sample_count > 0 && age >= 0 &&
                age < ENVIRONMENT_HISTORY_WINDOW_MS &&
                point->start_monotonic_ms < origin_ms) {
                origin_ms = point->start_monotonic_ms;
            }
        }
    }
    axis.rolling = now_ms - origin_ms >= ENVIRONMENT_HISTORY_WINDOW_MS;
    axis.start_monotonic_ms = axis.rolling
        ? now_ms - ENVIRONMENT_HISTORY_WINDOW_MS : origin_ms;
    return axis;
}

static inline int environment_display_x(int64_t axis_start_ms, int64_t point_ms)
{
    int64_t elapsed = point_ms >= axis_start_ms ? point_ms - axis_start_ms : 0;
    if (elapsed > ENVIRONMENT_HISTORY_WINDOW_MS) elapsed = ENVIRONMENT_HISTORY_WINDOW_MS;
    return ENV_CHART_LEFT + (int)(elapsed * (ENV_CHART_RIGHT - ENV_CHART_LEFT) /
                                  ENVIRONMENT_HISTORY_WINDOW_MS);
}

static inline int environment_display_y(int32_t value, int32_t low, int32_t high,
                                        int top, int bottom)
{
    if (high <= low) return (top + bottom) / 2;
    if (value < low) value = low;
    if (value > high) value = high;
    return bottom - (int)((int64_t)(value - low) * (bottom - top) / (high - low));
}

static inline void environment_display_text(u8g2_t *screen, const char *text,
    const uint8_t *font, int left, int right, int baseline, bool align_right)
{
    u8g2_SetFont(screen, font);
    int width = (int)u8g2_GetStrWidth(screen, text);
    if (width > right - left) {
        text = "--";
        width = (int)u8g2_GetStrWidth(screen, text);
    }
    u8g2_DrawStr(screen, align_right ? right - width : left, baseline, text);
}

static inline void environment_display_chart(u8g2_t *screen,
    const environment_history_snapshot_t *history, bool humidity)
{
    const int top = humidity ? ENV_HUM_TOP : ENV_TEMP_TOP;
    const int bottom = humidity ? ENV_HUM_BOTTOM : ENV_TEMP_BOTTOM;
    u8g2_DrawHLine(screen, ENV_CHART_LEFT, bottom + 1,
                   ENV_CHART_RIGHT - ENV_CHART_LEFT + 1);
    if (history == NULL || !history->has_samples) {
        environment_display_text(screen, "WAITING FOR DATA", u8g2_font_6x13_tf,
                                  130, 380, top + 32, false);
        return;
    }
    int32_t low = humidity ? history->humidity_min_centi_percent
                           : history->temperature_min_centi_c;
    int32_t high = humidity ? history->humidity_max_centi_percent
                            : history->temperature_max_centi_c;
    /* Keep a flat room readable without visually exaggerating tiny changes. */
    const int32_t minimum_span = humidity ? 500 : 100;
    if (high - low < minimum_span) {
        const int32_t midpoint = (high + low) / 2;
        low = midpoint - minimum_span / 2;
        high = low + minimum_span;
    }
    bool previous_valid = false;
    bool previous_restored = false;
    const environment_display_axis_t axis = environment_display_axis(history);
    int previous_x = 0, previous_y = 0;
    int64_t previous_time = 0;
    const size_t count = history->count < ENVIRONMENT_HISTORY_CAPACITY
        ? history->count : ENVIRONMENT_HISTORY_CAPACITY;
    for (size_t i = 0; i < count; ++i) {
        const environment_history_point_t *point = &history->points[i];
        const int64_t age = history->captured_monotonic_ms - point->start_monotonic_ms;
        if (point->sample_count == 0 || age < 0 || age >= ENVIRONMENT_HISTORY_WINDOW_MS) {
            previous_valid = false;
            continue;
        }
        const int x = environment_display_x(axis.start_monotonic_ms,
                                             point->start_monotonic_ms);
        const int32_t value = humidity ? point->humidity_mean_centi_percent
                                      : point->temperature_mean_centi_c;
        const int y = environment_display_y(value, low, high, top, bottom);
        /* Failed buckets and the boundary between boots remain real gaps. */
        if (previous_valid && point->failure_count == 0 &&
            point->restored == previous_restored &&
            point->start_monotonic_ms - previous_time == ENVIRONMENT_HISTORY_BUCKET_MS) {
            u8g2_DrawLine(screen, previous_x, previous_y, x, y);
        } else {
            u8g2_DrawPixel(screen, x, y);
        }
        previous_valid = point->failure_count == 0;
        previous_restored = point->restored;
        previous_x = x; previous_y = y;
        previous_time = point->start_monotonic_ms;
    }
}

static inline void environment_display_render(u8g2_t *screen,
    const environment_history_snapshot_t *history, bool fahrenheit,
    const char *next_daily_page)
{
    environment_display_text(screen, "ENVIRONMENT", u8g2_font_helvB18_tf,
                              12, 300, 28, false);
    environment_display_text(screen, "24h", u8g2_font_6x13_tf, 320, 388, 28, true);
    u8g2_DrawHLine(screen, 12, 44, 376);
    for (unsigned i = 0; i < 2; ++i) {
        const int baseline = i == 0 ? 66 : 163;
        const char *label = i == 0 ? (fahrenheit ? "TEMP F" : "TEMP C") : "HUMIDITY %";
        environment_display_text(screen, label, u8g2_font_helvB12_tf,
                                  12, 132, baseline, false);
        char range[48] = "MIN --  MAX --";
        if (history != NULL && history->has_samples) {
            char low[16], high[16];
            environment_display_number(low, sizeof(low), i == 0
                ? environment_display_temperature(history->temperature_min_centi_c, fahrenheit)
                : history->humidity_min_centi_percent);
            environment_display_number(high, sizeof(high), i == 0
                ? environment_display_temperature(history->temperature_max_centi_c, fahrenheit)
                : history->humidity_max_centi_percent);
            snprintf(range, sizeof(range), "MIN %s  MAX %s", low, high);
        }
        environment_display_text(screen, range, u8g2_font_6x13_tf,
                                  134, 388, baseline, true);
        environment_display_chart(screen, history, i != 0);
    }
    const environment_display_axis_t axis = environment_display_axis(history);
    environment_display_text(screen, axis.rolling ? "-24H" : "START",
                              u8g2_font_5x8_tf, 20, 80, 242, false);
    environment_display_text(screen, axis.rolling ? "-12H" : "+12H",
                              u8g2_font_5x8_tf, 190, 240, 242, false);
    environment_display_text(screen, axis.rolling ? "NOW" : "+24H",
                              u8g2_font_5x8_tf, 330, 380, 242, true);
    u8g2_DrawHLine(screen, 12, 250, 376);
    char footer[64];
    display_interaction_format_daily_navigation(footer, sizeof(footer), next_daily_page);
    environment_display_text(screen, footer, u8g2_font_6x13_tf, 12, 388, 270, false);
    char source[64] = "RECORDING NOT AVAILABLE";
    if (history != NULL) {
        int64_t elapsed = history->captured_monotonic_ms - history->recording_started_monotonic_ms;
        if (elapsed < 0) elapsed = 0;
        if (history->restored_history) {
            snprintf(source, sizeof(source), "SD HISTORY + CURRENT SESSION");
        } else if (elapsed >= ENVIRONMENT_HISTORY_WINDOW_MS) {
            snprintf(source, sizeof(source), "CURRENT SESSION | LAST 24h");
        } else {
            const unsigned minutes = (unsigned)(elapsed / 60000);
            snprintf(source, sizeof(source), "SINCE BOOT %02uh %02um", minutes / 60, minutes % 60);
        }
    }
    environment_display_text(screen, source, u8g2_font_6x13_tf, 12, 388, 292, false);
}
