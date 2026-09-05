#include "display.h"

#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "calendar_month.h"
#include "display_interaction_model.h"
#include "network_credentials.h"
#include "qrcode.h"
#include "u8g2.h"
#include "u8g2_st7305.h"
#include "voice_display_model.h"

static u8g2_st7305_t s_lcd;
static u8g2_t *s_u8g2;

static const char *const WEEKDAYS[] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六",
};

static const char *const CALENDAR_WEEKDAYS[] = {
    "一", "二", "三", "四", "五", "六", "日",
};

enum {
    DASHBOARD_SIDE_MARGIN = 12,
    TOP_ROW_BASELINE_Y = 31,
    TOP_DIVIDER_Y = 55,
    TIME_BASELINE_Y = 179,
    BOTTOM_DIVIDER_Y = 244,
    ENVIRONMENT_BASELINE_Y = 280,
    COMFORT_FACE_CENTER_Y = 272,
    STATUS_GROUP_WIDTH = 86,
    WIFI_ICON_WIDTH = 18,
    WIFI_ICON_HEIGHT = 14,
    WIFI_ICON_Y = 20,
    STATUS_GROUP_GAP = 7,
    SETUP_SIDE_MARGIN = 12,
    SETUP_TITLE_BASELINE_Y = 34,
    SETUP_DIVIDER_Y = 44,
    SETUP_QR_AREA_TOP = 48,
    SETUP_QR_AREA_SIZE = 170,
    SETUP_QR_QUIET_MODULES = 4,
    SETUP_QR_MAX_SCALE = 5,
    SYSTEM_PAGE_COUNT = 4,
    SYSTEM_SIDE_MARGIN = 12,
    SYSTEM_TITLE_BASELINE_Y = 32,
    SYSTEM_DIVIDER_Y = 44,
    SYSTEM_LABEL_X = 18,
    SYSTEM_VALUE_X = 116,
    SYSTEM_FOOTER_DIVIDER_Y = 250,
    SYSTEM_FOOTER_BASELINE_Y = 280,
    DAILY_SIDE_MARGIN = 12,
    DAILY_FOOTER_DIVIDER_Y = 250,
    DAILY_FOOTER_BASELINE_Y = 280,
    WEATHER_HEADER_BASELINE_Y = 28,
    WEATHER_HEADER_DIVIDER_Y = 42,
    WEATHER_CURRENT_DIVIDER_Y = 126,
    WEATHER_FORECAST_TOP_Y = 136,
    WEATHER_SOURCE_BASELINE_Y = 270,
    WEATHER_FOOTER_BASELINE_Y = 293,
    CALENDAR_SIDE_MARGIN = 11,
    CALENDAR_HEADER_BASELINE_Y = 30,
    CALENDAR_HEADER_DIVIDER_Y = 43,
    CALENDAR_WEEKDAY_BASELINE_Y = 67,
    CALENDAR_WEEKDAY_DIVIDER_Y = 75,
    CALENDAR_GRID_TOP_Y = 78,
    CALENDAR_ROW_HEIGHT = 28,
    CALENDAR_COLUMN_WIDTH = 54,
};

/*
 * An 18 x 14 pixel status glyph designed for the reflective monochrome panel.
 * Filled two-pixel bands stay legible at arm's length and visually balance the
 * adjacent battery fill better than one-pixel circle outlines.
 */
static const uint8_t WIFI_STATUS_BITMAP[] = {
    0xe0, 0x1f, 0x00, /* .....########..... */
    0x78, 0x78, 0x00, /* ...####....####... */
    0x0e, 0xc0, 0x01, /* .###..........###. */
    0x03, 0x00, 0x03, /* ##..............## */
    0x00, 0x00, 0x00, /* .................. */
    0xc0, 0x0f, 0x00, /* ......######...... */
    0x70, 0x38, 0x00, /* ....###....###.... */
    0x18, 0x60, 0x00, /* ...##........##... */
    0x00, 0x00, 0x00, /* .................. */
    0x00, 0x03, 0x00, /* ........##........ */
    0x80, 0x07, 0x00, /* .......####....... */
    0x80, 0x07, 0x00, /* .......####....... */
    0x00, 0x03, 0x00, /* ........##........ */
    0x00, 0x00, 0x00, /* .................. */
};

static void draw_centered(int baseline_y, const char *text)
{
    int x = (BOARD_DISPLAY_WIDTH - (int)u8g2_GetStrWidth(s_u8g2, text)) / 2;
    if (x < 0) {
        x = 0;
    }
    u8g2_DrawStr(s_u8g2, x, baseline_y, text);
}

static void draw_utf8_right_aligned(int right, int baseline_y, const char *text)
{
    int x = right - (int)u8g2_GetUTF8Width(s_u8g2, text);
    if (x < 0) {
        x = 0;
    }
    u8g2_DrawUTF8(s_u8g2, x, baseline_y, text);
}

static void draw_setup_line(int baseline_y, const char *text)
{
    const int available_width = BOARD_DISPLAY_WIDTH - 2 * SETUP_SIDE_MARGIN;

    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    if ((int)u8g2_GetStrWidth(s_u8g2, text) > available_width) {
        u8g2_SetFont(s_u8g2, u8g2_font_5x8_tf);
    }
    draw_centered(baseline_y, text);
}

static void draw_system_row(int baseline_y, const char *label,
                            const char *value)
{
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    u8g2_DrawStr(s_u8g2, SYSTEM_LABEL_X, baseline_y, label);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    u8g2_DrawStr(s_u8g2, SYSTEM_VALUE_X, baseline_y, value);
}

static void draw_system_header(const char *title, uint8_t page)
{
    char position[8];
    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    draw_centered(SYSTEM_TITLE_BASELINE_Y, title);
    if (page > 0U) {
        snprintf(position, sizeof(position), "%u/%u", page,
                 SYSTEM_PAGE_COUNT);
        u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
        u8g2_DrawStr(s_u8g2,
                     BOARD_DISPLAY_WIDTH - SYSTEM_SIDE_MARGIN -
                         (int)u8g2_GetStrWidth(s_u8g2, position),
                     SYSTEM_TITLE_BASELINE_Y, position);
    }
    u8g2_DrawHLine(s_u8g2, SYSTEM_SIDE_MARGIN, SYSTEM_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * SYSTEM_SIDE_MARGIN);
}

static void draw_system_footer(const char *text)
{
    u8g2_DrawHLine(s_u8g2, SYSTEM_SIDE_MARGIN,
                   SYSTEM_FOOTER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * SYSTEM_SIDE_MARGIN);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(SYSTEM_FOOTER_BASELINE_Y, text);
}

static void draw_settings_footer(bool manual_saving_requested)
{
    u8g2_DrawHLine(s_u8g2, SYSTEM_SIDE_MARGIN,
                   SYSTEM_FOOTER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * SYSTEM_SIDE_MARGIN);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(270,
                  display_interaction_settings_navigation_footer());
    draw_centered(
        294,
        display_interaction_settings_action_footer(
            manual_saving_requested));
}

static void draw_daily_footer(const char *text)
{
    u8g2_DrawHLine(s_u8g2, DAILY_SIDE_MARGIN,
                   DAILY_FOOTER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DAILY_SIDE_MARGIN);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(DAILY_FOOTER_BASELINE_Y, text);
}

static void draw_weather_cloud(int center_x, int center_y, int scale)
{
    const int left = center_x - 15 * scale;
    const int right = center_x + 16 * scale;
    const int base_y = center_y + 5 * scale;

    u8g2_DrawLine(s_u8g2, left, base_y,
                  left, center_y);
    u8g2_DrawLine(s_u8g2, left, center_y,
                  center_x - 11 * scale, center_y - 4 * scale);
    u8g2_DrawLine(s_u8g2, center_x - 11 * scale,
                  center_y - 4 * scale,
                  center_x - 7 * scale, center_y - 5 * scale);
    u8g2_DrawLine(s_u8g2, center_x - 7 * scale,
                  center_y - 5 * scale,
                  center_x - 4 * scale, center_y - 10 * scale);
    u8g2_DrawLine(s_u8g2, center_x - 4 * scale,
                  center_y - 10 * scale,
                  center_x + 1 * scale, center_y - 12 * scale);
    u8g2_DrawLine(s_u8g2, center_x + 1 * scale,
                  center_y - 12 * scale,
                  center_x + 6 * scale, center_y - 10 * scale);
    u8g2_DrawLine(s_u8g2, center_x + 6 * scale,
                  center_y - 10 * scale,
                  center_x + 9 * scale, center_y - 5 * scale);
    u8g2_DrawLine(s_u8g2, center_x + 9 * scale,
                  center_y - 5 * scale,
                  center_x + 13 * scale, center_y - 4 * scale);
    u8g2_DrawLine(s_u8g2, center_x + 13 * scale,
                  center_y - 4 * scale,
                  right, center_y - scale);
    u8g2_DrawLine(s_u8g2, right, center_y - scale,
                  right, base_y);
    u8g2_DrawHLine(s_u8g2, left, base_y, right - left + 1);
}

