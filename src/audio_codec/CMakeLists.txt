# Build only the ES8311/ES7210 subset used by this board from the pinned
# esp_codec_dev source kept outside the repository. The upstream component
# CMake file references optional, untracked ZL38063 directories even when that
# codec is disabled, so this project-owned adapter lists the required sources
# explicitly without modifying or copying third-party code.
if(DEFINED ENV{RLCD_WAVESHARE_AUDIO_CODEC_DIR})
    file(TO_CMAKE_PATH "$ENV{RLCD_WAVESHARE_AUDIO_CODEC_DIR}"
         AUDIO_CODEC_SOURCE_DIR)
else()
    set(AUDIO_CODEC_SOURCE_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/sources/waveshareteam/ESP32-S3-RLCD-4.2/02_Example/Arduino/07_Audio_Test/src/ExternLib/esp_codec_dev")
endif()

if(NOT EXISTS "${AUDIO_CODEC_SOURCE_DIR}/include/esp_codec_dev.h" OR
   NOT EXISTS "${AUDIO_CODEC_SOURCE_DIR}/device/es8311/es8311.c" OR
   NOT EXISTS "${AUDIO_CODEC_SOURCE_DIR}/device/es7210/es7210.c")
    message(FATAL_ERROR
        "Missing pinned esp_codec_dev sources under: ${AUDIO_CODEC_SOURCE_DIR}. "
        "Run ./scripts/bootstrap.sh first.")
endif()

idf_component_register(
    SRCS
        "${AUDIO_CODEC_SOURCE_DIR}/esp_codec_dev.c"
        "${AUDIO_CODEC_SOURCE_DIR}/esp_codec_dev_vol.c"
        "${AUDIO_CODEC_SOURCE_DIR}/esp_codec_dev_if.c"
        "${AUDIO_CODEC_SOURCE_DIR}/audio_codec_sw_vol.c"
        "${AUDIO_CODEC_SOURCE_DIR}/platform/audio_codec_gpio.c"
        "${AUDIO_CODEC_SOURCE_DIR}/platform/audio_codec_ctrl_i2c.c"
        "${AUDIO_CODEC_SOURCE_DIR}/platform/audio_codec_data_i2s.c"
        "${AUDIO_CODEC_SOURCE_DIR}/platform/esp_codec_dev_os.c"
        "${AUDIO_CODEC_SOURCE_DIR}/device/es8311/es8311.c"
        "${AUDIO_CODEC_SOURCE_DIR}/device/es7210/es7210.c"
    INCLUDE_DIRS
        "${AUDIO_CODEC_SOURCE_DIR}/include"
        "${AUDIO_CODEC_SOURCE_DIR}/interface"
        "${AUDIO_CODEC_SOURCE_DIR}/device/include"
    REQUIRES esp_common esp_driver_gpio esp_driver_i2c esp_driver_i2s
    PRIV_REQUIRES freertos log
)
