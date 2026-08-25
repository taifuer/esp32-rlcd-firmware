idf_component_register(
    SRCS "sd_image.c" "image_catalog.c" "image_delete_policy.c"
         "image_import_policy.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES board fatfs freertos heap image mbedtls nvs_flash
    PRIV_REQUIRES esp_hw_support
)