static void draw_weather_sun(int center_x, int center_y, int scale)
{
    const int radius = 5 * scale;
    const int ray_inner = 8 * scale;
    const int ray_outer = 12 * scale;

    u8g2_DrawCircle(s_u8g2, center_x, center_y, radius,
                    U8G2_DRAW_ALL);
    if (scale > 1) {
        u8g2_DrawCircle(s_u8g2, center_x, center_y, radius - 1,
                        U8G2_DRAW_ALL);
    }
    u8g2_DrawLine(s_u8g2, center_x, center_y - ray_inner,
                  center_x, center_y - ray_outer);
    u8g2_DrawLine(s_u8g2, center_x, center_y + ray_inner,
                  center_x, center_y + ray_outer);
    u8g2_DrawLine(s_u8g2, center_x - ray_inner, center_y,
                  center_x - ray_outer, center_y);
    u8g2_DrawLine(s_u8g2, center_x + ray_inner, center_y,
                  center_x + ray_outer, center_y);
    u8g2_DrawLine(s_u8g2, center_x - 6 * scale,
                  center_y - 6 * scale,
                  center_x - 9 * scale, center_y - 9 * scale);
    u8g2_DrawLine(s_u8g2, center_x + 6 * scale,
                  center_y - 6 * scale,
                  center_x + 9 * scale, center_y - 9 * scale);
    u8g2_DrawLine(s_u8g2, center_x - 6 * scale,
                  center_y + 6 * scale,
                  center_x - 9 * scale, center_y + 9 * scale);
    u8g2_DrawLine(s_u8g2, center_x + 6 * scale,
                  center_y + 6 * scale,
                  center_x + 9 * scale, center_y + 9 * scale);
}

static void draw_weather_snowflake(int center_x, int center_y, int scale)
{
    const int radius = 3 * scale;
    u8g2_DrawHLine(s_u8g2, center_x - radius, center_y,
                   2 * radius + 1);
    u8g2_DrawVLine(s_u8g2, center_x, center_y - radius,
                   2 * radius + 1);
    u8g2_DrawLine(s_u8g2, center_x - 2 * scale,
                  center_y - 2 * scale,
                  center_x + 2 * scale, center_y + 2 * scale);
    u8g2_DrawLine(s_u8g2, center_x - 2 * scale,
                  center_y + 2 * scale,
                  center_x + 2 * scale, center_y - 2 * scale);
}

static void draw_weather_icon(int center_x, int center_y, int scale,
                              display_weather_icon_t icon)
{
    switch (icon) {
    case DISPLAY_WEATHER_ICON_CLEAR:
        draw_weather_sun(center_x, center_y, scale);
        break;
    case DISPLAY_WEATHER_ICON_CLOUDY:
        draw_weather_sun(center_x - 8 * scale,
                         center_y - 6 * scale, scale);
        draw_weather_cloud(center_x + 2 * scale,
                           center_y + 3 * scale, scale);
        break;
    case DISPLAY_WEATHER_ICON_WIND:
        u8g2_DrawLine(s_u8g2, center_x - 15 * scale,
                      center_y - 6 * scale,
                      center_x + 9 * scale, center_y - 6 * scale);
        u8g2_DrawLine(s_u8g2, center_x + 9 * scale,
                      center_y - 6 * scale,
                      center_x + 14 * scale, center_y - 2 * scale);
        u8g2_DrawLine(s_u8g2, center_x - 11 * scale, center_y,
                      center_x + 15 * scale, center_y);
        u8g2_DrawLine(s_u8g2, center_x - 15 * scale,
                      center_y + 6 * scale,
                      center_x + 7 * scale, center_y + 6 * scale);
        u8g2_DrawLine(s_u8g2, center_x + 7 * scale,
                      center_y + 6 * scale,
                      center_x + 12 * scale, center_y + 3 * scale);
        break;
    case DISPLAY_WEATHER_ICON_RAIN:
        draw_weather_cloud(center_x, center_y - 4 * scale, scale);
        for (int index = -1; index <= 1; ++index) {
            const int x = center_x + index * 8 * scale;
            u8g2_DrawLine(s_u8g2, x, center_y + 4 * scale,
                          x - 3 * scale, center_y + 11 * scale);
        }
        break;
    case DISPLAY_WEATHER_ICON_THUNDER:
        draw_weather_cloud(center_x, center_y - 4 * scale, scale);
        u8g2_DrawLine(s_u8g2, center_x + 2 * scale,
                      center_y + 3 * scale,
                      center_x - 3 * scale, center_y + 10 * scale);
        u8g2_DrawLine(s_u8g2, center_x - 3 * scale,
                      center_y + 10 * scale,
                      center_x + 2 * scale, center_y + 10 * scale);
        u8g2_DrawLine(s_u8g2, center_x + 2 * scale,
                      center_y + 10 * scale,
                      center_x - 4 * scale, center_y + 17 * scale);
        break;
    case DISPLAY_WEATHER_ICON_SNOW:
        draw_weather_cloud(center_x, center_y - 5 * scale, scale);
        draw_weather_snowflake(center_x - 7 * scale,
                               center_y + 9 * scale, scale);
        draw_weather_snowflake(center_x + 7 * scale,
                               center_y + 9 * scale, scale);
        break;
    case DISPLAY_WEATHER_ICON_FOG:
        draw_weather_cloud(center_x, center_y - 6 * scale, scale);
        u8g2_DrawHLine(s_u8g2, center_x - 14 * scale,
                       center_y + 5 * scale, 24 * scale);
        u8g2_DrawHLine(s_u8g2, center_x - 9 * scale,
                       center_y + 10 * scale, 24 * scale);
        break;
    case DISPLAY_WEATHER_ICON_UNKNOWN:
    default:
        u8g2_DrawCircle(s_u8g2, center_x, center_y, 11 * scale,
                        U8G2_DRAW_ALL);
        u8g2_SetFont(s_u8g2, scale > 1 ? u8g2_font_helvB24_tf
                                      : u8g2_font_6x13_tf);
        const int text_width = (int)u8g2_GetStrWidth(s_u8g2, "?");
        u8g2_DrawStr(s_u8g2, center_x - text_width / 2,
                     center_y + (scale > 1 ? 8 : 4), "?");
        break;
    }
}

static void draw_weather_utf8_in_region(
    int left, int width, int top, int bottom,
    int baseline_y, const char *text)
{
    if (text == NULL || width <= 0) {
        return;
    }
    int x = left + (width - (int)u8g2_GetUTF8Width(s_u8g2, text)) / 2;
    if (x < left) {
        x = left;
    }
    u8g2_SetClipWindow(s_u8g2, left, top, left + width - 1, bottom);
    u8g2_DrawUTF8(s_u8g2, x, baseline_y, text);
    u8g2_SetMaxClipWindow(s_u8g2);
}

static void draw_image_footer(const char *navigation)
{
    u8g2_DrawHLine(s_u8g2, DAILY_SIDE_MARGIN,
                   DAILY_FOOTER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DAILY_SIDE_MARGIN);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(270, navigation);
    draw_centered(294, display_interaction_image_action_footer());
}

static void format_version(char *buffer, size_t capacity,
                           const char *version)
{
    if (buffer == NULL || capacity == 0U) {
        return;
    }
    if (version == NULL || version[0] == '\0') {
        snprintf(buffer, capacity, "--");
    } else if (version[0] == 'v' || version[0] == 'V') {
        snprintf(buffer, capacity, "%s", version);
    } else {
        snprintf(buffer, capacity, "v%s", version);
    }
}

static void draw_online_update_modal(const char *heading,
                                     const char *primary,
                                     const char *secondary,
                                     const char *footer)
{
    u8g2_DrawFrame(s_u8g2, 24, 64, BOARD_DISPLAY_WIDTH - 48, 164);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    draw_centered(108, heading);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    draw_centered(158, primary);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(199, secondary);
    draw_system_footer(footer);
}

static void draw_online_update_progress(
    const display_online_update_status_t *status)
{
    enum {
        PROGRESS_LEFT = 40,
        PROGRESS_TOP = 138,
        PROGRESS_WIDTH = 320,
        PROGRESS_HEIGHT = 22,
        PROGRESS_INNER_WIDTH = PROGRESS_WIDTH - 4,
    };
    char text[48];
    const uint8_t percent = status->progress_percent > 100U
                                ? 100U
                                : status->progress_percent;

    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    snprintf(text, sizeof(text), "DOWNLOADING %u%%", percent);
    draw_centered(105, text);
    u8g2_DrawFrame(s_u8g2, PROGRESS_LEFT, PROGRESS_TOP,
                   PROGRESS_WIDTH, PROGRESS_HEIGHT);
    const int fill_width = (int)percent * PROGRESS_INNER_WIDTH / 100;
    if (fill_width > 0) {
        u8g2_DrawBox(s_u8g2, PROGRESS_LEFT + 2, PROGRESS_TOP + 2,
                     fill_width, PROGRESS_HEIGHT - 4);
    }

    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    if (status->total_bytes > 0U) {
        snprintf(text, sizeof(text), "%u / %u KiB",
                 (unsigned)((status->downloaded_bytes + 1023U) / 1024U),
                 (unsigned)((status->total_bytes + 1023U) / 1024U));
    } else {
        snprintf(text, sizeof(text), "%u KiB received",
                 (unsigned)((status->downloaded_bytes + 1023U) / 1024U));
    }
    draw_centered(198, text);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(225, "Keep power connected");
    draw_system_footer("DO NOT POWER OFF");
}

