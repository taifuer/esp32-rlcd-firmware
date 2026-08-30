#!/usr/bin/env bash

# Shared path and version discovery for repository scripts. This file is meant
# to be sourced, not executed directly.

RLCD_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RLCD_PROJECT_DIR="$(cd -- "${RLCD_SCRIPT_DIR}/.." && pwd)"

# shellcheck disable=SC1091
source "${RLCD_PROJECT_DIR}/tool-versions.env"

if [[ -n "${RLCD_DEPS_DIR:-}" ]]; then
    RLCD_EXTERNAL_DEPS_DIR="${RLCD_DEPS_DIR}"
elif [[ -d "${RLCD_PROJECT_DIR}/../../third_party" ]]; then
    # Layout used by the current WSL workspace.
    RLCD_EXTERNAL_DEPS_DIR="$(cd -- "${RLCD_PROJECT_DIR}/../../third_party" && pwd)"
else
    # A fresh standalone clone keeps downloaded dependencies beside, not in,
    # the Git repository.
    RLCD_EXTERNAL_DEPS_DIR="${RLCD_PROJECT_DIR}/../esp32-rlcd-firmware-deps"
fi

RLCD_IDF_DIR="${RLCD_EXTERNAL_DEPS_DIR}/toolchains/esp-idf-v${ESP_IDF_VERSION}"
RLCD_IDF_TOOLS_DIR="${RLCD_EXTERNAL_DEPS_DIR}/toolchains/espressif-v${ESP_IDF_VERSION}"
RLCD_WAVESHARE_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/waveshareteam/ESP32-S3-RLCD-4.2"
RLCD_WAVESHARE_COMPONENTS_DIR="${RLCD_WAVESHARE_DIR}/${WAVESHARE_COMPONENTS_RELATIVE_PATH}"
RLCD_WAVESHARE_AUDIO_CODEC_DIR="${RLCD_WAVESHARE_DIR}/${WAVESHARE_AUDIO_CODEC_RELATIVE_PATH}"
RLCD_IDF_EXTRA_COMPONENTS_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/espressif/idf-extra-components"
RLCD_QRCODE_COMPONENT_DIR="${RLCD_IDF_EXTRA_COMPONENTS_DIR}/${QRCODE_COMPONENT_RELATIVE_PATH}"
RLCD_ESP_SR_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/espressif/espressif__esp-sr"
RLCD_ESP_SR_ARCHIVE="${RLCD_EXTERNAL_DEPS_DIR}/downloads/espressif__esp-sr-v${ESP_SR_VERSION}.zip"
RLCD_ESP_DSP_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/espressif/espressif__esp-dsp"
RLCD_ESP_DSP_ARCHIVE="${RLCD_EXTERNAL_DEPS_DIR}/downloads/espressif__esp-dsp-v${ESP_DSP_VERSION}.zip"
RLCD_DL_FFT_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/espressif/espressif__dl_fft"
RLCD_DL_FFT_ARCHIVE="${RLCD_EXTERNAL_DEPS_DIR}/downloads/espressif__dl_fft-v${DL_FFT_VERSION}.zip"
RLCD_CJSON_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/espressif/espressif__cjson"
RLCD_CJSON_ARCHIVE="${RLCD_EXTERNAL_DEPS_DIR}/downloads/espressif__cjson-v${CJSON_VERSION}.zip"
RLCD_ESP_PROTOCOLS_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/espressif/esp-protocols"
RLCD_ESP_WEBSOCKET_CLIENT_DIR="${RLCD_ESP_PROTOCOLS_DIR}/${ESP_WEBSOCKET_CLIENT_RELATIVE_PATH}"
RLCD_ESPTOOL_WINDOWS_DIR="${RLCD_EXTERNAL_DEPS_DIR}/toolchains/esptool-windows-v${ESPTOOL_WINDOWS_VERSION}"

export RLCD_PROJECT_DIR
export RLCD_EXTERNAL_DEPS_DIR
export RLCD_WAVESHARE_COMPONENTS_DIR
export RLCD_WAVESHARE_AUDIO_CODEC_DIR
export RLCD_QRCODE_COMPONENT_DIR
export RLCD_ESP_SR_DIR
export RLCD_ESP_DSP_DIR
export RLCD_DL_FFT_DIR
export RLCD_CJSON_DIR
export RLCD_ESP_WEBSOCKET_CLIENT_DIR
export IDF_COMPONENT_MANAGER=0

verify_sha256_manifest_entry() {
    local manifest_path="$1"
    local file_path="$2"
    local manifest_relative_path="$3"
    local label="${4:-${manifest_relative_path}}"
    local actual_sha256

    if [[ ! -f "${manifest_path}" ]]; then
        echo "缺少 SHA256SUMS: ${manifest_path}" >&2
        return 1
    fi
    if [[ ! -f "${file_path}" ]]; then
        echo "缺少待校验文件: ${file_path}" >&2
        return 1
    fi
    read -r actual_sha256 _ < <(sha256sum "${file_path}")
    if ! grep -Fqx -- "${actual_sha256}  ${manifest_relative_path}" \
        "${manifest_path}"; then
        echo "SHA256SUMS 未包含所选文件或摘要不一致: ${manifest_relative_path}" >&2
        return 1
    fi
    echo "${label}: OK"
}
