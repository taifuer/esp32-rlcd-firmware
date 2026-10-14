#include <assert.h>
#include <stdlib.h>
#include "environment_display_layout.h"

enum { WIDTH = 400, HEIGHT = 300 };
static unsigned char pixels[HEIGHT][WIDTH];
static unsigned chart_lines, chart_points;

void u8g2_DrawPixel(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y)
{
    assert(x >= 12 && x < WIDTH - 12 && y < HEIGHT - 2);
    pixels[y][x] = screen->draw_color != 0;
    ++chart_points;
}

void u8g2_DrawHVLine(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y,
                     u8g2_uint_t length, uint8_t direction)
{
    assert(direction == 0);
    for (unsigned i = 0; i < length; ++i) u8g2_DrawPixel(screen, x + i, y);
}

void u8g2_DrawHLine(u8g2_t *screen, u8g2_uint_t x, u8g2_uint_t y,
                    u8g2_uint_t width)
{
    u8g2_DrawHVLine(screen, x, y, width, 0);
}

void u8g2_DrawLine(u8g2_t *screen, u8g2_uint_t x0, u8g2_uint_t y0,
                   u8g2_uint_t x1, u8g2_uint_t y1)
{
    ++chart_lines;
    int x = x0, y = y0;
    const int dx = abs((int)x1 - x), sx = x < x1 ? 1 : -1;
    const int dy = -abs((int)y1 - y), sy = y < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        u8g2_DrawPixel(screen, (u8g2_uint_t)x, (u8g2_uint_t)y);
        if (x == x1 && y == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

uint8_t u8g2_IsIntersection(u8g2_t *screen, u8g2_uint_t x0, u8g2_uint_t y0,
                            u8g2_uint_t x1, u8g2_uint_t y1)
{
    (void)screen; (void)x1; (void)y1;
    return x0 < WIDTH && y0 < HEIGHT;
}

static void render(u8g2_t *screen, environment_history_snapshot_t *history,
                    bool fahrenheit)
{
    memset(pixels, 0, sizeof(pixels));
    chart_lines = chart_points = 0;
    screen->draw_color = 1;
    u8g2_SetFontMode(screen, 1);
    u8g2_SetFontPosBaseline(screen);
    environment_display_render(screen, history, fahrenheit, "ENVIRONMENT");
    assert(chart_points > 100);
}

static environment_history_snapshot_t history;

static void sample_history(void)
{
    memset(&history, 0, sizeof(history));
    history.captured_monotonic_ms = ENVIRONMENT_HISTORY_WINDOW_MS;
    history.has_samples = true;
    history.count = ENVIRONMENT_HISTORY_CAPACITY;
    history.temperature_min_centi_c = 2200;
    history.temperature_max_centi_c = 2600;
    history.humidity_min_centi_percent = 4500;
    history.humidity_max_centi_percent = 5500;
    for (size_t i = 0; i < history.count; ++i) {
        environment_history_point_t *point = &history.points[i];
        point->start_monotonic_ms = (int64_t)(i + 1) * ENVIRONMENT_HISTORY_BUCKET_MS;
        point->sample_count = 10;
        const int triangle = (int)(i % 96) < 48 ? (int)(i % 96) : 96 - (int)(i % 96);
        point->temperature_mean_centi_c = (int16_t)(2200 + triangle * 400 / 48);
        point->humidity_mean_centi_percent = (uint16_t)(5500 - triangle * 1000 / 48);
    }
}

static void sample_partial_history(void)
{
    sample_history();
    history.captured_monotonic_ms = ENVIRONMENT_HISTORY_WINDOW_MS / 2;
    history.count = ENVIRONMENT_HISTORY_CAPACITY / 2 + 1;
    for (size_t i = 0; i < history.count; ++i)
        history.points[i].start_monotonic_ms = (int64_t)i * ENVIRONMENT_HISTORY_BUCKET_MS;
}

static void assert_chart_extents(u8g2_t *screen, bool humidity,
                                  int expected_left, int expected_right)
{
    memset(pixels, 0, sizeof(pixels));
    chart_lines = chart_points = 0;
    screen->draw_color = 1;
    environment_display_chart(screen, &history, humidity);
    int left = WIDTH, right = -1;
    const int top = humidity ? ENV_HUM_TOP : ENV_TEMP_TOP;
    const int bottom = humidity ? ENV_HUM_BOTTOM : ENV_TEMP_BOTTOM;
    for (int y = top; y <= bottom; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            if (!pixels[y][x]) continue;
            if (x < left) left = x;
            if (x > right) right = x;
        }
    }
    assert(left == expected_left);
    assert(right == expected_right);
}

static void test_time_axis(u8g2_t *screen)
{
    assert(environment_display_axis(NULL).start_monotonic_ms == 0);
    assert(!environment_display_axis(NULL).rolling);
    assert(environment_display_x(0, 0) == ENV_CHART_LEFT);
    assert(environment_display_x(0, ENVIRONMENT_HISTORY_WINDOW_MS) == ENV_CHART_RIGHT);
    assert(environment_display_x(1000, 0) == ENV_CHART_LEFT);
    assert(environment_display_x(0, 2 * ENVIRONMENT_HISTORY_WINDOW_MS) == ENV_CHART_RIGHT);

    sample_partial_history();
    environment_display_axis_t axis = environment_display_axis(&history);
    assert(!axis.rolling);
    assert(axis.start_monotonic_ms == 0);
    assert_chart_extents(screen, false, ENV_CHART_LEFT, 200);
    assert_chart_extents(screen, true, ENV_CHART_LEFT, 200);
    /* A partial first bucket can precede service initialization by milliseconds;
     * it belongs at the left edge, not outside the plot or at the right. */
    history.recording_started_monotonic_ms = 507;
    axis = environment_display_axis(&history);
    assert(axis.start_monotonic_ms == 507);
    assert(environment_display_x(axis.start_monotonic_ms,
                                  history.points[0].start_monotonic_ms) == ENV_CHART_LEFT);
    history.count = 1;
    history.captured_monotonic_ms = 1000;
    assert_chart_extents(screen, false, ENV_CHART_LEFT, ENV_CHART_LEFT);
    assert_chart_extents(screen, true, ENV_CHART_LEFT, ENV_CHART_LEFT);

    sample_history();
    axis = environment_display_axis(&history);
    assert(axis.rolling);
    assert(axis.start_monotonic_ms == 0);
    history.captured_monotonic_ms += 7200000;
    axis = environment_display_axis(&history);
    assert(axis.rolling);
    assert(axis.start_monotonic_ms == 7200000);
    assert(environment_display_x(axis.start_monotonic_ms,
                                  history.captured_monotonic_ms) == ENV_CHART_RIGHT);

    /* Restore uses the earliest valid sample, with the entire powered-off
     * interval still present. The live session does not collapse the gap. */
    sample_history();
    history.count = 4;
    history.captured_monotonic_ms = 300000;
    history.restored_history = true;
    history.points[0].start_monotonic_ms = -14400000;
    history.points[1].start_monotonic_ms = -14100000;
    history.points[2].start_monotonic_ms = 0;
    history.points[3].start_monotonic_ms = 300000;
    history.points[0].restored = history.points[1].restored = true;
    axis = environment_display_axis(&history);
    assert(!axis.rolling);
    assert(axis.start_monotonic_ms == -14400000);
    assert_chart_extents(screen, false, ENV_CHART_LEFT, 81);
    assert(chart_lines == 2);
    assert_chart_extents(screen, true, ENV_CHART_LEFT, 81);
    assert(chart_lines == 2);
    history.points[0].sample_count = history.points[1].sample_count = 0;
    axis = environment_display_axis(&history);
    assert(axis.start_monotonic_ms == 0);
    history.points[0].sample_count = 10;
    history.points[0].start_monotonic_ms =
        history.captured_monotonic_ms - ENVIRONMENT_HISTORY_WINDOW_MS;
    assert(environment_display_axis(&history).start_monotonic_ms == 0);
    history.points[0].start_monotonic_ms = history.captured_monotonic_ms + 1;
    assert(environment_display_axis(&history).start_monotonic_ms == 0);
}

int main(int argc, char **argv)
{
    u8g2_t screen = {0};
    char text[16];
    environment_display_number(text, sizeof(text), -55);
    assert(strcmp(text, "-0.6") == 0);
    environment_display_number(text, sizeof(text), 10000);
    assert(strcmp(text, "100.0") == 0);
    assert(environment_display_temperature(-4000, true) == -4000);
    assert(environment_display_temperature(12500, true) == 25700);
    render(&screen, NULL, false);
    render(&screen, &history, true);
    test_time_axis(&screen);
    sample_history();
    render(&screen, &history, false);
    assert(chart_lines == 574);
    history.points[100].sample_count = 0;
    render(&screen, &history, false);
    assert(chart_lines == 570);
    history.points[200].failure_count = 1;
    render(&screen, &history, true);
    assert(chart_lines == 566);
    history.points[0].restored = true;
    render(&screen, &history, false);
    assert(chart_lines == 564);
    history.temperature_min_centi_c = -4000;
    history.temperature_max_centi_c = 12500;
    history.humidity_min_centi_percent = 0;
    history.humidity_max_centi_percent = 10000;
    history.restored_history = true;
    render(&screen, &history, true);
    history.count = 1;
    history.captured_monotonic_ms = 300000;
    history.restored_history = false;
    render(&screen, &history, false);
    assert(chart_lines == 0);
    if (argc == 2) {
        assert(strcmp(argv[1], "--svg") == 0);
        sample_partial_history();
        render(&screen, &history, false);
        memset(pixels, 0, sizeof(pixels));
        environment_display_render(&screen, &history, false, "MARKET");
        puts("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"300\" viewBox=\"0 0 400 300\" role=\"img\" aria-labelledby=\"title description\">");
        puts("<title id=\"title\">Environment: native 400 by 300 display</title>");
        puts("<desc id=\"description\">Illustrative 12-hour temperature and humidity history growing from the left on a fixed 24-hour scale. Future time remains blank. Native firmware bitmap fonts; not actual sensor measurements.</desc>");
        puts("<rect width=\"400\" height=\"300\" fill=\"#000\"/><path fill=\"#fff\" shape-rendering=\"crispEdges\" d=\"");
        for (unsigned y = 0; y < HEIGHT; ++y) {
            for (unsigned x = 0; x < WIDTH;) {
                if (!pixels[y][x]) { ++x; continue; }
                const unsigned start = x;
                while (x < WIDTH && pixels[y][x]) ++x;
                printf("M%u %uh%uv1h-%uz", start, y, x - start, x - start);
            }
            putchar('\n');
        }
        puts("\"/></svg>");
    } else {
        assert(argc == 1);
        puts("environment display: left-growing and rolling axes, gaps, native fonts, extremes, units and 400x300 bounds passed");
    }
    return 0;
}
