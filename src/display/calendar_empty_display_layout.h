#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "display_interaction_model.h"
#include "u8g2.h"

enum { CALENDAR_EMPTY_DISPLAY_LINE_COUNT = 4 };
typedef struct {
    char text[64];
    const uint8_t *font;
    int baseline_y;
} calendar_empty_display_line_t;

static inline void calendar_empty_display_lines(
    bool time_valid, const char *next_daily_page,
    calendar_empty_display_line_t lines[CALENDAR_EMPTY_DISPLAY_LINE_COUNT])
{
    lines[0] = (calendar_empty_display_line_t){
        "CALENDAR", u8g2_font_helvB24_tf, 32};
    lines[1] = (calendar_empty_display_line_t){
        "", u8g2_font_helvB14_tf, 136};
    snprintf(lines[1].text, sizeof(lines[1].text), "%s",
        time_valid ? "Date is out of range" : "Time is not set");
    lines[2] = (calendar_empty_display_line_t){
        "Set time in web settings", u8g2_font_6x13_tf, 176};
    lines[3] = (calendar_empty_display_line_t){
        "", u8g2_font_6x13_tf, 280};
    if (!display_interaction_format_daily_navigation(
            lines[3].text, sizeof(lines[3].text), next_daily_page)) {
        display_interaction_format_daily_navigation(
            lines[3].text, sizeof(lines[3].text), "HOME");
    }
}
