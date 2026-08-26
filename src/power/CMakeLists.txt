idf_component_register(
    SRCS "battery.c" "battery_level.c" "power_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_adc esp_pm log
)
