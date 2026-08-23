idf_component_register(
    SRCS "clock_service.c" "clock_service_policy.c" "pcf85063.c"
         "rtc_backup.c" "rtc_backup_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c nvs_flash
)
