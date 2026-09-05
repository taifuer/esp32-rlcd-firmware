idf_component_register(
    SRCS "music_format.c" "music_library.c" "music_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES sd_image espressif__esp_audio_codec
    PRIV_REQUIRES heap freertos fatfs mbedtls esp_hw_support
)