typedef struct {
    bool rendered;
    int area_left;
    int area_top;
    int area_size;
    int quiet_modules;
    int max_scale;
    bool standard_polarity;
} display_qr_context_t;

static void draw_qr(esp_qrcode_handle_t qrcode, void *user_data)
{
    display_qr_context_t *context = user_data;
    if (context == NULL) {
        return;
    }
    const int module_count = esp_qrcode_get_size(qrcode);
    const int total_modules = module_count + 2 * context->quiet_modules;
    int scale = total_modules > 0 ? context->area_size / total_modules : 0;
    if (scale > context->max_scale) {
        scale = context->max_scale;
    }
    if (module_count <= 0 || scale <= 0) {
        return;
    }

    const int pixel_size = total_modules * scale;
    const int left = context->area_left + (context->area_size - pixel_size) / 2;
    const int top = context->area_top + (context->area_size - pixel_size) / 2;
    const int data_left = left + context->quiet_modules * scale;
    const int data_top = top + context->quiet_modules * scale;

    if (context->standard_polarity) {
        u8g2_SetDrawColor(s_u8g2, 0);
    }
    for (int row = 0; row < module_count; ++row) {
        for (int column = 0; column < module_count; ++column) {
            if (esp_qrcode_get_module(qrcode, column, row)) {
                u8g2_DrawBox(s_u8g2, data_left + column * scale,
                             data_top + row * scale, scale, scale);
            }
        }
    }
    u8g2_SetDrawColor(s_u8g2, 1);
    context->rendered = true;
}

static bool draw_qr_payload(const char *payload, display_qr_context_t *context,
                            int correction)
{
    if (payload == NULL || context == NULL) {
        return false;
    }
    context->rendered = false;
    if (context->standard_polarity) {
        u8g2_SetDrawColor(s_u8g2, 1);
        u8g2_DrawBox(s_u8g2, context->area_left, context->area_top,
                     context->area_size, context->area_size);
    }
    esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
    config.display_func_with_cb = draw_qr;
    config.max_qrcode_version = 10;
    config.qrcode_ecc_level = correction;
    config.user_data = context;
    (void)esp_qrcode_generate(&config, payload);
    if (!context->rendered && context->standard_polarity) {
        u8g2_SetDrawColor(s_u8g2, 0);
        u8g2_DrawBox(s_u8g2, context->area_left, context->area_top,
                     context->area_size, context->area_size);
        u8g2_SetDrawColor(s_u8g2, 1);
    }
    return context->rendered;
}

static void draw_comfort_face(int center_x, int center_y,
                              display_environment_comfort_t comfort)
{
    if (comfort == DISPLAY_ENVIRONMENT_COMFORT_UNKNOWN) {
        return;
    }

    u8g2_DrawBox(s_u8g2, center_x - 9, center_y - 8, 3, 3);
    u8g2_DrawBox(s_u8g2, center_x + 7, center_y - 8, 3, 3);

    if (comfort == DISPLAY_ENVIRONMENT_COMFORT_COMFORTABLE) {
        u8g2_DrawLine(s_u8g2, center_x - 9, center_y,
                      center_x - 4, center_y + 5);
        u8g2_DrawLine(s_u8g2, center_x - 8, center_y,
                      center_x - 3, center_y + 5);
        u8g2_DrawHLine(s_u8g2, center_x - 3, center_y + 5, 7);
        u8g2_DrawLine(s_u8g2, center_x + 3, center_y + 5,
                      center_x + 8, center_y);
        u8g2_DrawLine(s_u8g2, center_x + 4, center_y + 5,
                      center_x + 9, center_y);
    } else if (comfort == DISPLAY_ENVIRONMENT_COMFORT_FAIR) {
        u8g2_DrawBox(s_u8g2, center_x - 9, center_y + 3, 19, 2);
    } else if (comfort ==
               DISPLAY_ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT) {
        u8g2_DrawLine(s_u8g2, center_x - 9, center_y + 6,
                      center_x - 4, center_y + 1);
        u8g2_DrawLine(s_u8g2, center_x - 8, center_y + 6,
                      center_x - 3, center_y + 1);
        u8g2_DrawHLine(s_u8g2, center_x - 3, center_y + 1, 7);
        u8g2_DrawLine(s_u8g2, center_x + 3, center_y + 1,
                      center_x + 8, center_y + 6);
        u8g2_DrawLine(s_u8g2, center_x + 4, center_y + 1,
                      center_x + 9, center_y + 6);
    }
}

static void draw_utf8_centered_in_region(int left, int width, int baseline_y,
                                         const char *text)
{
    int x = left + (width - (int)u8g2_GetUTF8Width(s_u8g2, text)) / 2;
    if (x < left) {
        x = left;
    }
    u8g2_DrawUTF8(s_u8g2, x, baseline_y, text);
}

static int measure_voice_text(const char *text, void *context)
{
    return u8g2_GetUTF8Width((u8g2_t *)context, text);
}

static void draw_voice_turn(const display_voice_status_t *status,
                            int baseline_y);

static void draw_voice_text_window(const char *text,
                                   int first_baseline_y,
                                   int line_spacing,
                                   uint8_t max_lines)
{
    enum {
        VOICE_TEXT_MARGIN = 18,
    };
    const int width = BOARD_DISPLAY_WIDTH - 2 * VOICE_TEXT_MARGIN;
    voice_display_text_window_t window;
    if (!voice_display_build_text_window(
            text != NULL ? text : "", width, max_lines,
            measure_voice_text, s_u8g2, &window)) {
        return;
    }
    for (uint8_t index = 0U; index < window.count; ++index) {
        const char *line = voice_display_text_window_line(&window, index);
        if (line != NULL) {
            u8g2_DrawUTF8(s_u8g2, VOICE_TEXT_MARGIN,
                          first_baseline_y + index * line_spacing,
                          line);
        }
    }
}

static void draw_cloud_response(const display_voice_status_t *status,
                                const char *title,
                                const char *footer)
{
    draw_centered(78, title);
    draw_voice_turn(status, 103);
    u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
    draw_voice_text_window(status->response, 132, 27,
                           VOICE_DISPLAY_TEXT_MAX_LINES);
    draw_system_footer(footer);
}

static void draw_voice_turn(const display_voice_status_t *status,
                            int baseline_y)
{
    char label[16];
    if (status == NULL || !status->cloud_mode ||
        !voice_display_format_turn(label, sizeof(label),
                                   status->turn_number,
                                   status->max_turns)) {
        return;
    }
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(baseline_y, label);
}

static void draw_battery(int x, int y, bool valid, uint8_t percent)
{
    const int body_width = 28;
    const int body_height = 14;
    const int inner_width = body_width - 4;

    u8g2_DrawFrame(s_u8g2, x, y, body_width, body_height);
    u8g2_DrawBox(s_u8g2, x + body_width, y + 4, 3, 6);
    if (valid) {
        const int fill_width = (inner_width * percent + 99) / 100;
        if (fill_width > 0) {
            u8g2_DrawBox(s_u8g2, x + 2, y + 2, fill_width, body_height - 4);
        }
    }

    char text[8];
    if (valid) {
        snprintf(text, sizeof(text), "%u%%", percent);
    } else {
        snprintf(text, sizeof(text), "--%%");
    }
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    u8g2_DrawStr(s_u8g2, x + body_width + 8, y + 12, text);
}

static void draw_usb_power(int left, int width, int baseline_y)
{
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    int x = left + (width - (int)u8g2_GetStrWidth(s_u8g2, "USB")) / 2;
    if (x < left) {
        x = left;
    }
    u8g2_DrawStr(s_u8g2, x, baseline_y, "USB");
}

static void draw_wifi_status(int x, int y, display_network_state_t state)
{
    if (state == DISPLAY_NETWORK_CONNECTED) {
        u8g2_DrawXBM(s_u8g2, x, y, WIFI_ICON_WIDTH, WIFI_ICON_HEIGHT,
                     WIFI_STATUS_BITMAP);
    }
}

