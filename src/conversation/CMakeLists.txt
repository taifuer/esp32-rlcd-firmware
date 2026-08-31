idf_component_register(
    SRCS "conversation_client.c" "conversation_config.c"
         "conversation_config_model.c" "conversation_config_record.c"
         "conversation_protocol.c" "conversation_text_buffer.c"
         "conversation_turn_state.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES cjson esp-tls esp_websocket_client nvs_flash storage
    PRIV_REQUIRES esp_hw_support freertos log
)
