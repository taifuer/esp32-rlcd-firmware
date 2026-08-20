idf_component_register(
    SRCS "board_buttons.c" "button_state.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_gpio
)
