idf_component_register(
    SRCS "main.c" "hold_interaction.c" "image_delete_ui.c"
         "network_screen_policy.c"
         "page_state.c" "usb_commands.c" "voice_backend_policy.c"
    INCLUDE_DIRS "."
    REQUIRES alarm audio board calendar conversation display gallery input network power rtc sd_image sensors settings storage update esp_app_format esp_system heap esp_driver_usb_serial_jtag vfs
)
