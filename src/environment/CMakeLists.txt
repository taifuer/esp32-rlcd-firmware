idf_component_register(
    SRCS "environment_history.c" "environment_storage.c" "environment_archive.c" "environment_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer freertos heap sd_image esp_hw_support
)
