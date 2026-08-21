idf_component_register(
    SRCS "firmware_update.c" "firmware_update_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_app_format esp_event esp_http_server esp_netif esp_wifi network
    PRIV_REQUIRES esp_hw_support
)
