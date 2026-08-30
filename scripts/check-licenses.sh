#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

require_file() {
    if [[ ! -f "$1" ]]; then
        echo "缺少许可文件: $1" >&2
        exit 1
    fi
}

compare_file() {
    local tracked_file="$1"
    local upstream_file="$2"
    local label="$3"

    require_file "${tracked_file}"
    require_file "${upstream_file}"
    if ! cmp -s -- "${tracked_file}" "${upstream_file}"; then
        echo "${label} 许可文本与固定依赖不一致" >&2
        echo "  仓库: ${tracked_file}" >&2
        echo "  上游: ${upstream_file}" >&2
        exit 1
    fi
}

compare_text_file() {
    local tracked_file="$1"
    local upstream_file="$2"
    local label="$3"

    require_file "${tracked_file}"
    require_file "${upstream_file}"
    # Command substitution ignores a final newline. The ESP-SR archive omits
    # one, while the tracked text follows normal repository formatting.
    if [[ "$(<"${tracked_file}")" != "$(<"${upstream_file}")" ]]; then
        echo "${label} 许可文本与固定依赖不一致" >&2
        echo "  仓库: ${tracked_file}" >&2
        echo "  上游: ${upstream_file}" >&2
        exit 1
    fi
}

require_notice_entry() {
    local entry="$1"
    if ! grep -Fq -- "${entry}" "${RLCD_PROJECT_DIR}/NOTICE.md"; then
        echo "NOTICE.md 缺少许可条目: ${entry}" >&2
        exit 1
    fi
}

require_manifest_entry() {
    local manifest="$1"
    local entry="$2"
    local label="$3"

    require_file "${manifest}"
    if ! grep -Fxq -- "${entry}" "${manifest}"; then
        echo "${label} 组件清单缺少或改变了许可元数据: ${entry}" >&2
        exit 1
    fi
}

"${RLCD_PROJECT_DIR}/scripts/bootstrap.sh" --check >/dev/null

required_files=(
    "LICENSE"
    "NOTICE.md"
    "LICENSES/Cadence-Xtensa-MIT.txt"
    "LICENSES/cJSON.txt"
    "LICENSES/dl_fft-MIT.txt"
    "LICENSES/ESP-SR.txt"
    "LICENSES/FreeRTOS.txt"
    "LICENSES/GCC-Runtime-Library-Exception-3.1.txt"
    "LICENSES/GPL-2.0-only.txt"
    "LICENSES/GPL-3.0-or-later.txt"
    "LICENSES/HTTP-Parser.txt"
    "LICENSES/Logisoso.txt"
    "LICENSES/Mbed-TLS.txt"
    "LICENSES/Nayuki-QR-Code-Generator.txt"
    "LICENSES/Newlib.txt"
    "LICENSES/TLSF-BSD-3-Clause.txt"
    "LICENSES/U8g2.txt"
    "LICENSES/WPA-Supplicant-COPYING.txt"
    "LICENSES/WPA-Supplicant.txt"
    "LICENSES/WenQuanYi-Bitmap-Song.txt"
    "LICENSES/lwIP.txt"
)
for relative_path in "${required_files[@]}"; do
    require_file "${RLCD_PROJECT_DIR}/${relative_path}"
done

compare_file "${RLCD_PROJECT_DIR}/LICENSE" "${RLCD_IDF_DIR}/LICENSE" "Apache-2.0"
compare_file "${RLCD_PROJECT_DIR}/LICENSE" \
    "${RLCD_IDF_DIR}/components/esp_wifi/lib/LICENSE" "ESP32 Wi-Fi libraries"
compare_file "${RLCD_PROJECT_DIR}/LICENSE" \
    "${RLCD_IDF_DIR}/components/esp_phy/lib/LICENSE" "ESP32 PHY libraries"
compare_file "${RLCD_PROJECT_DIR}/LICENSE" \
    "${RLCD_IDF_DIR}/components/esp_coex/lib/LICENSE" "ESP32 coexistence libraries"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/U8g2.txt" \
    "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/LICENSE" "U8g2"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/FreeRTOS.txt" \
    "${RLCD_IDF_DIR}/components/freertos/FreeRTOS-Kernel/LICENSE.md" "FreeRTOS"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/Mbed-TLS.txt" \
    "${RLCD_IDF_DIR}/components/mbedtls/mbedtls/LICENSE" "Mbed TLS"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/HTTP-Parser.txt" \
    "${RLCD_IDF_DIR}/components/http_parser/LICENSE.txt" "HTTP Parser"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/cJSON.txt" \
    "${RLCD_CJSON_DIR}/LICENSE" "Espressif cJSON package"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/cJSON.txt" \
    "${RLCD_CJSON_DIR}/cJSON/LICENSE" "upstream cJSON"
