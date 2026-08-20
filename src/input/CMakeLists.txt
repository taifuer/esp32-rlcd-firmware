idf_component_register(
    SRCS "button_state.c" "user_button.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_gpio
)
