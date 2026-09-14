#pragma once

#include <stdbool.h>
#include "display_interaction_model.h"
#include "u8g2.h"

/* Native bitmap fonts, never rescaled or artificially thickened. Keep the
 * actual layout shared with host pixel/width tests and the SVG preview. */
enum { SETTINGS_DISPLAY_LINE_COUNT = 7 };

typedef struct {
    const char *text;
    const uint8_t *font;
    int baseline_y;
} settings_display_line_t;

static inline const char *settings_display_power_text(
    bool pending, bool effective_saving, bool usb, bool manual, bool automatic)
{
    if (pending) return effective_saving ? "SAVING | PENDING" : "NORMAL | PENDING";
    if (usb) return "NORMAL | USB";
    if (manual) return "SAVING | MANUAL";
    if (automatic) return "SAVING | LOW BAT";
    return effective_saving ? "SAVING" : "NORMAL";
}

static inline void settings_display_lines(
    const char *power, bool manual_saving_requested,
    settings_display_line_t lines[SETTINGS_DISPLAY_LINE_COUNT])
{
    lines[0] = (settings_display_line_t){"WEB SETTINGS", u8g2_font_helvB18_tf, 98};
    lines[1] = (settings_display_line_t){"HOLD KEY 2s TO OPEN", u8g2_font_helvB12_tf, 131};
    lines[2] = (settings_display_line_t){"Use a phone or computer", u8g2_font_helvR12_tf, 157};
    lines[3] = (settings_display_line_t){"POWER", u8g2_font_helvB10_tf, 193};
    lines[4] = (settings_display_line_t){power, u8g2_font_helvB14_tf, 222};
    lines[5] = (settings_display_line_t){
        display_interaction_settings_navigation_footer(), u8g2_font_helvB10_tf, 270};
    lines[6] = (settings_display_line_t){
        display_interaction_settings_action_footer(manual_saving_requested),
        u8g2_font_helvB10_tf, 294};
}
