idf_component_register(
    SRCS "main.c" "rtc_time_sync.c"
    INCLUDE_DIRS "."
    REQUIRES board display rtc sensors esp_app_format heap esp_driver_usb_serial_jtag vfs
)