static void draw_top_row(const display_dashboard_t *dashboard)
{
    char date_text[48];
    const char *lunar_text;
    const char *weekday_text;

    if (dashboard->time_valid) {
        snprintf(date_text, sizeof(date_text), "%04u年%u月%u日", dashboard->year,
                 dashboard->month, dashboard->day);
        lunar_text = dashboard->lunar_valid && dashboard->lunar_text != NULL
                         ? dashboard->lunar_text
                         : "--月--";
        weekday_text = dashboard->weekday <= 6U ? WEEKDAYS[dashboard->weekday] : "星期-";
    } else {
        snprintf(date_text, sizeof(date_text), "----年--月--日");
        lunar_text = "--月--";
        weekday_text = "星期-";
    }

    u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
    const int date_width = (int)u8g2_GetUTF8Width(s_u8g2, date_text);
    const int lunar_width = (int)u8g2_GetUTF8Width(s_u8g2, lunar_text);
    const int weekday_width = (int)u8g2_GetUTF8Width(s_u8g2, weekday_text);
    const int usable_width = BOARD_DISPLAY_WIDTH - 2 * DASHBOARD_SIDE_MARGIN;
    int remaining_width = usable_width - date_width - lunar_width - weekday_width -
                          STATUS_GROUP_WIDTH;
    if (remaining_width < 0) {
        remaining_width = 0;
    }

    const int base_gap = remaining_width / 3;
    const int extra_gap_pixels = remaining_width % 3;
    const int first_gap = base_gap + (extra_gap_pixels > 0 ? 1 : 0);
    const int second_gap = base_gap + (extra_gap_pixels > 1 ? 1 : 0);
    int x = DASHBOARD_SIDE_MARGIN;

    u8g2_DrawUTF8(s_u8g2, x, TOP_ROW_BASELINE_Y, date_text);
    x += date_width + first_gap;
    u8g2_DrawUTF8(s_u8g2, x, TOP_ROW_BASELINE_Y, lunar_text);
    x += lunar_width + second_gap;
    u8g2_DrawUTF8(s_u8g2, x, TOP_ROW_BASELINE_Y, weekday_text);
    x += weekday_width + base_gap;
    draw_wifi_status(x, WIFI_ICON_Y, dashboard->network_state);
    const int power_left = x + WIFI_ICON_WIDTH + STATUS_GROUP_GAP;
    const int power_width = STATUS_GROUP_WIDTH - WIFI_ICON_WIDTH -
                            STATUS_GROUP_GAP;
    if (dashboard->usb_data_host_connected) {
        draw_usb_power(power_left, power_width, TOP_ROW_BASELINE_Y + 1);
    } else {
        draw_battery(power_left, 20, dashboard->battery_valid,
                     dashboard->battery_percent);
    }
}

esp_err_t display_init(void)
{
    u8g2_st7305_config_t config = u8g2_st7305_default_config();
    config.mosi_io = BOARD_DISPLAY_MOSI_GPIO;
    config.sclk_io = BOARD_DISPLAY_SCLK_GPIO;
    config.dc_io = BOARD_DISPLAY_DC_GPIO;
    config.cs_io = BOARD_DISPLAY_CS_GPIO;
    config.reset_io = BOARD_DISPLAY_RESET_GPIO;
    config.spi_host = BOARD_DISPLAY_SPI_HOST;
    config.clock_hz = BOARD_DISPLAY_SPI_CLOCK_HZ;
    config.tile_buf_height = U8G2_ST7305_TILE_BUF_FULL;
    config.rotation = U8G2_R1;
    config.prefer_psram = true;

    esp_err_t error = u8g2_st7305_init(&s_lcd, &config);
    if (error == ESP_OK) {
        s_u8g2 = u8g2_st7305_get_u8g2(&s_lcd);
    }
    return error;
}

void display_show_status(const char *title, const char *detail)
{
    if (s_u8g2 == NULL) {
        return;
    }

    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_DrawFrame(s_u8g2, 3, 3, BOARD_DISPLAY_WIDTH - 6, BOARD_DISPLAY_HEIGHT - 6);

    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    draw_centered(130, title != NULL ? title : "RLCD FIRMWARE");
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    draw_centered(170, detail != NULL ? detail : "Starting");
    u8g2_SendBuffer(s_u8g2);
}

void display_show_hold_prompt(const char *title,
                              uint8_t seconds_remaining)
{
    if (s_u8g2 == NULL) {
        return;
    }

    char countdown[32];
    snprintf(countdown, sizeof(countdown), "KEEP HOLDING: %us",
             seconds_remaining);

    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_DrawFrame(s_u8g2, 3, 3, BOARD_DISPLAY_WIDTH - 6,
                   BOARD_DISPLAY_HEIGHT - 6);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    draw_centered(118, title != NULL ? title : "KEEP HOLDING");
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    draw_centered(170, countdown);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(218, "RELEASE TO CANCEL");
    u8g2_SendBuffer(s_u8g2);
}

void display_show_network_setup(const char *ssid, const char *password, const char *url)
{
    if (s_u8g2 == NULL) {
        return;
    }

    char line[80];
    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_DrawFrame(s_u8g2, 3, 3, BOARD_DISPLAY_WIDTH - 6, BOARD_DISPLAY_HEIGHT - 6);

    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    draw_centered(SETUP_TITLE_BASELINE_Y, "WI-FI SETUP");
    u8g2_DrawHLine(s_u8g2, SETUP_SIDE_MARGIN, SETUP_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * SETUP_SIDE_MARGIN);

    bool qr_rendered = false;
    if (ssid != NULL && password != NULL) {
        char payload[NETWORK_SETUP_QR_PAYLOAD_CAPACITY];
        if (network_setup_wifi_qr_payload(ssid, password, payload,
                                          sizeof(payload))) {
            display_qr_context_t context = {
                .area_left = (BOARD_DISPLAY_WIDTH - SETUP_QR_AREA_SIZE) / 2,
                .area_top = SETUP_QR_AREA_TOP,
                .area_size = SETUP_QR_AREA_SIZE,
                .quiet_modules = SETUP_QR_QUIET_MODULES,
                .max_scale = SETUP_QR_MAX_SCALE,
                .standard_polarity = true,
            };
            qr_rendered = draw_qr_payload(payload, &context,
                                          ESP_QRCODE_ECC_MED);
        }
        memset(payload, 0, sizeof(payload));
    }

    snprintf(line, sizeof(line), "SSID: %s", ssid != NULL ? ssid : "-");
    draw_setup_line(qr_rendered ? 238 : 119, line);
    snprintf(line, sizeof(line), "PASS: %s", password != NULL ? password : "-");
    draw_setup_line(qr_rendered ? 258 : 158, line);
    snprintf(line, sizeof(line), "OPEN: %s | BOOT: OFFLINE",
             url != NULL ? url : "192.168.4.1");
    draw_setup_line(qr_rendered ? 280 : 197, line);

    if (!qr_rendered) {
        u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
        draw_centered(244, "Connect with a phone or computer");
    }
    u8g2_SendBuffer(s_u8g2);
}

void display_show_settings_portal_ready(const char *ssid,
                                        const char *password,
                                        const char *url)
{
    if (s_u8g2 == NULL) {
        return;
    }

    char line[96];
    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_DrawFrame(s_u8g2, 3, 3, BOARD_DISPLAY_WIDTH - 6,
                   BOARD_DISPLAY_HEIGHT - 6);

    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    draw_centered(SETUP_TITLE_BASELINE_Y, "DEVICE SETTINGS");
    u8g2_DrawHLine(s_u8g2, SETUP_SIDE_MARGIN, SETUP_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * SETUP_SIDE_MARGIN);

    bool qr_rendered = false;
    if (ssid != NULL && password != NULL) {
        char payload[NETWORK_SETUP_QR_PAYLOAD_CAPACITY];
        if (network_setup_wifi_qr_payload(ssid, password, payload,
                                          sizeof(payload))) {
            display_qr_context_t context = {
                .area_left = (BOARD_DISPLAY_WIDTH - SETUP_QR_AREA_SIZE) / 2,
                .area_top = SETUP_QR_AREA_TOP,
                .area_size = SETUP_QR_AREA_SIZE,
                .quiet_modules = SETUP_QR_QUIET_MODULES,
                .max_scale = SETUP_QR_MAX_SCALE,
                .standard_polarity = true,
            };
            qr_rendered = draw_qr_payload(payload, &context,
                                          ESP_QRCODE_ECC_MED);
        }
        memset(payload, 0, sizeof(payload));
    }

    snprintf(line, sizeof(line), "SSID: %s", ssid != NULL ? ssid : "-");
    draw_setup_line(qr_rendered ? 238 : 119, line);
    snprintf(line, sizeof(line), "PASS: %s",
             password != NULL ? password : "-");
    draw_setup_line(qr_rendered ? 258 : 158, line);
    snprintf(line, sizeof(line), "OPEN: %s | BOOT: CLOSE",
             url != NULL ? url : "192.168.4.1");
    draw_setup_line(qr_rendered ? 280 : 197, line);

    if (!qr_rendered) {
        u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
        draw_centered(244, "Connect and open the settings page");
    }
    u8g2_SendBuffer(s_u8g2);
}

