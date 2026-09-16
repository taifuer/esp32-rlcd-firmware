#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "market_display_layout.h"

enum { WIDTH = 400, HEIGHT = 300, MARGIN = 12 };
static unsigned char pixels[HEIGHT][WIDTH];
static unsigned char owners[HEIGHT][WIDTH];
static unsigned char current_owner;
static int ink_left, ink_right, ink_top, ink_bottom;

/* Real native U8g2 font decoder output; no desktop surrogate, scaling or
 * clipping that could conceal overlaps or off-panel glyphs. */
void u8g2_DrawHVLine(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y,
                     u8g2_uint_t length, uint8_t direction)
{
    assert(direction == 0);
    assert(y < HEIGHT && x + length <= WIDTH);
    for (unsigned i = 0U; i < length; ++i) {
        pixels[y][x + i] = screen->draw_color != 0;
        if (screen->draw_color == 0) continue;
        assert(owners[y][x + i] == 0U || owners[y][x + i] == current_owner);
        owners[y][x + i] = current_owner;
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

static display_market_t example_market(void)
{
    return (display_market_t){
        .count = 5U,
        .rows = {
            {.name = "上证指数", .valid = true, .price = 4123.56, .percent = 1.28,
             .quote_time = "2026-09-16 15:00:00"},
            {.name = "创业板指", .valid = true, .price = 2876.54, .percent = -0.76,
             .quote_time = "2026-09-16 15:00:00"},
            {.name = "恒生科技", .valid = true, .price = 6543.21, .percent = 0.0,
             .quote_time = "2026-09-16 16:10:00"},
            {.name = "纳斯达克综合", .valid = true, .price = 22456.78, .percent = 0.83,
             .quote_time = "2026-09-16 04:00:00"},
            {.name = "标普500", .valid = true, .price = 6543.21, .percent = -1.24,
             .quote_time = "2026-09-16 04:00:00"},
        },
        .current_time_valid = true, .current_year = 2026U,
        .current_month = 9U, .current_day = 16U,
        .current_hour = 20U, .current_minute = 50U,
        .status_detail = "READY",
    };
}

static void render_market(u8g2_t *screen, const display_market_t *market)
{
    memset(pixels, 0, sizeof(pixels));
    memset(owners, 0, sizeof(owners));
    screen->draw_color = 1;
    u8g2_SetFontPosBaseline(screen);
    u8g2_SetFontMode(screen, 1);
    market_display_line_t lines[MARKET_DISPLAY_LINE_LIMIT];
    const size_t count = market_display_lines(market, lines);
    const size_t rows = market->count < DISPLAY_MARKET_ROW_LIMIT
        ? market->count : DISPLAY_MARKET_ROW_LIMIT;
    assert(count == 8U + rows * 4U);
    assert(count <= MARKET_DISPLAY_LINE_LIMIT);
    for (int x = MARGIN; x < WIDTH - MARGIN; ++x) {
        pixels[42][x] = pixels[250][x] = 1U;
        owners[42][x] = owners[250][x] = 255U;
    }
    int previous_row_bottom = 60;
    int current_row_bottom = 0;
    int previous_footer_bottom = 250;
    for (size_t i = 0U; i < count; ++i) {
        const market_display_line_t *line = &lines[i];
        ink_left = WIDTH; ink_right = -1; ink_top = HEIGHT; ink_bottom = -1;
        current_owner = (unsigned char)(i + 1U);
        market_display_draw_line(screen, line);
        assert(ink_right >= ink_left);
        assert(ink_left >= line->x_left && ink_right < line->x_right);
        assert(ink_left >= MARGIN && ink_right < WIDTH - MARGIN);
        if (i < 2U) {
            assert(ink_top >= 8 && ink_bottom < 42);
        } else if (i < 5U) {
            assert(ink_top > 42 && ink_bottom <= 60);
        } else if (i < 5U + rows * 4U) {
            const size_t cell = (i - 5U) % 4U;
            const size_t row = (i - 5U) / 4U;
            assert(line->baseline_y == 84 + (int)row * 36 + (cell == 3U ? 16 : 0));
            if (cell < 3U) {
                assert(ink_top > previous_row_bottom);
                if (cell == 0U || ink_bottom > current_row_bottom)
                    current_row_bottom = ink_bottom;
            } else {
                assert(ink_top - current_row_bottom >= 3);
                previous_row_bottom = ink_bottom;
            }
            assert(ink_bottom < 250);
        } else {
            assert(ink_top - previous_footer_bottom >= 3);
            assert(ink_bottom < HEIGHT - 2);
            previous_footer_bottom = ink_bottom;
        }
    }
}

static void test_formatting(void)
{
    char text[64];
    assert(display_market_format_price(text, sizeof(text), true, 99999.99));
    assert(strcmp(text, "99,999.99") == 0);
    assert(display_market_format_price(text, sizeof(text), true, 100000.00));
    assert(strcmp(text, "100,000.00") == 0);
    assert(display_market_format_price(text, sizeof(text), true, NAN));
    assert(strcmp(text, "--") == 0);
    assert(display_market_format_price(text, sizeof(text), false, 4000.0));
    assert(strcmp(text, "--") == 0);
    assert(display_market_format_percent(text, sizeof(text), true, 12.345));
    assert(strcmp(text, "+12.35%") == 0);
    assert(display_market_format_percent(text, sizeof(text), true, -12.34));
    assert(strcmp(text, "-12.34%") == 0);
    assert(display_market_format_percent(text, sizeof(text), true, -0.004));
    assert(strcmp(text, "0.00%") == 0);
    assert(display_market_format_percent(text, sizeof(text), true, INFINITY));
    assert(strcmp(text, "--") == 0);
    const char *invalid_dates[] = {NULL, "", "1759126320", "2026-09-16",
        "2026-02-29 12:00:00", "2026-09-16 24:00:00", "2026-09-16 12:60:00",
        "2026-09-16 12:00:60", "2026/09/16 12:00:00", "2026-09-16 12:00:00Z"};
    for (size_t i = 0U; i < sizeof(invalid_dates) / sizeof(invalid_dates[0]); ++i) {
        assert(display_market_format_quote_time(text, sizeof(text), invalid_dates[i]));
        assert(strcmp(text, "--/-- --:-- UTC+8") == 0);
    }
    assert(display_market_format_quote_time(text, sizeof(text), "2024-02-29 23:59:59"));
    assert(strcmp(text, "02/29 23:59 UTC+8") == 0);
    /* An old quote stays visibly old: no local capture time substitution. */
    assert(display_market_format_quote_time(text, sizeof(text), "2025-09-26 15:00:00"));
    assert(strcmp(text, "09/26 15:00 UTC+8") == 0);
    display_market_t market = example_market();
    assert(display_market_format_current_time(text, sizeof(text), &market));
    assert(strcmp(text, "2026/09/16 20:50") == 0);
    market.current_time_valid = false;
    assert(display_market_format_current_time(text, sizeof(text), &market));
    assert(strcmp(text, "----/--/-- --:--") == 0);
    market.saving = true;
    market.refreshing = true;
    assert(display_market_format_status(text, sizeof(text), &market));
    assert(strcmp(text, "SINA | SAVING | REFRESHING") == 0);
    market.refreshing = false;
    market.status_detail = "CACHED / RETRY FAILED";
    assert(display_market_format_status(text, sizeof(text), &market));
    assert(strcmp(text, "SINA | SAVING | CACHED / RETRY FAILED") == 0);
    assert(!display_market_format_price(text, 2U, true, 1234.0));
    assert(text[0] == '\0');
    assert(!display_market_format_percent(NULL, sizeof(text), true, 1.0));
    assert(!display_market_format_quote_time(text, 0U, NULL));
    assert(strcmp(display_interaction_weather_footer_with_market(false),
                  display_interaction_weather_footer()) == 0);
    assert(strstr(display_interaction_weather_footer_with_market(true), "BOOT: MARKET") != NULL);
}

static void test_layout_variants(u8g2_t *screen)
{
    static const char *const NAMES[] = {
        "上证指数", "深证成指", "创业板指", "沪深300", "中证500", "上证50", "科创50",
        "恒生指数", "恒生科技", "纳斯达克综合", "纳斯达克100", "纳指100", "标普500",
        "道琼斯", "日经225", "台湾加权",
    };
    display_market_t market = example_market();
    for (size_t n = 0U; n < sizeof(NAMES) / sizeof(NAMES[0]); ++n) {
        u8g2_SetFont(screen, u8g2_font_wqy16_t_gb2312);
        assert(u8g2_GetUTF8Width(screen, NAMES[n]) <= 118U);
        for (size_t i = 0U; i < DISPLAY_MARKET_ROW_LIMIT; ++i) {
            market.rows[i].name = NAMES[n];
            market.rows[i].price = i == 0U ? 999999.99 : 99999.99;
            market.rows[i].percent = i == 1U ? -12.34 : i == 2U ? 0.0 : 12.34;
            market.rows[i].stale = i == 4U;
        }
        render_market(screen, &market);
    }
    for (size_t count = 0U; count <= 6U; ++count) {
        market.count = count;
        for (unsigned mask = 0U; mask < 16U; ++mask) {
            market.current_time_valid = (mask & 1U) != 0U;
            market.saving = (mask & 2U) != 0U;
            market.refreshing = (mask & 4U) != 0U;
            market.status_detail = "CACHED / RETRY FAILED";
            for (size_t i = 0U; i < DISPLAY_MARKET_ROW_LIMIT; ++i)
                market.rows[i].valid = (mask & 8U) != 0U;
            render_market(screen, &market);
        }
    }
    market = example_market();
    market.rows[0].price = 999999999.0; /* Too wide: whole --, never clipped. */
    market.rows[0].percent = 999999999.0;
    render_market(screen, &market);
    market.rows[0].percent = DBL_MAX;
    render_market(screen, &market);
    market_display_line_t lines[MARKET_DISPLAY_LINE_LIMIT];
    market.rows[4].stale = true;
    market_display_lines(&market, lines);
    assert(strcmp(lines[24].text, "09/16 04:00 UTC+8 *") == 0);
    market.refreshing = true;
    market_display_line_t refreshing[MARKET_DISPLAY_LINE_LIMIT];
    market_display_lines(&market, refreshing);
    for (size_t i = 5U; i < 25U; ++i) {
        assert(strcmp(lines[i].text, refreshing[i].text) == 0);
        assert(lines[i].baseline_y == refreshing[i].baseline_y);
    }
}

static void print_svg(void)
{
    puts("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"300\" viewBox=\"0 0 400 300\" role=\"img\" aria-labelledby=\"title description\">");
    puts("  <title id=\"title\">Market: five index quotes</title>");
    puts("  <desc id=\"description\">Illustrative values, not a live snapshot. Native 400 by 300 monochrome font pixels. Each row shows a Chinese index name, price, signed percent change, and its own quote time in UTC+8. The header is device-local RTC time. BOOT opens Calendar, KEY opens Settings, and holding KEY for two seconds refreshes without leaving this page.</desc>");
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
    test_layout_variants(&screen);
    if (argc == 2 && strcmp(argv[1], "--svg") == 0) {
        const display_market_t market = example_market();
        render_market(&screen, &market);
        print_svg();
    } else {
        assert(argc == 1);
        puts("market layout: native font ink, 16 names, five fixed rows, signed values, stale timestamps and 112 states passed");
    }
    return 0;
}
