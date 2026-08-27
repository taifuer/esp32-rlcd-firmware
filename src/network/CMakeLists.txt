idf_component_register(
    SRCS "network_credentials.c" "network_retry_policy.c"
         "network_session_policy.c" "network_station_link.c" "network_time.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_http_server esp_netif esp_wifi nvs_flash storage
    PRIV_REQUIRES esp_hw_support
)
