#include "board_buttons.h"

#include "board_pins.h"
#include "driver/gpio.h"

esp_err_t board_buttons_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_BOOT_GPIO) | (1ULL << BOARD_KEY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

bool board_boot_is_pressed(void)
{
    return gpio_get_level(BOARD_BOOT_GPIO) == 0;
}

bool board_key_is_pressed(void)
{
    return gpio_get_level(BOARD_KEY_GPIO) == 0;
}
