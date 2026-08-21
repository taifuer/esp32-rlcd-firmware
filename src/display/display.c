#include "display.h"

#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "calendar_month.h"
#include "network_credentials.h"
#include "qrcode.h"
#include "u8g2.h"
#include "u8g2_st7305.h"

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
    HEART_CENTER_Y = 272,
    STATUS_GROUP_WIDTH = 86,
    WIFI_ICON_WIDTH = 18,
    WIFI_ICON_HEIGHT = 14,
    WIFI_ICON_Y = 20,
    WIFI_CONNECTING_ROW = 5,
    STATUS_GROUP_GAP = 7,
    SETUP_SIDE_MARGIN = 12,
    SETUP_TITLE_BASELINE_Y = 34,
    SETUP_DIVIDER_Y = 44,
    SETUP_QR_AREA_TOP = 48,
    SETUP_QR_AREA_SIZE = 170,
    SETUP_QR_QUIET_MODULES = 4,
    SETUP_QR_MAX_SCALE = 5,
    SYSTEM_PAGE_COUNT = 5,
    SYSTEM_SIDE_MARGIN = 12,
    SYSTEM_TITLE_BASELINE_Y = 32,
    SYSTEM_DIVIDER_Y = 44,
    SYSTEM_LABEL_X = 18,
    SYSTEM_VALUE_X = 116,
    SYSTEM_FOOTER_DIVIDER_Y = 250,
    SYSTEM_FOOTER_BASELINE_Y = 280,
    CALENDAR_SIDE_MARGIN = 11,
    CALENDAR_HEADER_BASELINE_Y = 30,
    CALENDAR_HEADER_DIVIDER_Y = 43,
    CALENDAR_WEEKDAY_BASELINE_Y = 67,
    CALENDAR_WEEKDAY_DIVIDER_Y = 75,
    CALENDAR_GRID_TOP_Y = 78,
    CALENDAR_ROW_HEIGHT = 28,
    CALENDAR_COLUMN_WIDTH = 54,
    CALENDAR_FOOTER_DIVIDER_Y = 250,
    CALENDAR_FOOTER_BASELINE_Y = 280,
    ABOUT_QR_LEFT = 216,
    ABOUT_QR_TOP = 55,
    ABOUT_QR_SIZE = 172,
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
    snprintf(position, sizeof(position), "%u/%u", page, SYSTEM_PAGE_COUNT);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    u8g2_DrawStr(s_u8g2,
                 BOARD_DISPLAY_WIDTH - SYSTEM_SIDE_MARGIN -
                     (int)u8g2_GetStrWidth(s_u8g2, position),
                 SYSTEM_TITLE_BASELINE_Y, position);
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

