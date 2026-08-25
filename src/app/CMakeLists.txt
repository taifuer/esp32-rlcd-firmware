idf_component_register(
    SRCS "main.c" "network_screen_policy.c" "page_state.c" "usb_commands.c"
    INCLUDE_DIRS "."
    REQUIRES alarm audio board calendar display gallery input network power rtc sd_image sensors settings storage update esp_app_format esp_system heap esp_driver_usb_serial_jtag vfs
)
