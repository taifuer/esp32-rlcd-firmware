idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    REQUIRES board network qrcode u8g2_st7305
)
