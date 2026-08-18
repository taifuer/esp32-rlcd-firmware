cmake_minimum_required(VERSION 3.16)

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

set(PROJECT_VER "0.2.0")
set(COMPONENTS app)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rlcd_firmware)

# The pinned Waveshare adapter contains one private helper which is currently
# unused. Keep the upstream checkout clean while silencing only that warning.
idf_component_get_property(st7305_component_lib u8g2_st7305 COMPONENT_LIB)
target_compile_options(${st7305_component_lib} PRIVATE -Wno-unused-function)
