idf_component_register(
    SRCS "network_credentials.c" "network_time.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_http_server esp_netif esp_wifi nvs_flash storage
    PRIV_REQUIRES esp_hw_support
)
