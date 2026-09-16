idf_component_register(
    SRCS "market_model.c" "market_config.c" "market_client.c" "market_service.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_http_client mbedtls network nvs_flash storage
    PRIV_REQUIRES esp_timer freertos log
)
