idf_component_register(
    SRCS "audio_conversation_control.c" "audio_conversation_flow.c"
         "audio_diagnostics.c"
         "audio_level.c" "audio_mono.c"
         "audio_session_state.c"
         "voice_command_policy.c" "voice_reliability_metrics.c"
         "voice_session_state.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_codec board conversation esp_driver_i2c esp_driver_i2s
             esp_partition network
             espressif__esp-sr espressif__esp-dsp espressif__dl_fft
             espressif__cjson heap mbedtls spi_flash
    PRIV_REQUIRES esp_pm freertos log
)
