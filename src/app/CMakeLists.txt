idf_component_register(
    SRCS "main.c" "network_screen_policy.c" "page_state.c" "usb_commands.c"
    INCLUDE_DIRS "."
    REQUIRES audio board calendar display input network power rtc sensors storage update esp_app_format esp_system heap esp_driver_usb_serial_jtag vfs
)
