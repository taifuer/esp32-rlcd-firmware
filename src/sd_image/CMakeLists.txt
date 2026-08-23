idf_component_register(
    SRCS "sd_image.c" "image_catalog.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES board fatfs freertos heap image
)