void display_show_dashboard(const display_dashboard_t *dashboard)
{
    if (s_u8g2 == NULL || dashboard == NULL) {
        return;
    }

    char text[96];
    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);

    draw_top_row(dashboard);
    u8g2_DrawHLine(s_u8g2, DASHBOARD_SIDE_MARGIN, TOP_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DASHBOARD_SIDE_MARGIN);

    if (dashboard->time_valid && dashboard->show_seconds) {
        snprintf(text, sizeof(text), "%02u:%02u:%02u",
                 dashboard->hour, dashboard->minute, dashboard->second);
    } else if (dashboard->time_valid) {
        snprintf(text, sizeof(text), "%02u:%02u",
                 dashboard->hour, dashboard->minute);
    } else if (dashboard->show_seconds) {
        snprintf(text, sizeof(text), "--:--:--");
    } else {
        snprintf(text, sizeof(text), "--:--");
    }
    u8g2_SetFont(s_u8g2, u8g2_font_logisoso78_tn);
    draw_centered(TIME_BASELINE_Y, text);

    u8g2_DrawHLine(s_u8g2, DASHBOARD_SIDE_MARGIN, BOTTOM_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DASHBOARD_SIDE_MARGIN);

    char temperature_text[24];
    if (dashboard->environment_valid) {
        const double temperature = dashboard->temperature_fahrenheit
                                       ? (double)dashboard->temperature_c *
                                                 9.0 / 5.0 + 32.0
                                       : (double)dashboard->temperature_c;
        snprintf(temperature_text, sizeof(temperature_text), "%.1f °%c",
                 temperature,
                 dashboard->temperature_fahrenheit ? 'F' : 'C');
    } else {
        snprintf(temperature_text, sizeof(temperature_text), "--.- °%c",
                 dashboard->temperature_fahrenheit ? 'F' : 'C');
    }

    char humidity_text[24];
    if (dashboard->environment_valid) {
        snprintf(humidity_text, sizeof(humidity_text), "%.0f %%",
                 (double)dashboard->humidity_percent);
    } else {
        snprintf(humidity_text, sizeof(humidity_text), "-- %%");
    }
    u8g2_SetFont(s_u8g2, u8g2_font_logisoso20_tf);
    draw_utf8_centered_in_region(0, BOARD_DISPLAY_WIDTH / 2,
                                 ENVIRONMENT_BASELINE_Y, temperature_text);
    draw_comfort_face(BOARD_DISPLAY_WIDTH / 2, COMFORT_FACE_CENTER_Y,
                      dashboard->environment_valid
                          ? dashboard->environment_comfort
                          : DISPLAY_ENVIRONMENT_COMFORT_UNKNOWN);
    draw_utf8_centered_in_region(BOARD_DISPLAY_WIDTH / 2, BOARD_DISPLAY_WIDTH / 2,
                                 ENVIRONMENT_BASELINE_Y, humidity_text);
    u8g2_SendBuffer(s_u8g2);
}

void display_show_weather(const display_weather_t *weather)
{
    if (s_u8g2 == NULL || weather == NULL) {
        return;
    }

    char current_date[32];
    char source[80];
    char temperature[16];
    char feels_like[48];
    const bool failed =
        weather->status_detail != NULL &&
        weather->status_detail[0] != '\0';
    const char *location =
        weather->location != NULL && weather->location[0] != '\0'
            ? weather->location
            : "天气";
    const char *condition =
        weather->data_available && weather->condition_text != NULL &&
                weather->condition_text[0] != '\0'
            ? weather->condition_text
            : (weather->refreshing
                   ? "正在获取"
                   : (failed ? "获取失败" : "等待获取"));

    if (!display_weather_format_current_date(
            current_date, sizeof(current_date), weather->current_date_valid,
            weather->current_year, weather->current_month,
            weather->current_day)) {
        snprintf(current_date, sizeof(current_date), "--月--日  周-");
    }
    if (weather->data_available) {
        if (!display_weather_format_source(
                source, sizeof(source), weather->freshness,
                weather->update_time_valid,
                weather->update_month, weather->update_day,
                weather->update_hour, weather->update_minute)) {
            snprintf(source, sizeof(source), "QWeather | 缓存时间未知");
        }
        if (weather->refreshing) {
            const size_t length = strlen(source);
            snprintf(source + length, sizeof(source) - length, " | 更新中");
        }
    } else if (weather->refreshing) {
        snprintf(source, sizeof(source), "QWeather | 正在获取");
    } else if (failed) {
        snprintf(source, sizeof(source), "QWeather | 获取失败");
    } else {
        snprintf(source, sizeof(source), "QWeather | 等待获取");
    }
    if (!weather->data_available || !display_weather_format_temperature(
            temperature, sizeof(temperature),
            weather->temperature_tenths_celsius,
            weather->temperature_fahrenheit, true)) {
        snprintf(temperature, sizeof(temperature), "--");
    }
    char feels_temperature[16];
    if (!weather->data_available || !display_weather_format_temperature(
            feels_temperature, sizeof(feels_temperature),
            weather->feels_like_tenths_celsius,
            weather->temperature_fahrenheit, true)) {
        snprintf(feels_temperature, sizeof(feels_temperature), "--");
    }
    if (!weather->data_available || failed) {
        snprintf(feels_like, sizeof(feels_like), "%s",
                 failed
                     ? weather->status_detail
                     : "等待天气数据");
    } else {
        snprintf(feels_like, sizeof(feels_like), "体感 %s",
                 feels_temperature);
    }

    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);

    u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
    const int date_width = (int)u8g2_GetUTF8Width(s_u8g2, current_date);
    const int date_x = BOARD_DISPLAY_WIDTH - DAILY_SIDE_MARGIN - date_width;
    u8g2_DrawUTF8(s_u8g2, date_x, WEATHER_HEADER_BASELINE_Y, current_date);

    const int location_right = date_x - 12;
    if (location_right > DAILY_SIDE_MARGIN) {
        u8g2_SetClipWindow(s_u8g2, DAILY_SIDE_MARGIN, 0,
                           location_right, WEATHER_HEADER_DIVIDER_Y - 1);
        u8g2_DrawUTF8(s_u8g2, DAILY_SIDE_MARGIN,
                      WEATHER_HEADER_BASELINE_Y, location);
        u8g2_SetMaxClipWindow(s_u8g2);
    }
    u8g2_DrawHLine(s_u8g2, DAILY_SIDE_MARGIN,
                   WEATHER_HEADER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DAILY_SIDE_MARGIN);

    draw_weather_icon(
        50, 81, 2,
        display_weather_icon_from_qweather_code(
            weather->condition_code));
    u8g2_SetFont(s_u8g2, u8g2_font_logisoso42_tf);
    draw_weather_utf8_in_region(88, 150, 47, 118, 103,
                                temperature);
    u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
    draw_weather_utf8_in_region(245, 143, 49, 86, 76,
                                condition);
    draw_weather_utf8_in_region(245, 143, 87, 118, 106,
                                feels_like);

    u8g2_DrawHLine(s_u8g2, DAILY_SIDE_MARGIN,
                   WEATHER_CURRENT_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DAILY_SIDE_MARGIN);

    size_t day_count = weather->forecast_day_count;
    if (day_count > DISPLAY_WEATHER_FORECAST_DAY_LIMIT) {
        day_count = DISPLAY_WEATHER_FORECAST_DAY_LIMIT;
    }
    if (day_count == 0U) {
        u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
        draw_weather_utf8_in_region(
            DAILY_SIDE_MARGIN,
            BOARD_DISPLAY_WIDTH - 2 * DAILY_SIDE_MARGIN,
            WEATHER_FORECAST_TOP_Y, DAILY_FOOTER_DIVIDER_Y - 1,
            194, "暂无预报");
    }

    for (size_t index = 0U; index < day_count; ++index) {
        const int left = (int)index * BOARD_DISPLAY_WIDTH /
                         (int)day_count;
        const int right = (int)(index + 1U) * BOARD_DISPLAY_WIDTH /
                          (int)day_count;
        const int width = right - left;
        const int center_x = left + width / 2;
        const display_weather_forecast_day_t *day =
            &weather->forecast[index];
        char high[12];
        char low[12];
        char high_low[32];
        char precipitation[24];
        char day_label[16];

        if (index > 0U) {
            u8g2_DrawVLine(s_u8g2, left, WEATHER_FORECAST_TOP_Y,
                           DAILY_FOOTER_DIVIDER_Y -
                               WEATHER_FORECAST_TOP_Y - 6);
        }
        u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
        if (!display_weather_format_day_label(
                day_label, sizeof(day_label), day->date,
                weather->current_date_valid, weather->current_year,
                weather->current_month, weather->current_day)) {
            snprintf(day_label, sizeof(day_label), "--");
        }
        draw_weather_utf8_in_region(left, width,
                                    WEATHER_FORECAST_TOP_Y, 157,
                                    153, day_label);

        draw_weather_icon(
            center_x, 174, 1,
            display_weather_icon_from_qweather_code(
                day->condition_code));
        u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
        draw_weather_utf8_in_region(left + 4, width - 8, 186, 204,
                                    201,
                                    day->condition_text != NULL &&
                                            day->condition_text[0] != '\0'
                                        ? day->condition_text
                                        : "--");

        if (!display_weather_format_temperature(
                high, sizeof(high),
                day->temperature_high_tenths_celsius,
                weather->temperature_fahrenheit, false)) {
            snprintf(high, sizeof(high), "--");
        }
        if (!display_weather_format_temperature(
                low, sizeof(low),
                day->temperature_low_tenths_celsius,
                weather->temperature_fahrenheit, false)) {
            snprintf(low, sizeof(low), "--");
        }
        snprintf(high_low, sizeof(high_low), "%s / %s", high, low);
        u8g2_SetFont(s_u8g2, u8g2_font_logisoso16_tf);
        draw_weather_utf8_in_region(left + 4, width - 8, 205, 225,
                                    221, high_low);

        const uint8_t probability =
            day->precipitation_probability_percent > 100U
                ? 100U
                : day->precipitation_probability_percent;
        snprintf(precipitation, sizeof(precipitation),
                 "降水 %u%%", probability);
        u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
        draw_weather_utf8_in_region(left + 4, width - 8, 226, 248,
                                    243, precipitation);
    }

    u8g2_DrawHLine(s_u8g2, DAILY_SIDE_MARGIN,
                   DAILY_FOOTER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DAILY_SIDE_MARGIN);
    u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
    draw_weather_utf8_in_region(
        DAILY_SIDE_MARGIN, BOARD_DISPLAY_WIDTH - 2 * DAILY_SIDE_MARGIN,
        DAILY_FOOTER_DIVIDER_Y + 1, WEATHER_SOURCE_BASELINE_Y + 4,
        WEATHER_SOURCE_BASELINE_Y, source);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(WEATHER_FOOTER_BASELINE_Y,
                  display_interaction_weather_footer());
    u8g2_SendBuffer(s_u8g2);
}

