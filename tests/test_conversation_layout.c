#include <assert.h>
#include <stdio.h>

#include "conversation_feedback.h"
#include "voice_display_model.h"
#include "u8g2.h"

enum { WIDTH = 400, HEIGHT = 300, MARGIN = 12, BASELINE = 174 };
static unsigned ink_pixels;
static unsigned current_baseline = BASELINE;

/* Decode the same native font and baseline used by DISPLAY_VOICE_STATE_FAILED,
 * without any display hardware or substituted desktop font. */
void u8g2_DrawHVLine(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y,
                     u8g2_uint_t length, uint8_t direction)
{
    (void)screen;
    assert(direction == 0);
    assert(x >= MARGIN && x + length <= WIDTH - MARGIN);
    assert(y >= current_baseline - 20 && y <= current_baseline + 5);
    ink_pixels += length;
}

uint8_t u8g2_IsIntersection(u8g2_t *screen, u8g2_uint_t x0, u8g2_uint_t y0,
                            u8g2_uint_t x1, u8g2_uint_t y1)
{
    (void)screen; (void)x1; (void)y1;
    return x0 < WIDTH && y0 < HEIGHT;
}

int main(void)
{
    u8g2_t screen = {0};
    u8g2_SetFont(&screen, u8g2_font_helvB14_tf);
    u8g2_SetFontPosBaseline(&screen);
    u8g2_SetFontMode(&screen, 1);
    screen.draw_color = 1;
    for (unsigned value = CONVERSATION_FEEDBACK_ERROR_AUTH;
         value <= CONVERSATION_FEEDBACK_ERROR_UNKNOWN; ++value) {
        const char *hint = conversation_feedback_error_hint(
            (conversation_feedback_error_t)value);
        const unsigned width = u8g2_GetStrWidth(&screen, hint);
        assert(width > 0 && width <= WIDTH - 2 * MARGIN);
        ink_pixels = 0;
        u8g2_DrawStr(&screen, (WIDTH - width) / 2U, BASELINE, hint);
        assert(ink_pixels > 0);
    }
    u8g2_SetFont(&screen, u8g2_font_6x13_tf);
    current_baseline = 280;
    for (unsigned state = 0; state < 4; ++state) {
        const char *footer = voice_display_failure_footer(state & 1U, state & 2U);
        const unsigned width = u8g2_GetStrWidth(&screen, footer);
        assert(width > 0 && width <= WIDTH - 2 * MARGIN);
        ink_pixels = 0;
        u8g2_DrawStr(&screen, (WIDTH - width) / 2U, current_baseline, footer);
        assert(ink_pixels > 0);
    }
    puts("AI error hints and retry footer: native font pixels fit display safety margins.");
    return 0;
}
