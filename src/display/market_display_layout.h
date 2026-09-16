#pragma once

#include <stdio.h>
#include <string.h>

#include "display_interaction_model.h"
#include "market_display_model.h"
#include "u8g2.h"

/* One shared native-font layout for the panel, pixel-boundary tests and SVG.
 * x_right is exclusive. Overflowing values show --, never clipped digits. */
enum { MARKET_DISPLAY_LINE_LIMIT = 28, MARKET_DISPLAY_TEXT_CAPACITY = 64 };
typedef struct {
    char text[MARKET_DISPLAY_TEXT_CAPACITY];
    const uint8_t *font;
    int x_left;
    int x_right;
    int baseline_y;
    bool right_aligned;
} market_display_line_t;

static inline void market_display_add_line(
    market_display_line_t *line, const char *text, const uint8_t *font,
    int x_left, int x_right, int baseline_y, bool right_aligned)
{
    snprintf(line->text, sizeof(line->text), "%s", text != NULL ? text : "--");
    line->font = font;
    line->x_left = x_left;
    line->x_right = x_right;
    line->baseline_y = baseline_y;
    line->right_aligned = right_aligned;
}

static inline size_t market_display_lines(
    const display_market_t *market,
    market_display_line_t lines[MARKET_DISPLAY_LINE_LIMIT])
{
    size_t n = 0U;
    char text[MARKET_DISPLAY_TEXT_CAPACITY];
    market_display_add_line(&lines[n++], "MARKET", u8g2_font_helvB18_tf, 12, 150, 28, false);
    display_market_format_current_time(text, sizeof(text), market);
    market_display_add_line(&lines[n++], text, u8g2_font_6x13_tf, 170, 388, 28, true);
    market_display_add_line(&lines[n++], "INDEX", u8g2_font_6x13_tf, 12, 130, 60, false);
    market_display_add_line(&lines[n++], "PRICE", u8g2_font_6x13_tf, 142, 270, 60, true);
    market_display_add_line(&lines[n++], "CHANGE", u8g2_font_6x13_tf, 294, 388, 60, true);
    size_t count = market->count < DISPLAY_MARKET_ROW_LIMIT ? market->count : DISPLAY_MARKET_ROW_LIMIT;
    for (size_t i = 0U; i < count; ++i) {
        const display_market_row_t *row = &market->rows[i];
        const int baseline = 84 + (int)i * 36;
        market_display_add_line(&lines[n++], row->name, u8g2_font_wqy16_t_gb2312,
                                12, 130, baseline, false);
        if (!display_market_format_price(text, sizeof(text), row->valid, row->price))
            snprintf(text, sizeof(text), "--");
        market_display_add_line(&lines[n++], text, u8g2_font_helvB18_tf,
                                142, 270, baseline, true);
        if (!display_market_format_percent(text, sizeof(text), row->valid, row->percent))
            snprintf(text, sizeof(text), "--");
        market_display_add_line(&lines[n++], text, u8g2_font_helvB14_tf,
                                294, 388, baseline, true);
        display_market_format_quote_time(text, sizeof(text), row->valid ? row->quote_time : NULL);
        if (row->valid && row->stale) {
            const size_t length = strlen(text);
            snprintf(text + length, sizeof(text) - length, " *");
        }
        market_display_add_line(&lines[n++], text, u8g2_font_6x13_tf,
                                12, 142, baseline + 16, false);
    }
    display_market_format_status(text, sizeof(text), market);
    market_display_add_line(&lines[n++], text, u8g2_font_6x13_tf, 12, 388, 264, false);
    market_display_add_line(&lines[n++], display_interaction_market_navigation_footer(),
                            u8g2_font_6x13_tf, 12, 388, 279, false);
    market_display_add_line(&lines[n++], display_interaction_market_action_footer(),
                            u8g2_font_6x13_tf, 12, 388, 294, false);
    return n;
}

static inline int market_display_draw_line(u8g2_t *screen,
                                           const market_display_line_t *line)
{
    u8g2_SetFont(screen, line->font);
    const char *text = line->text;
    int width = (int)u8g2_GetUTF8Width(screen, text);
    if (width > line->x_right - line->x_left) {
        text = "--";
        width = (int)u8g2_GetUTF8Width(screen, text);
    }
    const int x = line->right_aligned ? line->x_right - width : line->x_left;
    u8g2_DrawUTF8(screen, x, line->baseline_y, text);
    return x;
}
