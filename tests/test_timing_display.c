#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calendar_empty_display_layout.h"
#include "timing_display_layout.h"

enum { WIDTH = 400, HEIGHT = 300, MARGIN = 12 };
static unsigned char pixels[HEIGHT][WIDTH];
static int ink_left, ink_right, ink_top, ink_bottom;
static unsigned box_count, box_x, box_y, box_width, box_height;

/* Capture the same unscaled native bitmap glyphs that the panel uses. */
void u8g2_DrawHVLine(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y,
                     u8g2_uint_t length, uint8_t direction)
{
    assert(direction == 0U);
    assert(y < HEIGHT && x >= MARGIN && x + length <= WIDTH - MARGIN);
    for (unsigned i = 0U; i < length; ++i) {
        pixels[y][x + i] = screen->draw_color != 0U;
        if (screen->draw_color == 0U) continue;
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

void u8g2_SetDrawColor(u8g2_t *screen, uint8_t color)
{
    assert(color <= 1U);
    screen->draw_color = color;
}

void u8g2_DrawBox(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y,
                  u8g2_uint_t width, u8g2_uint_t height)
{
    ++box_count;
    box_x = x; box_y = y; box_width = width; box_height = height;
    for (unsigned row = 0U; row < height; ++row)
        u8g2_DrawHVLine(screen, x, y + row, width, 0U);
}

static void draw_line(u8g2_t *screen, const char *text,
                      const uint8_t *font, int baseline_y)
{
    u8g2_SetFont(screen, font);
    const int width = (int)u8g2_GetStrWidth(screen, text);
    assert(width > 0 && width <= WIDTH - 2 * MARGIN);
    ink_left = WIDTH; ink_right = -1; ink_top = HEIGHT; ink_bottom = -1;
    u8g2_DrawStr(screen, (WIDTH - width) / 2, baseline_y, text);
    assert(ink_right >= ink_left && ink_left >= MARGIN && ink_right < WIDTH - MARGIN);
    assert(abs(ink_left + ink_right - (WIDTH - 1)) <= 4);
}

static void clear_frame(u8g2_t *screen)
{
    memset(pixels, 0, sizeof(pixels));
    screen->draw_color = 1U;
    u8g2_SetFontPosBaseline(screen);
    u8g2_SetFontMode(screen, 1U);
    for (unsigned x = MARGIN; x < WIDTH - MARGIN; ++x)
        pixels[44][x] = pixels[250][x] = 1U;
}

static void render_timing(u8g2_t *screen,
                          const timing_display_line_t lines[TIMING_DISPLAY_LINE_COUNT])
{
    clear_frame(screen);
    int previous_bottom = 0;
    for (unsigned i = 0U; i < TIMING_DISPLAY_LINE_COUNT; ++i) {
        if (lines[i].text[0] == '\0') continue;
        u8g2_SetFont(screen, lines[i].font);
        const int width = (int)u8g2_GetStrWidth(screen, lines[i].text);
        assert(width > 0 && width <= WIDTH - 2 * MARGIN);
        ink_left = WIDTH; ink_right = -1; ink_top = HEIGHT; ink_bottom = -1;
        box_count = 0U;
        timing_display_draw_line(screen, &lines[i]);
        assert(ink_right >= ink_left && ink_left >= MARGIN && ink_right < WIDTH - MARGIN);
        assert(abs(ink_left + ink_right - (WIDTH - 1)) <=
               (lines[i].selected_length > 0U ? 12 : 4));
        assert(screen->draw_color == 1U && screen->font_decode.is_transparent == 1U);
        assert(box_count == (lines[i].selected_length > 0U ? 1U : 0U));
        if (box_count != 0U) {
            unsigned white = 0U, black = 0U;
            for (unsigned y = box_y; y < box_y + box_height; ++y) {
                for (unsigned x = box_x; x < box_x + box_width; ++x) {
                    if (pixels[y][x]) ++white;
                    else ++black;
                }
            }
            assert(white > 0U && black > 0U);
            assert(box_y > 164U && box_y + box_height < 240U);
        }
        if (i > 0U) assert(ink_top - previous_bottom >= 8);
        if (i == 0U) assert(ink_bottom < 44);
        else if (i < 4U) assert(ink_top > 44 && ink_bottom < 240);
        else assert(ink_top > 250 && ink_bottom < HEIGHT - 2);
        if (i == 2U && lines[i].text[0] != '-')
            assert(ink_bottom - ink_top + 1 >= 60);
        previous_bottom = i == 0U ? 44 : i == 3U ? 250 : ink_bottom;
    }
}

static void test_formatting(void)
{
    const uint32_t seconds[] = {0U, 1U, 59U, 60U, 3599U, 3600U, 10800U, 359999U};
    const char *expected[] = {"00:00:00", "00:00:01", "00:00:59", "00:01:00",
                              "00:59:59", "01:00:00", "03:00:00", "99:59:59"};
    char text[9];
    for (size_t i = 0U; i < sizeof(seconds) / sizeof(seconds[0]); ++i) {
        assert(timing_display_format_time(text, sizeof(text), seconds[i]));
        assert(strcmp(text, expected[i]) == 0);
    }
    assert(!timing_display_format_time(text, sizeof(text), 360000U));
    assert(text[0] == '\0');
    assert(!timing_display_format_time(text, sizeof(text), UINT32_MAX));
    assert(!timing_display_format_time(text, 8U, 0U));
    assert(text[0] == '\0');
    assert(!timing_display_format_time(text, 0U, 0U));
    assert(!timing_display_format_time(NULL, sizeof(text), 0U));
}

static void test_states_and_navigation(u8g2_t *screen)
{
    const char *destinations[] = {"HOME", "WEATHER", "MARKET", "CALENDAR",
                                 "IMAGE", "MUSIC", "CHAT", "TIMER", "ENVIRONMENT", NULL};
    const uint32_t values[] = {0U, 1U, 59U, 60U, 3599U, 3600U, 10800U,
                               359999U, UINT32_MAX};
    timing_display_line_t lines[TIMING_DISPLAY_LINE_COUNT];
    for (unsigned stopwatch = 0U; stopwatch <= 1U; ++stopwatch) {
        for (unsigned state = 0U; state < 4U; ++state) {
            for (size_t d = 0U; d < sizeof(destinations) / sizeof(destinations[0]); ++d) {
                for (size_t v = 0U; v < sizeof(values) / sizeof(values[0]); ++v) {
                    timing_display_lines(stopwatch != 0U, state == 1U, state == 2U,
                        state == 3U, values[v], 180U, 180U, destinations[d], lines);
                    assert(strcmp(lines[0].text, stopwatch ? "STOPWATCH" : "TIMER") == 0);
                    const char *states[] = {stopwatch ? "READY" : "SELECT DURATION", "RUNNING", "PAUSED",
                                            stopwatch ? "LIMIT REACHED" : "DONE"};
                    assert(strcmp(lines[1].text, states[state]) == 0);
                    assert(strstr(lines[4].text, destinations[d] ? destinations[d] : "HOME"));
                    assert(strstr(lines[4].text, state == 1U ? "KEY: PAUSE" :
                        state == 2U ? "KEY: RESUME" : stopwatch ? "KEY: START" :
                        state == 3U ? "KEY: RESET" : "KEY: DURATION"));
                    assert(strstr(lines[5].text, !stopwatch && state == 0U ?
                        "HOLD KEY 2s: START" : "HOLD KEY 2s: RESET"));
                    assert(strstr(lines[5].text, stopwatch ?
                        "HOLD BOOT 2s: TIMER" : "HOLD BOOT 2s: STOPWATCH"));
                    if (values[v] == UINT32_MAX) assert(strcmp(lines[2].text, "--:--:--") == 0);
                    render_timing(screen, lines);
                }
            }
        }
    }
    timing_display_lines(false, false, false, false, 60U, 0U, 0U,
        "A destination that is far too long to fit inside the navigation buffer", lines);
    assert(strcmp(lines[3].text, "1m   5m   10m   30m") == 0);
    assert(strcmp(lines[4].text, "BOOT: HOME | KEY: DURATION") == 0);
    render_timing(screen, lines);
    timing_display_done_lines(lines);
    assert(strcmp(lines[1].text, "TIME IS UP") == 0);
    assert(strcmp(lines[2].text, "00:00:00") == 0);
    assert(strcmp(lines[4].text, "BOOT: DISMISS | KEY: DISMISS") == 0);
    assert(lines[5].text[0] == '\0');
    render_timing(screen, lines);
}

static void test_preset_selection(u8g2_t *screen)
{
    timing_display_line_t lines[TIMING_DISPLAY_LINE_COUNT];
    for (uint16_t custom = 0U; custom <= 181U; ++custom) {
        uint16_t options[TIMING_PRESET_CAPACITY];
        const unsigned count = timing_preset_options(custom, options);
        for (unsigned selected = 0U; selected < count; ++selected) {
            timing_display_lines(false, false, false, false,
                (uint32_t)options[selected] * 60U, options[selected], custom, "IMAGE", lines);
            const timing_display_line_t *row = &lines[3];
            assert(row->selected_length > 0U);
            char token[8];
            snprintf(token, sizeof(token), "%um", (unsigned)options[selected]);
            assert(row->selected_length == strlen(token));
            assert(strncmp(row->text + row->selected_start, token, row->selected_length) == 0);
            unsigned label_count = 0U;
            for (const char *p = row->text; *p != '\0'; ++p) {
                if (*p == 'm') ++label_count;
            }
            assert(label_count == count && count <= 5U);
            render_timing(screen, lines);
            /* The selected glyph is black on a solid white, unoutlined box. */
            assert(row->font == u8g2_font_helvB12_tf);
            assert(row->baseline_y == 211);
        }
    }
    timing_display_lines(false, false, false, false, 300U, 5U, 5U, "IMAGE", lines);
    assert(strcmp(lines[2].text, "00:05:00") == 0);
    assert(strcmp(lines[3].text, "1m   5m   10m   30m") == 0);
    assert(lines[3].selected_start == 5U && lines[3].selected_length == 2U);
    render_timing(screen, lines);
    const timing_display_line_t selection_row = lines[3];
    u8g2_SetFontMode(screen, 0U);
    timing_display_draw_line(screen, &selection_row);
    assert(screen->font_decode.is_transparent == 0U && screen->draw_color == 1U);

    /* Replacing a custom option does not fabricate a new current selection. */
    timing_display_lines(false, false, false, false, 1500U, 25U, 7U, "IMAGE", lines);
    assert(strcmp(lines[2].text, "00:25:00") == 0);
    assert(strcmp(lines[3].text, "1m   5m   10m   30m   7m") == 0);
    assert(lines[3].selected_length == 0U);
    render_timing(screen, lines);
    timing_display_lines(false, false, false, false, 300U, 5U, UINT16_MAX, "IMAGE", lines);
    assert(strcmp(lines[3].text, "1m   5m   10m   30m") == 0);
    render_timing(screen, lines);
    timing_display_lines(false, true, false, false, 234U, 5U, 7U, "IMAGE", lines);
    assert(strcmp(lines[3].text, "COUNTDOWN | 5 MIN") == 0);
    assert(lines[3].selected_length == 0U);
    render_timing(screen, lines);
}

static void test_calendar_empty(u8g2_t *screen)
{
    const char *destinations[] = {"HOME", "WEATHER", "MARKET", "CALENDAR",
                                 "IMAGE", "MUSIC", "CHAT", "TIMER", "ENVIRONMENT", NULL};
    for (unsigned valid = 0U; valid <= 1U; ++valid) {
        for (size_t d = 0U; d < sizeof(destinations) / sizeof(destinations[0]); ++d) {
            calendar_empty_display_line_t lines[CALENDAR_EMPTY_DISPLAY_LINE_COUNT];
            calendar_empty_display_lines(valid != 0U, destinations[d], lines);
            assert(strcmp(lines[0].text, "CALENDAR") == 0);
            assert(strcmp(lines[1].text, valid ? "Date is out of range" : "Time is not set") == 0);
            assert(strstr(lines[3].text, destinations[d] ? destinations[d] : "HOME"));
            assert(strstr(lines[3].text, "KEY: SETTINGS"));
            clear_frame(screen);
            int previous_bottom = 0;
            for (unsigned i = 0U; i < CALENDAR_EMPTY_DISPLAY_LINE_COUNT; ++i) {
                draw_line(screen, lines[i].text, lines[i].font, lines[i].baseline_y);
                if (i > 0U) assert(ink_top - previous_bottom >= 8);
                previous_bottom = i == 0U ? 44 : i == 2U ? 250 : ink_bottom;
            }
        }
    }
}

static void print_svg(bool stopwatch)
{
    puts("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"300\" viewBox=\"0 0 400 300\" role=\"img\" aria-labelledby=\"title description\">");
    printf("  <title id=\"title\">%s: native 400 by 300 display</title>\n",
           stopwatch ? "Stopwatch" : "Countdown timer");
    printf("  <desc id=\"description\">Native bitmap font pixels, white on black. %s Hold BOOT for two seconds to change mode without stopping either timer. BOOT opens the next daily page.</desc>\n",
        stopwatch ? "KEY pauses or resumes the stopwatch; hold KEY for two seconds to reset." :
                    "Choose 1, 5, 10 or 30 minutes with KEY; 5 minutes is selected with a reversed native-font token. Hold KEY for two seconds to start.");
    puts("  <rect width=\"400\" height=\"300\" fill=\"#000\"/>");
    puts("  <path fill=\"#fff\" shape-rendering=\"crispEdges\" d=\"");
    for (unsigned y = 0U; y < HEIGHT; ++y) {
        for (unsigned x = 0U; x < WIDTH;) {
            if (!pixels[y][x]) { ++x; continue; }
            const unsigned start = x;
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
    test_formatting();
    test_states_and_navigation(&screen);
    test_preset_selection(&screen);
    test_calendar_empty(&screen);
    if (argc == 2) {
        const bool stopwatch = strcmp(argv[1], "--svg-stopwatch") == 0;
        assert(stopwatch || strcmp(argv[1], "--svg-timer") == 0);
        timing_display_line_t lines[TIMING_DISPLAY_LINE_COUNT];
        timing_display_lines(stopwatch, stopwatch, false, false,
            stopwatch ? 83U : 300U, 5U, 5U, "IMAGE", lines);
        render_timing(&screen, lines);
        print_svg(stopwatch);
    } else {
        assert(argc == 1);
        puts("timing display: native font pixels, states, boundaries, navigation and calendar empty states passed");
    }
    return 0;
}