void display_show_calendar(const display_dashboard_t *dashboard,
                           bool image_available)
{
    if (s_u8g2 == NULL || dashboard == NULL) {
        return;
    }
    if (!dashboard->time_valid) {
        display_show_status("CALENDAR", "Time is not set");
        return;
    }

    calendar_month_info_t month = {0};
    if (!calendar_month_info(dashboard->year, dashboard->month, &month)) {
        display_show_status("CALENDAR", "Date is out of range");
        return;
    }

    char header_left[32];
    char header_right[96];
    snprintf(header_left, sizeof(header_left), "%04u年%u月",
             dashboard->year, dashboard->month);
    snprintf(header_right, sizeof(header_right), "%s  %s  %02u:%02u",
             dashboard->lunar_valid && dashboard->lunar_text != NULL
                 ? dashboard->lunar_text
                 : "--月--",
             dashboard->weekday <= 6U ? WEEKDAYS[dashboard->weekday] : "星期-",
             dashboard->hour, dashboard->minute);

    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);

    u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
    u8g2_DrawUTF8(s_u8g2, CALENDAR_SIDE_MARGIN,
                  CALENDAR_HEADER_BASELINE_Y, header_left);
    draw_utf8_right_aligned(BOARD_DISPLAY_WIDTH - CALENDAR_SIDE_MARGIN,
                            CALENDAR_HEADER_BASELINE_Y, header_right);
    u8g2_DrawHLine(s_u8g2, CALENDAR_SIDE_MARGIN,
                   CALENDAR_HEADER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * CALENDAR_SIDE_MARGIN);

    for (uint8_t column = 0U; column < 7U; ++column) {
        draw_utf8_centered_in_region(
            CALENDAR_SIDE_MARGIN + column * CALENDAR_COLUMN_WIDTH,
            CALENDAR_COLUMN_WIDTH, CALENDAR_WEEKDAY_BASELINE_Y,
            CALENDAR_WEEKDAYS[column]);
    }
    u8g2_DrawHLine(s_u8g2, CALENDAR_SIDE_MARGIN,
                   CALENDAR_WEEKDAY_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * CALENDAR_SIDE_MARGIN);

    const uint8_t first_column = (uint8_t)((month.first_weekday + 6U) % 7U);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB18_tf);
    for (uint8_t day = 1U; day <= month.days; ++day) {
        const uint8_t position = (uint8_t)(first_column + day - 1U);
        const uint8_t row = position / 7U;
        const uint8_t column = position % 7U;
        const int center_x = CALENDAR_SIDE_MARGIN +
                             column * CALENDAR_COLUMN_WIDTH +
                             CALENDAR_COLUMN_WIDTH / 2;
        const int baseline_y = CALENDAR_GRID_TOP_Y + 21 +
                               row * CALENDAR_ROW_HEIGHT;
        char day_text[4];
        snprintf(day_text, sizeof(day_text), "%u", day);
        const int text_x = center_x -
                           (int)u8g2_GetStrWidth(s_u8g2, day_text) / 2;
        if (day == dashboard->day) {
            u8g2_DrawDisc(s_u8g2, center_x, baseline_y - 7, 13,
                          U8G2_DRAW_ALL);
            u8g2_SetDrawColor(s_u8g2, 0);
            u8g2_DrawStr(s_u8g2, text_x, baseline_y, day_text);
            u8g2_SetDrawColor(s_u8g2, 1);
        } else {
            u8g2_DrawStr(s_u8g2, text_x, baseline_y, day_text);
        }
    }

    draw_daily_footer(
        display_interaction_calendar_footer(image_available));
    u8g2_SendBuffer(s_u8g2);
}

void display_show_monochrome_image(
    const uint8_t bitmap[MONO_IMAGE_BITMAP_BYTES],
    size_t selected_index, size_t image_count)
{
    if (s_u8g2 == NULL || bitmap == NULL) {
        return;
    }

    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_SetBitmapMode(s_u8g2, 0);
    u8g2_DrawBitmap(s_u8g2, 0, 0, MONO_IMAGE_ROW_BYTES,
                    MONO_IMAGE_HEIGHT, bitmap);

    u8g2_SetDrawColor(s_u8g2, 0);
    u8g2_DrawBox(s_u8g2, 0, DAILY_FOOTER_DIVIDER_Y,
                 BOARD_DISPLAY_WIDTH,
                 BOARD_DISPLAY_HEIGHT - DAILY_FOOTER_DIVIDER_Y);
    u8g2_SetDrawColor(s_u8g2, 1);
    char footer[64];
    if (display_interaction_format_image_navigation(
            footer, sizeof(footer), selected_index, image_count)) {
        draw_image_footer(footer);
    }
    u8g2_SendBuffer(s_u8g2);
}

void display_show_image_delete_confirmation(
    const uint8_t bitmap[MONO_IMAGE_BITMAP_BYTES],
    size_t selected_index, size_t image_count, bool delete_ready)
{
    if (s_u8g2 == NULL || bitmap == NULL) {
        return;
    }

    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_SetBitmapMode(s_u8g2, 0);
    u8g2_DrawBitmap(s_u8g2, 0, 0, MONO_IMAGE_ROW_BYTES,
                    MONO_IMAGE_HEIGHT, bitmap);

    u8g2_SetDrawColor(s_u8g2, 0);
    u8g2_DrawBox(s_u8g2, 0, DAILY_FOOTER_DIVIDER_Y,
                 BOARD_DISPLAY_WIDTH,
                 BOARD_DISPLAY_HEIGHT - DAILY_FOOTER_DIVIDER_Y);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_DrawHLine(s_u8g2, DAILY_SIDE_MARGIN,
                   DAILY_FOOTER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DAILY_SIDE_MARGIN);

    char prompt[48];
    if (image_count > 0U) {
        if (selected_index >= image_count) {
            selected_index = 0U;
        }
        snprintf(prompt, sizeof(prompt), "DELETE IMAGE? | %u/%u",
                 (unsigned)(selected_index + 1U),
                 (unsigned)image_count);
    } else {
        snprintf(prompt, sizeof(prompt), "DELETE IMAGE?");
    }
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    draw_centered(271, prompt);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(294, delete_ready
                           ? "BOOT: CANCEL | KEY: DELETE"
                           : "RELEASE KEY | BOOT: CANCEL");
    u8g2_SendBuffer(s_u8g2);
}

void display_show_image_delete_status(display_image_delete_status_t status)
{
    switch (status) {
    case DISPLAY_IMAGE_DELETE_DELETING:
        display_show_status("DELETING", "Keep power on");
        break;
    case DISPLAY_IMAGE_DELETE_DELETED:
        display_show_status("DELETED", "Image removed");
        break;
    case DISPLAY_IMAGE_DELETE_FAILED:
        display_show_status("DELETE FAILED", "Image kept; check microSD");
        break;
    default:
        break;
    }
}

