#include "display.h"

#include <stdio.h>

#include "board_pins.h"
#include "u8g2.h"
#include "u8g2_st7305.h"

static u8g2_st7305_t s_lcd;
static u8g2_t *s_u8g2;

static const char *const WEEKDAYS[] = {
    "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
    "THURSDAY", "FRIDAY", "SATURDAY",
};

static void draw_centered(int baseline_y, const char *text)
{
    int x = (BOARD_DISPLAY_WIDTH - (int)u8g2_GetStrWidth(s_u8g2, text)) / 2;
    if (x < 0) {
        x = 0;
    }
    u8g2_DrawStr(s_u8g2, x, baseline_y, text);
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

void display_show_dashboard(const display_dashboard_t *dashboard)
{
    if (s_u8g2 == NULL || dashboard == NULL) {
        return;
    }

    char text[96];
    u8g2_ClearBuffer(s_u8g2);
    u8g2_SetDrawColor(s_u8g2, 1);
    u8g2_DrawFrame(s_u8g2, 3, 3, BOARD_DISPLAY_WIDTH - 6, BOARD_DISPLAY_HEIGHT - 6);

    snprintf(text, sizeof(text), "RLCD FIRMWARE  v%s",
             dashboard->firmware_version != NULL ? dashboard->firmware_version : "?");
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    draw_centered(25, text);
    u8g2_DrawHLine(s_u8g2, 12, 36, BOARD_DISPLAY_WIDTH - 24);

    if (dashboard->time_valid) {
        snprintf(text, sizeof(text), "%02u:%02u:%02u",
                 dashboard->hour, dashboard->minute, dashboard->second);
    } else {
        snprintf(text, sizeof(text), "--:--:--");
    }
    u8g2_SetFont(s_u8g2, u8g2_font_logisoso50_tn);
    draw_centered(112, text);

    if (dashboard->time_valid) {
        snprintf(text, sizeof(text), "%04u-%02u-%02u",
                 dashboard->year, dashboard->month, dashboard->day);
        u8g2_SetFont(s_u8g2, u8g2_font_helvB18_tf);
        draw_centered(153, text);

        const char *weekday = dashboard->weekday <= 6U ? WEEKDAYS[dashboard->weekday] : "UNKNOWN";
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(181, weekday);
    } else {
        u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
        draw_centered(163, "CONNECT USB TO SET RTC");
    }

    if (dashboard->environment_valid) {
        snprintf(text, sizeof(text), "TEMP %.1f C     HUM %.1f %%",
                 (double)dashboard->temperature_c, (double)dashboard->humidity_percent);
    } else {
        snprintf(text, sizeof(text), "TEMP --.- C     HUM --.- %%");
    }
    u8g2_SetFont(s_u8g2, u8g2_font_helvB14_tf);
    draw_centered(218, text);

    u8g2_DrawHLine(s_u8g2, 12, 235, BOARD_DISPLAY_WIDTH - 24);
    snprintf(text, sizeof(text), "RTC 0x51: %s    SHTC3 0x70: %s",
             dashboard->rtc_status != NULL ? dashboard->rtc_status : "UNKNOWN",
             dashboard->sensor_status != NULL ? dashboard->sensor_status : "UNKNOWN");
    u8g2_SetFont(s_u8g2, u8g2_font_6x13_tf);
    draw_centered(259, text);

    if (dashboard->sensor_id != 0U) {
        snprintf(text, sizeof(text), "Hello, world.    SENSOR ID 0x%04X",
                 dashboard->sensor_id);
    } else {
        snprintf(text, sizeof(text), "Hello, world.    OFFLINE HARDWARE TEST");
    }
    draw_centered(281, text);
    u8g2_SendBuffer(s_u8g2);
}
