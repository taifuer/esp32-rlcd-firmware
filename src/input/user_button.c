#include "user_button.h"

#include "board_pins.h"
#include "driver/gpio.h"

esp_err_t user_button_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_KEY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

bool user_button_is_pressed(void)
{
    return gpio_get_level(BOARD_KEY_GPIO) == 0;
}
