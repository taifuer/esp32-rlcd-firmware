#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u8g2.h"
#include "quick_settings.h"
#include "hold_interaction.h"
#include "display_interaction_model.h"
#include "settings_display_layout.h"

enum { WIDTH = 400, HEIGHT = 300, MARGIN = 12 };
static unsigned char pixels[HEIGHT][WIDTH];
static int ink_left, ink_right, ink_top, ink_bottom;

/* Capture real U8g2 font decoder output without an LCD driver. No surrogate
 * desktop fonts, scaling, anti-aliasing or fake bold in these checks. */
void u8g2_DrawHVLine(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y,
                     u8g2_uint_t length, uint8_t direction)
{
    assert(direction == 0);
    assert(y < HEIGHT && x + length <= WIDTH);
    for (unsigned i = 0; i < length; ++i) {
        pixels[y][x + i] = screen->draw_color != 0;
        if (screen->draw_color == 0) continue;
        if ((int)(x + i) < ink_left) ink_left = (int)(x + i);
        if ((int)(x + i) > ink_right) ink_right = (int)(x + i);
        if ((int)y < ink_top) ink_top = (int)y;
        if ((int)y > ink_bottom) ink_bottom = (int)y;
    }
}

uint8_t u8g2_IsIntersection(u8g2_t *screen, u8g2_uint_t x0, u8g2_uint_t y0,
                            u8g2_uint_t x1, u8g2_uint_t y1)
{
    (void)screen; (void)x1; (void)y1;
    return x0 < WIDTH && y0 < HEIGHT;
}

static void draw_line(u8g2_t *screen, const settings_display_line_t *line)
{
    u8g2_SetFont(screen, line->font);
    int width = (int)u8g2_GetStrWidth(screen, line->text);
    assert(width > 0 && width <= WIDTH - 2 * MARGIN);
    ink_left = WIDTH; ink_right = -1; ink_top = HEIGHT; ink_bottom = -1;
    u8g2_DrawStr(screen, (WIDTH - width) / 2, line->baseline_y, line->text);
    assert(ink_right >= ink_left);
    assert(ink_left >= MARGIN && ink_right < WIDTH - MARGIN);
    /* Bearings can move actual ink slightly relative to advance-width centre. */
    assert(abs(ink_left + ink_right - (WIDTH - 1)) <= 4);
}

static void render_settings(u8g2_t *screen, const char *power, bool manual)
{
    memset(pixels, 0, sizeof(pixels));
    screen->draw_color = 1;
    u8g2_SetFontPosBaseline(screen);
    u8g2_SetFontMode(screen, 1);
    const settings_display_line_t title = {"SETTINGS", u8g2_font_helvB24_tf, 32};
    draw_line(screen, &title);
    u8g2_SetFont(screen, u8g2_font_6x13_tf);
    u8g2_DrawStr(screen, WIDTH - MARGIN - u8g2_GetStrWidth(screen, "1/3"), 32, "1/3");
    for (int x = MARGIN; x < WIDTH - MARGIN; ++x) pixels[44][x] = pixels[250][x] = 1;

    settings_display_line_t lines[SETTINGS_DISPLAY_LINE_COUNT];
    settings_display_lines(power, manual, lines);
    int previous_bottom = 44;
    for (unsigned i = 0; i < SETTINGS_DISPLAY_LINE_COUNT; ++i) {
        draw_line(screen, &lines[i]);
        assert(ink_top - previous_bottom >= 8);
        if (i < 5) assert(ink_top > 44 && ink_bottom < 240);
        else assert(ink_top > 250 && ink_bottom < HEIGHT - 2);
        if (i == 1) assert(ink_bottom - ink_top + 1 >= 12);
        previous_bottom = i == 4 ? 250 : ink_bottom;
    }
}

static void test_power_states(u8g2_t *screen)
{
    assert(strcmp(settings_display_power_text(false, false, false, false, false), "NORMAL") == 0);
    assert(strcmp(settings_display_power_text(false, true, false, false, false), "SAVING") == 0);
    assert(strcmp(settings_display_power_text(false, false, true, true, true), "NORMAL | USB") == 0);
    assert(strcmp(settings_display_power_text(false, true, false, true, true), "SAVING | MANUAL") == 0);
    assert(strcmp(settings_display_power_text(false, true, false, false, true), "SAVING | LOW BAT") == 0);
    assert(strcmp(settings_display_power_text(true, false, true, true, true), "NORMAL | PENDING") == 0);
    assert(strcmp(settings_display_power_text(true, true, true, true, true), "SAVING | PENDING") == 0);
    for (unsigned mask = 0; mask < 32; ++mask) {
        const char *power = settings_display_power_text(mask & 1, mask & 2,
                                                       mask & 4, mask & 8, mask & 16);
        render_settings(screen, power, (mask & 8) != 0);
    }
}

static void print_svg(void)
{
    puts("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"300\" viewBox=\"0 0 400 300\" role=\"img\" aria-labelledby=\"title description\">");
    puts("  <title id=\"title\">Settings: centered power status</title>");
    puts("  <desc id=\"description\">400 by 300 native font pixels. Hold KEY for two seconds to open web settings. POWER and NORMAL are centered. BOOT returns home; KEY opens online update; hold BOOT for two seconds to enable manual saving.</desc>");
    puts("  <rect width=\"400\" height=\"300\" fill=\"#000\"/>");
    puts("  <path fill=\"#fff\" shape-rendering=\"crispEdges\" d=\"");
    for (unsigned y = 0; y < HEIGHT; ++y) {
        for (unsigned x = 0; x < WIDTH;) {
            if (!pixels[y][x]) { ++x; continue; }
            unsigned start = x;
            while (x < WIDTH && pixels[y][x]) ++x;
            printf("M%u %uh%uv1h-%uz", start, y, x - start, x - start);
        }
        putchar('\n');
    }
    puts("\"/>\n</svg>");
}

int main(int argc, char **argv)
{
    u8g2_t screen = {0};
    u8g2_SetFont(&screen, u8g2_font_helvB24_tf);
    const char *titles[] = {
        app_hold_prompt_title(APP_PAGE_ACTION_OPEN_SETTINGS, APP_HOLD_UPDATE_CHECK, false),
        quick_settings_hold_title(), "WEB SETTINGS", "VOLUME", "100 %"};
    for (unsigned i = 0U; i < sizeof(titles) / sizeof(titles[0]); ++i)
        assert(u8g2_GetStrWidth(&screen, titles[i]) <= 376U);
    u8g2_SetFont(&screen, u8g2_font_6x13_tf);
    const char *lines[] = {
        display_interaction_settings_action_footer(false),
        display_interaction_settings_action_footer(true),
        display_interaction_volume_navigation(),
        display_interaction_volume_action(),
        "HOLD KEY 2s TO OPEN", "Connect with a phone or computer",
        "Target changed; confirm again",
        "SAVE FAILED | TRY AGAIN OR CANCEL", "LIVE PREVIEW",
        "BOOT: CLOSE | KEY: USE HOTSPOT",
        "ACCESS CODE: ABC12345", "http://255.255.255.255"};
    for (unsigned i = 0U; i < sizeof(lines) / sizeof(lines[0]); ++i)
        assert(u8g2_GetStrWidth(&screen, lines[i]) <= 376U);
    test_power_states(&screen);
    if (argc == 2 && strcmp(argv[1], "--svg") == 0) {
        render_settings(&screen, "NORMAL", false);
        print_svg();
    } else {
        assert(argc == 1);
        puts("settings layout: real font pixels, centered power, safe margins and 32 power combinations passed");
    }
}
