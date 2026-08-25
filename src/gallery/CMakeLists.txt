idf_component_register(
    SRCS "gallery_download.c" "gallery_manifest.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_http_client json mbedtls network sd_image image
)
