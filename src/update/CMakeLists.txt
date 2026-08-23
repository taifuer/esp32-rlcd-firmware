idf_component_register(
    SRCS
        "firmware_update.c"
        "firmware_update_policy.c"
        "online_firmware_update.c"
        "online_update_manifest.c"
        "online_update_policy.c"
        "settings_portal_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_update
        esp_app_format
        esp_event
        esp_http_client
        esp_http_server
        esp_netif
        esp_system
        esp_wifi
        json
        mbedtls
        network
        rtc
        settings
    PRIV_REQUIRES esp_hw_support
)
