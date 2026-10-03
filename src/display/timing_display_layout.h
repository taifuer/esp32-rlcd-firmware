#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "timing_presets.h"
#include "u8g2.h"

/* One native-font layout shared by the device and host pixel-boundary tests. */
enum { TIMING_DISPLAY_LINE_COUNT = 6, TIMING_DISPLAY_TEXT_CAPACITY = 64 };
typedef struct {
    char text[TIMING_DISPLAY_TEXT_CAPACITY];
    const uint8_t *font;
    int baseline_y;
    uint8_t selected_start;
    uint8_t selected_length;
} timing_display_line_t;

static inline bool timing_display_format_time(char *text, size_t capacity,
                                              uint32_t seconds)
{
    if (text == NULL || capacity == 0U) return false;
    text[0] = '\0';
    if (seconds > 359999U) return false;
    const int written = snprintf(text, capacity, "%02lu:%02lu:%02lu",
        (unsigned long)(seconds / 3600U),
        (unsigned long)(seconds / 60U % 60U),
        (unsigned long)(seconds % 60U));
    if (written < 0 || (size_t)written >= capacity) {
        text[0] = '\0';
        return false;
    }
    return true;
}

static inline void timing_display_set_line(timing_display_line_t *line,
                                            const char *text,
                                            const uint8_t *font, int baseline_y)
{
    snprintf(line->text, sizeof(line->text), "%s", text);
    line->font = font;
    line->baseline_y = baseline_y;
    line->selected_start = 0U;
    line->selected_length = 0U;
}

static inline void timing_display_presets(timing_display_line_t *line,
                                          uint16_t selected, uint16_t custom)
{
    uint16_t choices[TIMING_PRESET_CAPACITY];
    const unsigned count = timing_preset_options(custom, choices);
    timing_display_set_line(line, "", u8g2_font_helvB12_tf, 211);
    size_t used = 0U;
    for (size_t i = 0U; i < count; ++i) {
        const size_t spacing = i == 0U ? 0U : 3U;
        const int written = snprintf(line->text + used, sizeof(line->text) - used,
            "%s%um", i == 0U ? "" : "   ", (unsigned)choices[i]);
        if (written < 0 || (size_t)written >= sizeof(line->text) - used) return;
        if (choices[i] == selected) {
            line->selected_start = (uint8_t)(used + spacing);
            line->selected_length = (uint8_t)((size_t)written - spacing);
        }
        used += (size_t)written;
    }
}

/* Draw only the selected token reversed; no outline, scaling or fake bold. */
static inline void timing_display_draw_line(u8g2_t *screen,
                                            const timing_display_line_t *line)
{
    if (line->text[0] == '\0') return;
    u8g2_SetFont(screen, line->font);
    const int text_x = (400 - (int)u8g2_GetStrWidth(screen, line->text)) / 2;
    u8g2_DrawStr(screen, text_x, line->baseline_y, line->text);
    if (line->selected_length == 0U) return;
    char prefix[TIMING_DISPLAY_TEXT_CAPACITY];
    char selected[TIMING_DISPLAY_TEXT_CAPACITY];
    memcpy(prefix, line->text, line->selected_start);
    prefix[line->selected_start] = '\0';
    memcpy(selected, line->text + line->selected_start, line->selected_length);
    selected[line->selected_length] = '\0';
    const int selected_x = text_x + (int)u8g2_GetStrWidth(screen, prefix);
    const int selected_width = (int)u8g2_GetStrWidth(screen, selected);
    const uint8_t previous_font_mode = screen->font_decode.is_transparent;
    u8g2_SetFontMode(screen, 1U);
    u8g2_SetDrawColor(screen, 1U);
    u8g2_DrawBox(screen, selected_x - 4, line->baseline_y - 16,
                 selected_width + 8, 20);
    u8g2_SetDrawColor(screen, 0U);
    u8g2_DrawStr(screen, selected_x, line->baseline_y, selected);
    u8g2_SetDrawColor(screen, 1U);
    u8g2_SetFontMode(screen, previous_font_mode);
}

static inline void timing_display_lines(
    bool stopwatch, bool running, bool paused, bool finished,
    uint32_t seconds, uint16_t duration_minutes, uint16_t custom_minutes,
    const char *next_daily_page,
    timing_display_line_t lines[TIMING_DISPLAY_LINE_COUNT])
{
    const bool idle = !running && !paused && !finished;
    const char *state = finished ? (stopwatch ? "LIMIT REACHED" : "DONE") :
                        paused ? "PAUSED" : running ? "RUNNING" :
                        stopwatch ? "READY" : "SELECT DURATION";
    const char *action = finished ? (stopwatch ? "START" : "RESET") :
                         paused ? "RESUME" : running ? "PAUSE" :
                         stopwatch ? "START" : "DURATION";
    char text[TIMING_DISPLAY_TEXT_CAPACITY];
    timing_display_set_line(&lines[0], stopwatch ? "STOPWATCH" : "TIMER",
                            u8g2_font_helvB24_tf, 32);
    timing_display_set_line(&lines[1], state, u8g2_font_helvB12_tf, 80);
    if (!timing_display_format_time(text, sizeof(text), seconds))
        snprintf(text, sizeof(text), "--:--:--");
    timing_display_set_line(&lines[2], text, u8g2_font_logisoso62_tn, 164);
    if (stopwatch) {
        snprintf(text, sizeof(text), "ELAPSED TIME");
    } else if (duration_minutes >= 1U && duration_minutes <= 180U) {
        snprintf(text, sizeof(text), "COUNTDOWN | %u MIN", (unsigned)duration_minutes);
    } else {
        snprintf(text, sizeof(text), "COUNTDOWN");
    }
    timing_display_set_line(&lines[3], text, u8g2_font_6x13_tf, 211);
    if (!stopwatch && idle)
        timing_display_presets(&lines[3], duration_minutes, custom_minutes);
    const int written = snprintf(text, sizeof(text), "BOOT: %s | KEY: %s",
        next_daily_page != NULL ? next_daily_page : "HOME", action);
    if (written < 0 || (size_t)written >= sizeof(text))
        snprintf(text, sizeof(text), "BOOT: HOME | KEY: %s", action);
    timing_display_set_line(&lines[4], text, u8g2_font_6x13_tf, 270);
    snprintf(text, sizeof(text), "HOLD KEY 2s: %s | HOLD BOOT 2s: %s",
             !stopwatch && idle ? "START" : "RESET",
             stopwatch ? "TIMER" : "STOPWATCH");
    timing_display_set_line(&lines[5], text, u8g2_font_6x13_tf, 294);
}

static inline void timing_display_done_lines(
    timing_display_line_t lines[TIMING_DISPLAY_LINE_COUNT])
{
    timing_display_lines(false, false, false, true, 0U, 0U, 0U, NULL, lines);
    timing_display_set_line(&lines[1], "TIME IS UP", u8g2_font_helvB14_tf, 80);
    timing_display_set_line(&lines[3], "Countdown complete", u8g2_font_6x13_tf, 211);
    timing_display_set_line(&lines[4], "BOOT: DISMISS | KEY: DISMISS",
                            u8g2_font_6x13_tf, 280);
    lines[5].text[0] = '\0';
}
