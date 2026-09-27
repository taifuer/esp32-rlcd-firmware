#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "u8g2.h"
#include "music_display_model.h"

enum { WIDTH = 400, HEIGHT = 300, MARGIN = 12 };
static unsigned ink_pixels;
static unsigned minimum_y, maximum_y;

void u8g2_DrawHVLine(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y,
                     u8g2_uint_t length, uint8_t direction)
{
    (void)screen;
    assert(direction == 0 && x >= MARGIN && x + length <= WIDTH - MARGIN);
    assert(y < HEIGHT);
    ink_pixels += length;
    if (y < minimum_y) minimum_y = y;
    if (y > maximum_y) maximum_y = y;
}

uint8_t u8g2_IsIntersection(u8g2_t *screen, u8g2_uint_t x0, u8g2_uint_t y0,
                            u8g2_uint_t x1, u8g2_uint_t y1)
{
    (void)screen; (void)x1; (void)y1;
    return x0 < WIDTH && y0 < HEIGHT;
}

static void check_line(u8g2_t *screen, const char *line, unsigned baseline)
{
    const unsigned width = u8g2_GetStrWidth(screen, line);
    assert(width > 0 && width <= WIDTH - 2 * MARGIN);
    ink_pixels = 0; minimum_y = HEIGHT; maximum_y = 0;
    u8g2_DrawStr(screen, (WIDTH - width) / 2U, baseline, line);
    assert(ink_pixels > 0 && minimum_y >= baseline - 13U && maximum_y <= baseline + 3U);
}

int main(void)
{
    u8g2_t screen = {0};
    u8g2_SetFont(&screen, u8g2_font_6x13_tf);
    u8g2_SetFontPosBaseline(&screen);
    u8g2_SetFontMode(&screen, 1);
    screen.draw_color = 1;
    const char *states[] = {"PLAYING", "PAUSED", "STOPPED", "CANNOT PLAY"};
    const char *modes[] = {"SEQUENTIAL", "REPEAT ONE", "REPEAT ALL"};
    char line[48];
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned j = 0; j < 3; ++j) {
            assert(music_display_format_status(line, sizeof(line), states[i], modes[j]));
            check_line(&screen, line, 152);
        }
    }
    const uint32_t seconds[] = {0, 1, 60, 61, 900, 1800, 3600, 5400};
    for (unsigned i = 0; i < sizeof(seconds) / sizeof(seconds[0]); ++i) {
        for (unsigned volume = 0; volume <= 100; volume += 10) {
            assert(music_display_format_detail(line, sizeof(line), volume, seconds[i], false));
            check_line(&screen, line, 225);
        }
    }
    assert(music_display_format_detail(line, sizeof(line), 68, 0, true));
    check_line(&screen, line, 225);
    puts("Music layout: native font mode, sleep countdown and error lines fit safe margins.");
    return 0;
}
