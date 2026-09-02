idf_component_register(
    SRCS
        "weather_cache.c"
        "weather_cache_record.c"
        "weather_client.c"
        "weather_config.c"
        "weather_config_model.c"
        "weather_config_record.c"
        "weather_gzip.c"
        "weather_http_json.c"
        "weather_location_catalog.c"
        "weather_model.c"
        "weather_request.c"
        "weather_response.c"
        "weather_service.c"
    INCLUDE_DIRS "." "include"
    REQUIRES
        cjson
        esp_http_client
        esp_rom
        mbedtls
        network
        nvs_flash
        storage
    PRIV_REQUIRES esp_hw_support freertos heap log
)