compare_file "${RLCD_PROJECT_DIR}/LICENSE" \
    "${RLCD_ESP_WEBSOCKET_CLIENT_DIR}/LICENSE" \
    "ESP WebSocket Client"
compare_text_file "${RLCD_PROJECT_DIR}/LICENSES/ESP-SR.txt" \
    "${RLCD_ESP_SR_DIR}/LICENSE" "ESP-SR"
compare_file "${RLCD_PROJECT_DIR}/LICENSE" \
    "${RLCD_ESP_DSP_DIR}/LICENSE" "ESP-DSP"
require_manifest_entry "${RLCD_ESP_SR_DIR}/idf_component.yml" \
    "version: ${ESP_SR_VERSION}" "ESP-SR"
require_manifest_entry "${RLCD_ESP_SR_DIR}/idf_component.yml" \
    "  commit_sha: 2f8c4b0459db5bbb39abd77adae27962d6d94bcb" \
    "ESP-SR"
require_manifest_entry "${RLCD_ESP_DSP_DIR}/idf_component.yml" \
    "version: ${ESP_DSP_VERSION}" "ESP-DSP"
require_manifest_entry "${RLCD_ESP_DSP_DIR}/idf_component.yml" \
    "  commit_sha: 196825deaa4848b2c8e87b6126491cd7fc87e5bf" \
    "ESP-DSP"
require_manifest_entry "${RLCD_DL_FFT_DIR}/idf_component.yml" \
    "license: MIT" "dl_fft"
require_manifest_entry "${RLCD_DL_FFT_DIR}/idf_component.yml" \
    "version: ${DL_FFT_VERSION}" "dl_fft"
require_manifest_entry "${RLCD_DL_FFT_DIR}/idf_component.yml" \
    "  commit_sha: a8a7b60ea5bfd6ce46960ea061641fffa9589440" \
    "dl_fft"
require_manifest_entry "${RLCD_CJSON_DIR}/idf_component.yml" \
    "version: ${CJSON_VERSION}" "cJSON"
require_manifest_entry "${RLCD_CJSON_DIR}/idf_component.yml" \
    "  commit_sha: 721d625669f4e4fdfe6e02cf7e11f15b33f13e3a" \
    "cJSON"
if ! grep -Fq -- "SPDX-License-Identifier: Apache-2.0" \
    "${RLCD_DL_FFT_DIR}/base/isa/esp32s3/dl_fft2r_fc32_aes3.S"; then
    echo "dl_fft ESP32-S3 源文件的 Apache-2.0 标识已改变" >&2
    exit 1
fi
compare_file "${RLCD_PROJECT_DIR}/LICENSES/lwIP.txt" \
    "${RLCD_IDF_DIR}/components/lwip/lwip/COPYING" "lwIP"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/WPA-Supplicant.txt" \
    "${RLCD_IDF_DIR}/components/wpa_supplicant/README" "wpa_supplicant"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/WPA-Supplicant-COPYING.txt" \
    "${RLCD_IDF_DIR}/components/wpa_supplicant/COPYING" "wpa_supplicant COPYING"
compare_file "${RLCD_PROJECT_DIR}/LICENSE" \
    "${RLCD_QRCODE_COMPONENT_DIR}/LICENSE" "Espressif QR Code"
compare_file "${RLCD_PROJECT_DIR}/LICENSE" \
    "${RLCD_WAVESHARE_AUDIO_CODEC_DIR}/LICENSE" "Espressif esp_codec_dev"

xtensa_license_dir="$(find "${RLCD_IDF_TOOLS_DIR}/tools/xtensa-esp-elf" -type d \
    -path '*/xtensa-esp-elf/share/licenses' -print -quit)"
if [[ -z "${xtensa_license_dir}" ]]; then
    echo "未找到 Xtensa 工具链许可目录" >&2
    exit 1