void display_show_system_status(const display_system_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    char value[64];
    draw_system_header("STATUS", 1U);

    if (!status->rtc_ready) {
        snprintf(value, sizeof(value), "NOT FOUND");
    } else {
        snprintf(value, sizeof(value), "%s | BACKUP %s",
                 status->time_valid ? "OK" : "INVALID",
                 status->rtc_backup_state != NULL
                     ? status->rtc_backup_state
                     : "NOT READY");
    }
    draw_system_row(72, "RTC", value);

    if (!status->sensor_ready) {
        snprintf(value, sizeof(value), "NOT FOUND");
    } else if (!status->environment_valid) {
        snprintf(value, sizeof(value), "READ ERROR");
    } else {
        const double temperature = status->temperature_fahrenheit
                                       ? (double)status->temperature_c *
                                                 9.0 / 5.0 + 32.0
                                       : (double)status->temperature_c;
        snprintf(value, sizeof(value), "%s | %.1f %c | %.0f %%",
                 status->environment_stale ? "STALE" : "OK",
                 temperature,
                 status->temperature_fahrenheit ? 'F' : 'C',
                 (double)status->humidity_percent);
    }
    draw_system_row(108, "SENSOR", value);

    if (!status->battery_ready) {
        snprintf(value, sizeof(value), "%sNOT READY",
                 status->usb_data_host_connected ? "USB | " : "");
    } else if (!status->battery_valid) {
        snprintf(value, sizeof(value), "%sREAD ERROR",
                 status->usb_data_host_connected ? "USB | " : "");
    } else if (status->usb_data_host_connected) {
        snprintf(value, sizeof(value), "USB | %u mV",
                 status->battery_voltage_mv);
    } else {
        snprintf(value, sizeof(value), "OK | %u %% | %u mV",
                 status->battery_percent, status->battery_voltage_mv);
    }
    draw_system_row(144, "BATTERY", value);

    if (status->last_sync_valid) {
        snprintf(value, sizeof(value), "%02u-%02u %02u:%02u | %s",
                 status->last_sync_month, status->last_sync_day,
                 status->last_sync_hour, status->last_sync_minute,
                 status->time_sync_state != NULL
                     ? status->time_sync_state
                     : "OK");
    } else {
        snprintf(value, sizeof(value), "%s",
                 status->time_sync_state != NULL
                     ? status->time_sync_state
                     : "NOT SYNCED");
    }
    draw_system_row(180, "TIME SYNC", value);

    snprintf(value, sizeof(value), "%s",
             status->wifi_state != NULL ? status->wifi_state
                                        : "NOT READY");
    draw_system_row(216, "WI-FI", value);

    draw_system_footer(display_interaction_status_footer());
    u8g2_SendBuffer(s_u8g2);
}

void display_show_voice(const display_voice_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    char value[48];
    const char *detail = status->detail != NULL ? status->detail : "";
    draw_system_header("CHAT", 2U);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);

    switch (status->state) {
    case DISPLAY_VOICE_STATE_WAITING_FOR_RELEASE:
        draw_centered(102, "RELEASE KEY");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(158, status->cloud_mode
                               ? "AI chat is ready for your question"
                               : "Listening starts after release");
        draw_centered(198, status->cloud_mode
                               ? "Up to 10 seconds per turn"
                               : "One command, up to 5 seconds");
        draw_voice_turn(status, 226);
        draw_system_footer("BOOT: CANCEL");
        break;
    case DISPLAY_VOICE_STATE_PREPARING:
        draw_centered(112, "PREPARING");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(170, "Starting offline recognition");
        draw_centered(210, "No network or cloud service");
        draw_system_footer("BOOT: CANCEL");
        break;
    case DISPLAY_VOICE_STATE_LISTENING:
        snprintf(value, sizeof(value), "LISTENING %u/%us",
                 (unsigned)((status->elapsed_ms + 999U) / 1000U),
                 (unsigned)(status->max_listening_ms / 1000U));
        draw_centered(96, value);
        draw_voice_turn(status, 120);
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        if (status->cloud_mode) {
            draw_centered(150, "Speak Chinese or English");
            u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
            draw_voice_text_window(status->transcript, 190, 30, 2U);
        } else {
            draw_centered(158, "Say one command clearly");
            u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
            draw_utf8_centered_in_region(
                0, BOARD_DISPLAY_WIDTH, 199,
                "回到主页  打开日历  查看状态");
            draw_utf8_centered_in_region(
                0, BOARD_DISPLAY_WIDTH, 226,
                "打开图片  打开设置  取消");
        }
        draw_system_footer("KEY: DONE | BOOT: CANCEL");
        break;
    case DISPLAY_VOICE_STATE_RECOGNIZING:
        draw_centered(112, "RECOGNIZING");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(174, "Matching locally on this device");
        draw_system_footer("BOOT: CANCEL");
        break;
    case DISPLAY_VOICE_STATE_CLOUD_CONNECTING:
        draw_centered(102, "CONNECTING");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(164, "Preparing AI chat");
        draw_centered(202, "Keep holding KEY or release now");
        draw_voice_turn(status, 226);
        draw_system_footer("BOOT: CANCEL");
        break;
    case DISPLAY_VOICE_STATE_CLOUD_THINKING:
        draw_centered(84, "THINKING");
        draw_voice_turn(status, 110);
        u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
        draw_voice_text_window(status->transcript, 156, 38, 2U);
        draw_system_footer("BOOT: CANCEL");
        break;
    case DISPLAY_VOICE_STATE_CLOUD_SPEAKING:
        draw_cloud_response(
            status, "SPEAKING",
            status->turn_number > 0U &&
                    status->turn_number < status->max_turns
                ? display_interaction_chat_next_turn_footer()
                : "BOOT: CANCEL");
        break;
    case DISPLAY_VOICE_STATE_CLOUD_ADVANCING:
        draw_centered(92, "NEXT TURN");
        draw_voice_turn(status, 122);
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(178, "Finishing the current response");
        draw_system_footer("BOOT: CANCEL");
        break;
    case DISPLAY_VOICE_STATE_CLOUD_FOLLOW_UP:
        draw_cloud_response(status, "FOLLOW-UP",
                            "KEY: CONTINUE | BOOT: END");
        break;
    case DISPLAY_VOICE_STATE_CLOUD_COMPLETED:
        draw_cloud_response(status, "DONE",
                            voice_display_feedback_footer());
        break;
    case DISPLAY_VOICE_STATE_SUCCEEDED:
        draw_centered(112, "UNDERSTOOD");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(178, detail);
        draw_system_footer("Opening page...");
        break;
    case DISPLAY_VOICE_STATE_NO_VOICE:
        draw_centered(112, "NO SPEECH");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(174, "No clear speech was detected");
        draw_system_footer(voice_display_feedback_footer());
        break;
    case DISPLAY_VOICE_STATE_NOT_UNDERSTOOD:
        draw_centered(112, "NOT UNDERSTOOD");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(174, "Try one of the commands shown");
        draw_system_footer(voice_display_feedback_footer());
        break;
    case DISPLAY_VOICE_STATE_TARGET_UNAVAILABLE:
        draw_centered(112, "NOT AVAILABLE");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(174, detail);
        draw_system_footer(voice_display_feedback_footer());
        break;
    case DISPLAY_VOICE_STATE_UNAVAILABLE:
        draw_centered(105, "CHAT UNAVAILABLE");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(164, detail);
        u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
        draw_centered(210, "Clock and other pages still work");
        draw_system_footer(status->session_active
                               ? voice_display_feedback_footer()
                               : display_interaction_chat_footer());
        break;
    case DISPLAY_VOICE_STATE_CANCELLED:
        draw_centered(112, "CANCELLED");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(174, "Temporary audio buffer cleared");
        draw_system_footer(voice_display_feedback_footer());
        break;
    case DISPLAY_VOICE_STATE_FAILED:
        draw_centered(112, "CHAT FAILED");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(174, detail);
        draw_system_footer(voice_display_feedback_footer());
        break;
    case DISPLAY_VOICE_STATE_READY:
    default:
        draw_centered(92, voice_display_mode_title(status->cloud_mode));
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(
            145,
            voice_display_ready_prompt(status->cloud_mode,
                                       status->engine_available));
        if (status->cloud_mode) {
            if (status->max_turns > 0U) {
                snprintf(value, sizeof(value),
                         "Chinese / English | Up to %u turns",
                         status->max_turns);
                draw_centered(198, value);
            } else {
                draw_centered(198, "Chinese / English conversation");
            }
        } else {
            u8g2_SetFont(s_u8g2, u8g2_font_wqy16_t_gb2312);
            draw_utf8_centered_in_region(
                0, BOARD_DISPLAY_WIDTH, 190,
                "回到主页  打开日历  查看状态");
            draw_utf8_centered_in_region(
                0, BOARD_DISPLAY_WIDTH, 218,
                "打开图片  打开设置  取消");
        }
        draw_system_footer(display_interaction_chat_footer());
        break;
    }
    u8g2_SendBuffer(s_u8g2);
}

