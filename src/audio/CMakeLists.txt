idf_component_register(
    SRCS "audio_diagnostics.c" "audio_level.c" "audio_session_state.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_codec board esp_driver_i2c esp_driver_i2s heap
    PRIV_REQUIRES freertos log
)