fi
compare_file "${RLCD_PROJECT_DIR}/LICENSES/Newlib.txt" \
    "${xtensa_license_dir}/newlib/COPYING.NEWLIB" "newlib"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/GPL-2.0-only.txt" \
    "${xtensa_license_dir}/picolibc/COPYING.GPL2" "GPL-2.0"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/GPL-3.0-or-later.txt" \
    "${xtensa_license_dir}/gcc/COPYING3" "GPL-3.0"
compare_file "${RLCD_PROJECT_DIR}/LICENSES/GCC-Runtime-Library-Exception-3.1.txt" \
    "${xtensa_license_dir}/gcc/COPYING.RUNTIME" "GCC runtime exception"

notice_entries=(
    "v${ESP_IDF_VERSION}"
    "${ESP_IDF_COMMIT}"
    "${WAVESHARE_COMMIT}"
    "FreeRTOS Kernel"
    "HTTP Parser"
    "cJSON"
    "Copyright (c) 2009-2017 Dave Gamble and cJSON contributors"
    "ESP-SR"
    "v${ESP_SR_VERSION}"
    "2f8c4b0459db5bbb39abd77adae27962d6d94bcb"
    "ESPRESSIF MIT"
    "ESP-DSP"
    "v${ESP_DSP_VERSION}"
    "196825deaa4848b2c8e87b6126491cd7fc87e5bf"
    "dl_fft"
    "v${DL_FFT_VERSION}"
    "a8a7b60ea5bfd6ce46960ea061641fffa9589440"
    "组件清单声明 MIT"
    "Espressif 组件 v${CJSON_VERSION}"
    "721d625669f4e4fdfe6e02cf7e11f15b33f13e3a"
    "ESP WebSocket Client"
    "v${ESP_WEBSOCKET_CLIENT_VERSION}"
    "${ESP_WEBSOCKET_CLIENT_COMMIT}"
    "Copyright 2015-2025 Espressif Systems (Shanghai) CO LTD"
    "Espressif QR Code"
    "Mbed TLS"
    "lwIP"
    "newlib"
    "libgcc"
    "libstdc++"
    "TLSF"
    "Cadence/Tensilica"
    "U8g2"
    "u8g2_st7305"
    "wpa_supplicant"
    "Copyright (C) 2021 Amazon.com, Inc. or its affiliates"
    "Copyright (c) 2001, 2002 Swedish Institute of Computer Science"
    "Copyright (c) 2002-2022, Jouni Malinen"
    "Copyright The Mbed TLS Contributors"
    "Copyright (c) 2016, olikraus@gmail.com"
    "Copyright 2026 Waveshare"
    "Copyright (c) Project Nayuki"
    "${IDF_EXTRA_COMPONENTS_COMMIT}"
    "esp_codec_dev"
    "1.3.5"
    "9b35bca1a6db3d989936f228d6e28f33089fa9e7"
    "ES8311"
    "ES7210"
)
for notice_entry in "${notice_entries[@]}"; do
    require_notice_entry "${notice_entry}"
done

expected_fonts="$(printf '%s\n' \
    u8g2_font_5x8_tf \
    u8g2_font_6x13_tf \
    u8g2_font_helvB14_tf \
    u8g2_font_helvB18_tf \
    u8g2_font_helvB24_tf \
    u8g2_font_logisoso20_tf \
    u8g2_font_logisoso78_tn \
    u8g2_font_wqy16_t_gb2312)"
actual_fonts="$(grep -oE 'u8g2_font_[A-Za-z0-9_]+' \
    "${RLCD_PROJECT_DIR}/src/display/display.c" | sort -u)"
if [[ "${actual_fonts}" != "${expected_fonts}" ]]; then
    echo "显示字体清单已变化，请同步更新 NOTICE.md 和 LICENSES/" >&2
    diff -u <(printf '%s\n' "${expected_fonts}") <(printf '%s\n' "${actual_fonts}") || true
    exit 1
fi
while IFS= read -r font_name; do
    require_notice_entry "${font_name}"
done <<< "${expected_fonts}"

if git -C "${RLCD_PROJECT_DIR}" ls-files | \
    grep -E '(^|/)(third_party|managed_components)/' >/dev/null; then
    echo "Git 仓库中不应包含第三方源码目录" >&2
    exit 1
fi

echo "许可材料检查通过；第三方源码仍位于 Git 仓库之外。"