static void format_utc_offset(char *buffer, size_t capacity,
                              int16_t offset_minutes)
{
    if (buffer == NULL || capacity == 0U) {
        return;
    }

    const int32_t signed_minutes = offset_minutes;
    if (signed_minutes == 0) {
        snprintf(buffer, capacity, "UTC");
        return;
    }

    const uint32_t absolute_minutes =
        (uint32_t)(signed_minutes < 0 ? -signed_minutes : signed_minutes);
    snprintf(buffer, capacity, "UTC%c%02u:%02u",
             signed_minutes < 0 ? '-' : '+',
             (unsigned)(absolute_minutes / 60U),
             (unsigned)(absolute_minutes % 60U));
}

static const char *alarm_repeat_name(uint8_t weekdays)
{
    switch (weekdays) {
    case 0x7fU:
        return "DAILY";
    case 0x3eU:
        return "MON-FRI";
    case 0x41U:
        return "WEEKENDS";
    default:
        return "CUSTOM";
    }
}

void display_show_settings(const display_settings_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    char value[40];
    draw_system_header("SETTINGS", status->recovery_mode ? 0U : 3U);

    if (status->recovery_mode) {
        draw_system_row(112, "MODE", "RECOVERY");
        draw_system_row(
            180, "NORMAL START",
            display_interaction_recovery_restart_hint());
        draw_system_footer(
            display_interaction_recovery_settings_footer());
        u8g2_SendBuffer(s_u8g2);
        return;
    }

    const char *power;
    if (status->power_apply_pending) {
        power = status->effective_low_power ? "SAVING | PENDING"
                                            : "NORMAL | PENDING";
    } else if (status->usb_data_host_connected) {
        power = "NORMAL | USB";
    } else if (status->manual_saving_requested) {
        power = "SAVING | MANUAL";
    } else if (status->automatic_saving_active) {
        power = "SAVING | LOW BAT";
    } else {
        power = status->effective_low_power ? "SAVING" : "NORMAL";
    }
    draw_system_row(72, "POWER", power);
    format_utc_offset(value, sizeof(value), status->utc_offset_minutes);
    draw_system_row(108, "TIME ZONE", value);
    draw_system_row(144, "TEMP UNIT",
                    status->temperature_fahrenheit ? "FAHRENHEIT"
                                                   : "CELSIUS");
    if (status->playback_volume_percent <= 100U) {
        snprintf(value, sizeof(value), "%u %%",
                 status->playback_volume_percent);
    } else {
        snprintf(value, sizeof(value), "NOT SET");
    }
    draw_system_row(180, "VOLUME", value);
    if (status->alarm_enabled && status->alarm_hour < 24U &&
        status->alarm_minute < 60U &&
        (status->alarm_weekdays & 0x7fU) != 0U) {
        snprintf(value, sizeof(value), "%02u:%02u %s",
                 status->alarm_hour, status->alarm_minute,
                 alarm_repeat_name(status->alarm_weekdays));
    } else {
        snprintf(value, sizeof(value), "OFF");
    }
    draw_system_row(216, "ALARM", value);

    draw_settings_footer(status->manual_saving_requested);
    u8g2_SendBuffer(s_u8g2);
}

void display_show_alarm(const display_alarm_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL || status->hour >= 24U ||
        status->minute >= 60U) {
        return;
    }

    char time_text[8];
    snprintf(time_text, sizeof(time_text), "%02u:%02u", status->hour,
             status->minute);

    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    draw_centered(34, "ALARM");
    u8g2_DrawHLine(s_u8g2, SYSTEM_SIDE_MARGIN, SYSTEM_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * SYSTEM_SIDE_MARGIN);

    u8g2_SetFont(s_u8g2, u8g2_font_logisoso78_tn);
    draw_centered(169, time_text);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    draw_centered(216, status->snooze_available ? "TIME TO WAKE"
                                               : "SNOOZED ALARM");
    draw_system_footer(status->snooze_available
                           ? "BOOT: STOP | KEY: SNOOZE 5m"
                           : "BOOT: STOP");
    u8g2_SendBuffer(s_u8g2);
}

void display_show_online_update(const display_online_update_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    char current_version[40];
    char latest_version[40];
    char detail[88];
    format_version(current_version, sizeof(current_version),
                   status->current_version);
    format_version(latest_version, sizeof(latest_version),
                   status->latest_version);

    draw_system_header("ONLINE UPDATE", status->recovery_mode ? 0U : 4U);
    switch (status->state) {
    case DISPLAY_ONLINE_UPDATE_STATE_CHECKING:
        draw_online_update_modal(
            "CHECKING",
            status->detail != NULL ? status->detail : "Connecting securely",
            "Current firmware remains available", "BOOT: CANCEL");
        break;
    case DISPLAY_ONLINE_UPDATE_STATE_CONFIRM_INSTALL:
        snprintf(detail, sizeof(detail), "%s  ->  %s", current_version,
                 latest_version);
        draw_online_update_modal("INSTALL UPDATE?", detail,
                                 "Keep power connected",
                                 "BOOT: CANCEL | HOLD KEY 3s: INSTALL");
        break;
    case DISPLAY_ONLINE_UPDATE_STATE_CONNECTING:
        snprintf(detail, sizeof(detail), "Preparing %s", latest_version);
        draw_online_update_modal(
            "STARTING UPDATE", detail,
            status->detail != NULL ? status->detail : "Connecting securely",
            "BOOT: CANCEL");
        break;
    case DISPLAY_ONLINE_UPDATE_STATE_DOWNLOADING:
        draw_online_update_progress(status);
        break;
    case DISPLAY_ONLINE_UPDATE_STATE_VERIFYING:
        draw_online_update_modal(
            "VERIFYING", latest_version,
            status->detail != NULL ? status->detail : "Checking firmware image",
            "DO NOT POWER OFF");
        break;
    case DISPLAY_ONLINE_UPDATE_STATE_SUCCESS:
        snprintf(detail, sizeof(detail), "%s is ready", latest_version);
        draw_online_update_modal(
            "UPDATE READY", detail,
            status->detail != NULL ? status->detail : "Restarting automatically",
            "DO NOT POWER OFF");
        break;
    case DISPLAY_ONLINE_UPDATE_STATE_FAILED:
        draw_online_update_modal(
            "UPDATE FAILED",
            status->detail != NULL ? status->detail : "Try again when online",
            "Current firmware is unchanged",
            status->recovery_mode
                ? display_interaction_recovery_update_footer(false)
                : display_interaction_online_update_footer(false));
        break;
    case DISPLAY_ONLINE_UPDATE_STATE_NOT_CHECKED:
    case DISPLAY_ONLINE_UPDATE_STATE_UP_TO_DATE:
    case DISPLAY_ONLINE_UPDATE_STATE_UPDATE_AVAILABLE: {
        const char *state_text = "NOT CHECKED";
        const char *footer =
            display_interaction_online_update_footer(false);
        if (status->state == DISPLAY_ONLINE_UPDATE_STATE_UP_TO_DATE) {
            state_text = "UP TO DATE";
        } else if (status->state ==
                   DISPLAY_ONLINE_UPDATE_STATE_UPDATE_AVAILABLE) {
            state_text = "UPDATE AVAILABLE";
            footer = display_interaction_online_update_footer(true);
        }
        if (status->recovery_mode) {
            draw_system_row(72, "CURRENT", current_version);
            draw_system_row(
                108, status->beta_channel ? "BETA LATEST" : "LATEST",
                latest_version);
            draw_system_row(
                144, "LAST RESET",
                status->recovery_reset != NULL
                    ? status->recovery_reset
                    : "UNKNOWN");
            draw_system_row(
                180, "FAILED AT",
                status->recovery_phase != NULL
                    ? status->recovery_phase
                    : "UNKNOWN");
            draw_system_row(216, "STATUS", state_text);
            draw_system_footer(
                display_interaction_recovery_update_footer(
                    status->state ==
                    DISPLAY_ONLINE_UPDATE_STATE_UPDATE_AVAILABLE));
        } else {
            draw_system_row(82, "CURRENT", current_version);
            draw_system_row(126,
                            status->beta_channel ? "BETA LATEST" : "LATEST",
                            latest_version);
            draw_system_row(170, "STATUS", state_text);
            draw_system_row(
                214, "LAST CHECK",
                status->last_checked != NULL ? status->last_checked : "--");
            draw_system_footer(footer);
        }
        break;
    }
    default:
        draw_system_row(126, "STATUS", "NOT CHECKED");
        draw_system_footer(status->recovery_mode
                               ? display_interaction_recovery_update_footer(false)
                               : display_interaction_online_update_footer(false));
        break;
    }
    u8g2_SendBuffer(s_u8g2);
}
