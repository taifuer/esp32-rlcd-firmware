#include "display.h"

#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "network_credentials.h"
#include "qrcode.h"
#include "u8g2.h"
#include "u8g2_st7305.h"

static u8g2_st7305_t s_lcd;
static u8g2_t *s_u8g2;

static const char *const WEEKDAYS[] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六",
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

static void draw_setup_line(int baseline_y, const char *text)
{
    const int available_width = BOARD_DISPLAY_WIDTH - 2 * SETUP_SIDE_MARGIN;

    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    if ((int)u8g2_GetStrWidth(s_u8g2, text) > available_width) {
        u8g2_SetFont(s_u8g2, u8g2_font_5x8_tf);
    }
    draw_centered(baseline_y, text);
}

typedef struct {
    bool rendered;
} setup_qr_context_t;

static void draw_setup_qr(esp_qrcode_handle_t qrcode, void *user_data)
{
    setup_qr_context_t *context = user_data;
    const int module_count = esp_qrcode_get_size(qrcode);
    const int total_modules = module_count + 2 * SETUP_QR_QUIET_MODULES;
    int scale = total_modules > 0 ? SETUP_QR_AREA_SIZE / total_modules : 0;
    if (scale > SETUP_QR_MAX_SCALE) {
        scale = SETUP_QR_MAX_SCALE;
    }
    if (context == NULL || module_count <= 0 || scale <= 0) {
        return;
    }

    const int pixel_size = total_modules * scale;
    const int left = (BOARD_DISPLAY_WIDTH - pixel_size) / 2;
    const int top = SETUP_QR_AREA_TOP + (SETUP_QR_AREA_SIZE - pixel_size) / 2;
    const int data_left = left + SETUP_QR_QUIET_MODULES * scale;
    const int data_top = top + SETUP_QR_QUIET_MODULES * scale;

    for (int row = 0; row < module_count; ++row) {
        for (int column = 0; column < module_count; ++column) {
            if (esp_qrcode_get_module(qrcode, column, row)) {
                u8g2_DrawBox(s_u8g2, data_left + column * scale,
                             data_top + row * scale, scale, scale);
            }
        }
    }
    context->rendered = true;
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
            setup_qr_context_t context = {0};
            esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
            config.display_func_with_cb = draw_setup_qr;
            config.max_qrcode_version = 10;
            config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
            config.user_data = &context;
            (void)esp_qrcode_generate(&config, payload);
            qr_rendered = context.rendered;
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
