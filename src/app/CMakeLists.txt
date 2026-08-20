idf_component_register(
    SRCS "main.c" "usb_commands.c"
    INCLUDE_DIRS "."
    REQUIRES board calendar display input network power rtc sensors esp_app_format esp_system heap esp_driver_usb_serial_jtag vfs
)