static void draw_audio_meter(int baseline_y, const char *label,
                             uint8_t percent)
{
    enum {
        METER_LEFT = 76,
        METER_TOP_OFFSET = 13,
        METER_WIDTH = 246,
        METER_HEIGHT = 14,
        METER_INNER_WIDTH = METER_WIDTH - 4,
        PERCENT_RIGHT = 382,
    };
    char percent_text[8];
    snprintf(percent_text, sizeof(percent_text), "%u %%", percent);

    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    u8g2_DrawStr(s_u8g2, SYSTEM_LABEL_X, baseline_y, label);
    u8g2_DrawFrame(s_u8g2, METER_LEFT, baseline_y - METER_TOP_OFFSET,
                   METER_WIDTH, METER_HEIGHT);
    const int fill_width = (int)percent * METER_INNER_WIDTH / 100;
    if (fill_width > 0) {
        u8g2_DrawBox(s_u8g2, METER_LEFT + 2,
                     baseline_y - METER_TOP_OFFSET + 2,
                     fill_width, METER_HEIGHT - 4);
    }
    u8g2_DrawStr(s_u8g2,
                 PERCENT_RIGHT -
                     (int)u8g2_GetStrWidth(s_u8g2, percent_text),
                 baseline_y, percent_text);
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

static void draw_heart(int center_x, int center_y)
{
    u8g2_DrawDisc(s_u8g2, center_x - 4, center_y - 3, 4, U8G2_DRAW_ALL);
    u8g2_DrawDisc(s_u8g2, center_x + 4, center_y - 3, 4, U8G2_DRAW_ALL);
    u8g2_DrawTriangle(s_u8g2, center_x - 8, center_y - 2,
                      center_x + 8, center_y - 2, center_x, center_y + 8);
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

static void draw_wifi_status(int x, int y, display_network_state_t state)
{
    if (state == DISPLAY_NETWORK_CONNECTING) {
        const size_t row_bytes = (WIFI_ICON_WIDTH + 7U) / 8U;
        u8g2_DrawXBM(s_u8g2, x, y + WIFI_CONNECTING_ROW, WIFI_ICON_WIDTH,
                     WIFI_ICON_HEIGHT - WIFI_CONNECTING_ROW,
                     &WIFI_STATUS_BITMAP[WIFI_CONNECTING_ROW * row_bytes]);
    } else {
        u8g2_DrawXBM(s_u8g2, x, y, WIFI_ICON_WIDTH, WIFI_ICON_HEIGHT,
                     WIFI_STATUS_BITMAP);
    }

    if (state == DISPLAY_NETWORK_UNCONFIGURED || state == DISPLAY_NETWORK_ERROR) {
        /* A white keyline keeps the two-pixel disabled slash readable where it
         * crosses the filled arcs. */
        u8g2_SetDrawColor(s_u8g2, 0);
        for (int offset = -1; offset <= 2; ++offset) {
            u8g2_DrawLine(s_u8g2, x + 1 + offset, y,
                          x + WIFI_ICON_WIDTH - 2 + offset,
                          y + WIFI_ICON_HEIGHT - 1);
        }
        u8g2_SetDrawColor(s_u8g2, 1);
        u8g2_DrawLine(s_u8g2, x + 1, y,
                      x + WIFI_ICON_WIDTH - 2, y + WIFI_ICON_HEIGHT - 1);
        u8g2_DrawLine(s_u8g2, x + 2, y,
                      x + WIFI_ICON_WIDTH - 1, y + WIFI_ICON_HEIGHT - 1);
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
    draw_battery(x + WIFI_ICON_WIDTH + STATUS_GROUP_GAP, 20,
                 dashboard->battery_valid, dashboard->battery_percent);
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
    snprintf(line, sizeof(line), "OPEN: %s", url != NULL ? url : "192.168.4.1");
    draw_setup_line(qr_rendered ? 280 : 197, line);

    if (!qr_rendered) {
        u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
        draw_centered(244, "Connect with a phone or computer");
    }
    u8g2_SendBuffer(s_u8g2);
}

void display_show_firmware_update_ready(const char *ssid, const char *password,
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
    draw_centered(SETUP_TITLE_BASELINE_Y, "FIRMWARE UPDATE");
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
    snprintf(line, sizeof(line), "OPEN: %s | BOOT: CANCEL",
             url != NULL ? url : "192.168.4.1");
    draw_setup_line(qr_rendered ? 280 : 197, line);

    if (!qr_rendered) {
        u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
        draw_centered(244, "Connect and open the update page");
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

    if (dashboard->time_valid) {
        snprintf(text, sizeof(text), "%02u:%02u:%02u",
                 dashboard->hour, dashboard->minute, dashboard->second);
    } else {
        snprintf(text, sizeof(text), "--:--:--");
    }
    u8g2_SetFont(s_u8g2, u8g2_font_logisoso78_tn);
    draw_centered(TIME_BASELINE_Y, text);

    u8g2_DrawHLine(s_u8g2, DASHBOARD_SIDE_MARGIN, BOTTOM_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * DASHBOARD_SIDE_MARGIN);

    char temperature_text[24];
    if (dashboard->environment_valid) {
        snprintf(temperature_text, sizeof(temperature_text), "%.1f °C",
                 (double)dashboard->temperature_c);
    } else {
        snprintf(temperature_text, sizeof(temperature_text), "--.- °C");
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
    draw_heart(BOARD_DISPLAY_WIDTH / 2, HEART_CENTER_Y);
    draw_utf8_centered_in_region(BOARD_DISPLAY_WIDTH / 2, BOARD_DISPLAY_WIDTH / 2,
                                 ENVIRONMENT_BASELINE_Y, humidity_text);
    u8g2_SendBuffer(s_u8g2);
}

void display_show_calendar(const display_dashboard_t *dashboard)
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

    u8g2_DrawHLine(s_u8g2, CALENDAR_SIDE_MARGIN,
                   CALENDAR_FOOTER_DIVIDER_Y,
                   BOARD_DISPLAY_WIDTH - 2 * CALENDAR_SIDE_MARGIN);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(CALENDAR_FOOTER_BASELINE_Y,
                  "BOOT: PAGE | KEY: SYSTEM");
    u8g2_SendBuffer(s_u8g2);
}

void display_show_device_health(const display_system_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    char value[64];
    draw_system_header("DEVICE HEALTH", 1U);

    if (!status->rtc_ready) {
        snprintf(value, sizeof(value), "NOT FOUND");
    } else if (!status->time_valid) {
        snprintf(value, sizeof(value), "INVALID TIME");
    } else {
        snprintf(value, sizeof(value), "OK | %04u-%02u-%02u %02u:%02u",
                 status->year, status->month, status->day,
                 status->hour, status->minute);
    }
    draw_system_row(76, "RTC", value);

    if (!status->rtc_ready) {
        snprintf(value, sizeof(value), "NOT AVAILABLE");
    } else {
        snprintf(value, sizeof(value), "%s",
                 status->rtc_backup_state != NULL
                     ? status->rtc_backup_state
                     : "NOT READY");
    }
    draw_system_row(113, "RTC BACKUP", value);

    if (!status->sensor_ready) {
        snprintf(value, sizeof(value), "NOT FOUND");
    } else if (!status->environment_valid) {
        snprintf(value, sizeof(value), "READ ERROR");
    } else {
        snprintf(value, sizeof(value), "OK | %.1f C | %.0f %%",
                 (double)status->temperature_c,
                 (double)status->humidity_percent);
    }
    draw_system_row(150, "SENSOR", value);

    if (!status->battery_ready) {
        snprintf(value, sizeof(value), "NOT READY");
    } else if (!status->battery_valid) {
        snprintf(value, sizeof(value), "READ ERROR");
    } else {
        snprintf(value, sizeof(value), "OK | %u %% | %u mV",
                 status->battery_percent, status->battery_voltage_mv);
    }
    draw_system_row(187, "BATTERY", value);

    snprintf(value, sizeof(value), "%u KiB | OK",
             (unsigned)status->psram_kib);
    draw_system_row(224, "PSRAM", value);

    draw_system_footer("BOOT: HOME | KEY: NEXT");
    u8g2_SendBuffer(s_u8g2);
}

void display_show_network_time(const display_system_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    char value[64];
    draw_system_header("NETWORK & TIME", 2U);

    if (!status->network_ready) {
        snprintf(value, sizeof(value), "NOT READY");
    } else if (!status->network_configured) {
        snprintf(value, sizeof(value), "NOT CONFIGURED");
    } else {
        snprintf(value, sizeof(value), "%s",
                 status->network_state != NULL ? status->network_state
                                               : "UNKNOWN");
    }
    draw_system_row(82, "WI-FI", value);

    if (!status->rtc_ready) {
        snprintf(value, sizeof(value), "NOT FOUND");
    } else if (!status->time_valid) {
        snprintf(value, sizeof(value), "INVALID TIME");
    } else {
        snprintf(value, sizeof(value), "%04u-%02u-%02u %02u:%02u",
                 status->year, status->month, status->day,
                 status->hour, status->minute);
    }
    draw_system_row(126, "RTC", value);

    if (status->last_sync_valid) {
        snprintf(value, sizeof(value), "%04u-%02u-%02u %02u:%02u",
                 status->last_sync_year, status->last_sync_month,
                 status->last_sync_day, status->last_sync_hour,
                 status->last_sync_minute);
    } else {
        snprintf(value, sizeof(value), "--");
    }
    draw_system_row(170, "LAST SYNC", value);
    draw_system_row(214, "AUTO SYNC", "EVERY 24 HOURS");

    draw_system_footer(
        "BOOT: HOME | KEY: NEXT | HOLD KEY 2s: SYNC");
    u8g2_SendBuffer(s_u8g2);
}

void display_show_audio(const display_audio_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    char value[80];
    draw_system_header("AUDIO", 3U);

    if (status->state == DISPLAY_AUDIO_STATE_PLAYING_TONE) {
        u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
        draw_centered(105, "SPEAKER TEST");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(160, "Listen for two short tones");
        draw_centered(205, "Recording starts next");
        draw_system_footer("BOOT: CANCEL");
    } else if (status->state ==
               DISPLAY_AUDIO_STATE_PREPARING_RECORDING) {
        u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
        draw_centered(112, "GET READY");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(165, "Speak when RECORDING appears");
        draw_centered(207, "Maximum 5 seconds");
        draw_system_footer("BOOT: CANCEL");
    } else if (status->state == DISPLAY_AUDIO_STATE_RECORDING) {
        snprintf(value, sizeof(value), "RECORDING %02u/%02us",
                 (unsigned)((status->recording_elapsed_ms + 999U) / 1000U),
                 (unsigned)(status->max_recording_ms / 1000U));
        u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
        draw_centered(86, value);
        draw_audio_meter(132, "MIC 1",
                         status->microphone_1_level_percent);
        draw_audio_meter(178, "MIC 2",
                         status->microphone_2_level_percent);
        u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
        draw_centered(222, "Speak clearly; audio stays in memory");
        draw_system_footer("KEY: STOP | BOOT: CANCEL");
    } else if (status->state == DISPLAY_AUDIO_STATE_ANALYZING) {
        u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
        draw_centered(112, "ANALYZING");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(165, "Checking both microphones");
        draw_centered(207, "Preparing temporary playback");
        draw_system_footer("BOOT: CANCEL");
    } else if (status->state == DISPLAY_AUDIO_STATE_PLAYBACK) {
        snprintf(value, sizeof(value), "PLAYBACK %02u/%02us",
                 (unsigned)((status->playback_elapsed_ms + 999U) / 1000U),
                 (unsigned)((status->recording_duration_ms + 999U) / 1000U));
        u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
        draw_centered(86, value);
        snprintf(value, sizeof(value), "SOURCE  MIC %u",
                 status->playback_microphone);
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(132, value);
        draw_audio_meter(174, "MIC 1",
                         status->microphone_1_level_percent);
        draw_audio_meter(214, "MIC 2",
                         status->microphone_2_level_percent);
        draw_system_footer("KEY: STOP | BOOT: CANCEL");
    } else if (status->state == DISPLAY_AUDIO_STATE_CANCELLED) {
        u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
        draw_centered(112, "CANCELLED");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(165, "Temporary audio cleared");
        draw_centered(207, "Hold KEY 2s to try again");
        draw_system_footer("BOOT: HOME | KEY: NEXT");
    } else if (status->state == DISPLAY_AUDIO_STATE_FAILED) {
        u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
        draw_centered(112, "TEST FAILED");
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(165, "Check the USB diagnostic log");
        draw_centered(207, "Hold KEY 2s to try again");
        draw_system_footer("BOOT: HOME | KEY: NEXT");
    } else if (status->state == DISPLAY_AUDIO_STATE_COMPLETED ||
               status->test_completed) {
        draw_system_row(72, "SPEAKER",
                        status->tone_played ? "TONE PLAYED" : "FAILED");
        if (status->microphone_capture_completed) {
            snprintf(value, sizeof(value), "%u %%",
                     status->microphone_1_level_percent);
            draw_system_row(108, "MIC 1", value);
            snprintf(value, sizeof(value), "%u %%",
                     status->microphone_2_level_percent);
            draw_system_row(144, "MIC 2", value);
        } else {
            draw_system_row(108, "MIC 1", "READ FAILED");
            draw_system_row(144, "MIC 2", "READ FAILED");
        }
        if (status->voice_played) {
            snprintf(value, sizeof(value), "VOICE PLAYED | MIC %u",
                     status->playback_microphone);
        } else if (status->playback_stopped) {
            snprintf(value, sizeof(value), "STOPPED | MIC %u",
                     status->playback_microphone);
        } else {
            snprintf(value, sizeof(value), "NOT PLAYED");
        }
        draw_system_row(180, "LOOPBACK", value);
        draw_system_row(216, "RESULT",
                        status->result != NULL ? status->result : "FAILED");
        draw_system_footer(
            "BOOT: HOME | KEY: NEXT | HOLD KEY 2s: TEST");
    } else {
        draw_system_row(82, "SPEAKER",
                        status->speaker_ready ? "READY" : "NOT FOUND");
        draw_system_row(126, "DUAL MICS",
                        status->microphones_ready ? "READY" : "NOT FOUND");
        snprintf(value, sizeof(value), "%u kHz | %u-bit",
                 (unsigned)(status->sample_rate_hz / 1000U),
                 status->bits_per_sample);
        draw_system_row(170, "FORMAT", value);
        draw_system_row(214, "LAST TEST",
                        status->initialized ? "NOT RUN" : "NOT READY");
        draw_system_footer(
            "BOOT: HOME | KEY: NEXT | HOLD KEY 2s: TEST");
    }
    u8g2_SendBuffer(s_u8g2);
}

void display_show_wifi_maintenance(const display_system_status_t *status)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    draw_system_header("WI-FI MAINTENANCE", 4U);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB24_tf);
    if (!status->network_ready) {
        draw_centered(105, "NOT READY");
    } else if (status->network_configured) {
        draw_centered(105, "CONFIGURED");
    } else {
        draw_centered(105, "NOT CONFIGURED");
    }

    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    if (status->network_configured) {
        draw_centered(155, "Saved home Wi-Fi is available");
        draw_centered(190, "Hold KEY for 5 seconds to clear it");
        draw_centered(218, "Device restarts in setup mode");
    } else {
        draw_centered(155, "No saved network settings");
        draw_centered(190, "Follow the setup QR after restart");
    }

    draw_system_footer(
        "BOOT: HOME | KEY: NEXT | HOLD KEY 5s: RESET");
    u8g2_SendBuffer(s_u8g2);
}

void display_show_about_update(const display_system_status_t *status,
                               const char *release_url)
{
    if (s_u8g2 == NULL || status == NULL) {
        return;
    }

    char version_text[40];
    char idf_text[48];
    snprintf(version_text, sizeof(version_text), "v%s",
             status->firmware_version != NULL ? status->firmware_version : "-");
    snprintf(idf_text, sizeof(idf_text), "ESP-IDF %s",
             status->idf_version != NULL ? status->idf_version : "-");

    draw_system_header("ABOUT & UPDATE", 5U);
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    u8g2_DrawStr(s_u8g2, 18, 78, "ESP32 RLCD");
    u8g2_SetFont(s_u8g2, u8g2_font_helvB18_tf);
    u8g2_DrawStr(s_u8g2, 18, 118, version_text);
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    u8g2_DrawStr(s_u8g2, 18, 152, idf_text);
    u8g2_DrawStr(s_u8g2, 18, 174, "LOCAL UPDATE");
    u8g2_DrawStr(s_u8g2, 18, 194, "HOLD KEY 3s");
    u8g2_DrawStr(s_u8g2, 18, 214, "WI-FI + BROWSER");
    u8g2_DrawStr(s_u8g2, 18, 234, "USB: RECOVERY");

    display_qr_context_t context = {
        .area_left = ABOUT_QR_LEFT,
        .area_top = ABOUT_QR_TOP,
        .area_size = ABOUT_QR_SIZE,
        .quiet_modules = 4,
        .max_scale = 5,
        .standard_polarity = true,
    };
    const bool qr_rendered = draw_qr_payload(
        release_url != NULL ? release_url : "https://github.com/taifuer/esp32-rlcd-firmware/releases/latest",
        &context, ESP_QRCODE_ECC_MED);
    if (!qr_rendered) {
        u8g2_DrawFrame(s_u8g2, ABOUT_QR_LEFT, ABOUT_QR_TOP,
                       ABOUT_QR_SIZE, ABOUT_QR_SIZE);
        u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
        const int center_x = ABOUT_QR_LEFT + ABOUT_QR_SIZE / 2;
        const char *message = "QR UNAVAILABLE";
        u8g2_DrawStr(s_u8g2,
                     center_x - (int)u8g2_GetStrWidth(s_u8g2, message) / 2,
                     ABOUT_QR_TOP + ABOUT_QR_SIZE / 2, message);
    }
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    const char *qr_label = "LATEST RELEASE";
    u8g2_DrawStr(s_u8g2,
                 ABOUT_QR_LEFT +
                     (ABOUT_QR_SIZE -
                      (int)u8g2_GetStrWidth(s_u8g2, qr_label)) /
                         2,
                 238, qr_label);

    draw_system_footer("BOOT: HOME | KEY: NEXT | HOLD KEY 3s: UPDATE");
    u8g2_SendBuffer(s_u8g2);
}
