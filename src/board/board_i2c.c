#include "board_i2c.h"

#include "board_pins.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board_i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t config = {
        .i2c_port = -1,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &s_bus), TAG,
                        "failed to initialize I2C on SDA=%d SCL=%d",
                        BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);
    ESP_LOGI(TAG, "I2C ready: SDA=%d SCL=%d speed=%d Hz",
             BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_I2C_CLOCK_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}

esp_err_t board_i2c_probe(uint16_t address, int timeout_ms)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(s_bus, address, timeout_ms);
}
