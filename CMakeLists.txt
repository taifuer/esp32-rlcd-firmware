cmake_minimum_required(VERSION 3.16)

# All external components are pinned and prepared by scripts/bootstrap.sh.
# Disable implicit registry downloads and their in-tree managed_components dir.
set(ENV{IDF_COMPONENT_MANAGER} "0")

# Keep all project-owned ESP-IDF components under src/. The directory contains
# one subdirectory per component, including the application component.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/src")

# Reuse the board-specific ST7305 adapter and its pinned U8g2 source from an
# external checkout. No third-party source is stored in this repository.
if(DEFINED ENV{RLCD_WAVESHARE_COMPONENTS_DIR})
    file(TO_CMAKE_PATH "$ENV{RLCD_WAVESHARE_COMPONENTS_DIR}" WAVESHARE_U8G2_COMPONENTS)
else()
    set(WAVESHARE_U8G2_COMPONENTS
        "${CMAKE_CURRENT_LIST_DIR}/../../third_party/sources/waveshareteam/ESP32-S3-RLCD-4.2/02_Example/ESP-IDF/11_U8G2_Test/components")
endif()

foreach(component_name u8g2 u8g2_st7305)
    if(NOT EXISTS "${WAVESHARE_U8G2_COMPONENTS}/${component_name}/CMakeLists.txt")
        message(FATAL_ERROR
            "Missing Waveshare component '${component_name}'. Expected it under: "
            "${WAVESHARE_U8G2_COMPONENTS}")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS
        "${WAVESHARE_U8G2_COMPONENTS}/${component_name}")
endforeach()

if(DEFINED ENV{RLCD_QRCODE_COMPONENT_DIR})
    file(TO_CMAKE_PATH "$ENV{RLCD_QRCODE_COMPONENT_DIR}" QRCODE_COMPONENT_DIR)
else()
    set(QRCODE_COMPONENT_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../third_party/sources/espressif/idf-extra-components/qrcode")
endif()
if(NOT EXISTS "${QRCODE_COMPONENT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Missing Espressif QR Code component. Expected it under: "
        "${QRCODE_COMPONENT_DIR}")
endif()
list(APPEND EXTRA_COMPONENT_DIRS "${QRCODE_COMPONENT_DIR}")

foreach(speech_dependency_var
        RLCD_ESP_SR_DIR RLCD_ESP_DSP_DIR RLCD_DL_FFT_DIR RLCD_CJSON_DIR
        RLCD_ESP_WEBSOCKET_CLIENT_DIR RLCD_ESP_AUDIO_CODEC_DIR)
    if(NOT DEFINED ENV{${speech_dependency_var}})
        message(FATAL_ERROR
            "Missing ${speech_dependency_var}; source scripts/common.sh or "
            "build through ./scripts/build.sh")
    endif()
    file(TO_CMAKE_PATH "$ENV{${speech_dependency_var}}"
         speech_dependency_path)
    if(NOT EXISTS "${speech_dependency_path}/CMakeLists.txt")
        message(FATAL_ERROR
            "Incomplete speech dependency: ${speech_dependency_path}. "
            "Run ./scripts/bootstrap.sh first.")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${speech_dependency_path}")
endforeach()

set(RLCD_PROJECT_VERSION "" CACHE STRING
    "SemVer embedded in the firmware image; empty selects the repository default")
if(NOT "${RLCD_PROJECT_VERSION}" STREQUAL "")
    set(PROJECT_VER "${RLCD_PROJECT_VERSION}")
elseif(DEFINED ENV{RLCD_PROJECT_VERSION} AND
       NOT "$ENV{RLCD_PROJECT_VERSION}" STREQUAL "")
    set(PROJECT_VER "$ENV{RLCD_PROJECT_VERSION}")
else()
    set(PROJECT_VER "0.27.0")
endif()
set(COMPONENTS app)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rlcd_firmware)

# The pinned Waveshare adapter contains one private helper which is currently
# unused. Keep the upstream checkout clean while silencing only that warning.
idf_component_get_property(st7305_component_lib u8g2_st7305 COMPONENT_LIB)
target_compile_options(${st7305_component_lib} PRIVATE -Wno-unused-function)

# The upstream QR wrapper logs its input at INFO level. The input contains the
# temporary access-point password, so compile those logs out instead of relying
# on a runtime log-level setting.
idf_component_get_property(qrcode_component_lib qrcode COMPONENT_LIB)
target_compile_definitions(${qrcode_component_lib} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_NONE)
