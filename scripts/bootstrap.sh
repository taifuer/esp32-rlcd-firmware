#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

check_only=false
if [[ "${1:-}" == "--check" ]]; then
    check_only=true
elif [[ $# -ne 0 ]]; then
    echo "用法: $0 [--check]" >&2
    exit 2
fi

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "缺少命令: $1" >&2
        exit 1
    fi
}

verify_checkout() {
    local path="$1"
    local expected_commit="$2"
    local label="$3"

    if [[ ! -d "${path}/.git" ]]; then
        echo "${label} 尚未安装: ${path}" >&2
        return 1
    fi

    local actual_commit
    actual_commit="$(git -C "${path}" rev-parse HEAD)"
    if [[ "${actual_commit}" != "${expected_commit}" ]]; then
        echo "${label} 版本不匹配" >&2
        echo "  期望: ${expected_commit}" >&2
        echo "  实际: ${actual_commit}" >&2
        return 1
    fi
    echo "${label}: ${actual_commit}"
}

require_command git
require_command python3

if [[ "${check_only}" == true ]]; then
    verify_checkout "${RLCD_IDF_DIR}" "${ESP_IDF_COMMIT}" "ESP-IDF v${ESP_IDF_VERSION}"
    verify_checkout "${RLCD_WAVESHARE_DIR}" "${WAVESHARE_COMMIT}" "Waveshare board sources"
    verify_checkout "${RLCD_IDF_EXTRA_COMPONENTS_DIR}" \
        "${IDF_EXTRA_COMPONENTS_COMMIT}" "Espressif QR Code component"
    if [[ ! -f "${RLCD_QRCODE_COMPONENT_DIR}/include/qrcode.h" ]]; then
        echo "二维码组件不完整: ${RLCD_QRCODE_COMPONENT_DIR}" >&2
        exit 1
    fi
    if [[ ! -f "${RLCD_WAVESHARE_AUDIO_CODEC_DIR}/include/esp_codec_dev.h" ||
          ! -f "${RLCD_WAVESHARE_AUDIO_CODEC_DIR}/device/include/es8311_codec.h" ||
          ! -f "${RLCD_WAVESHARE_AUDIO_CODEC_DIR}/device/include/es7210_adc.h" ]]; then
        echo "音频 codec 组件不完整: ${RLCD_WAVESHARE_AUDIO_CODEC_DIR}" >&2
        exit 1
    fi
    if [[ ! -f "${RLCD_IDF_TOOLS_DIR}/idf-env.json" ]]; then
        echo "ESP-IDF 工具尚未安装: ${RLCD_IDF_TOOLS_DIR}" >&2
        exit 1
    fi
    echo "ESP-IDF tools: ${RLCD_IDF_TOOLS_DIR}"
    echo "外部依赖检查通过；Git 仓库内未保存这些内容。"
    exit 0
fi

mkdir -p "${RLCD_EXTERNAL_DEPS_DIR}/toolchains"
mkdir -p "${RLCD_EXTERNAL_DEPS_DIR}/sources/waveshareteam"
mkdir -p "${RLCD_EXTERNAL_DEPS_DIR}/sources/espressif"

if [[ ! -d "${RLCD_IDF_DIR}/.git" ]]; then
    git clone --branch "v${ESP_IDF_VERSION}" --depth 1 --recursive --shallow-submodules \
        "${ESP_IDF_REPOSITORY_URL}" "${RLCD_IDF_DIR}"
fi
verify_checkout "${RLCD_IDF_DIR}" "${ESP_IDF_COMMIT}" "ESP-IDF v${ESP_IDF_VERSION}"

if [[ ! -d "${RLCD_WAVESHARE_DIR}/.git" ]]; then
    git clone --filter=blob:none --no-checkout \
        "${WAVESHARE_REPOSITORY_URL}" "${RLCD_WAVESHARE_DIR}"
    git -C "${RLCD_WAVESHARE_DIR}" sparse-checkout init --cone
    git -C "${RLCD_WAVESHARE_DIR}" sparse-checkout set \
        "${WAVESHARE_COMPONENTS_RELATIVE_PATH}/u8g2" \
        "${WAVESHARE_COMPONENTS_RELATIVE_PATH}/u8g2_st7305" \
        "${WAVESHARE_AUDIO_CODEC_RELATIVE_PATH}"
    git -C "${RLCD_WAVESHARE_DIR}" checkout --detach "${WAVESHARE_COMMIT}"
fi
verify_checkout "${RLCD_WAVESHARE_DIR}" "${WAVESHARE_COMMIT}" "Waveshare board sources"

if [[ "$(git -C "${RLCD_WAVESHARE_DIR}" config --bool core.sparseCheckout || true)" == true ]]; then
    git -C "${RLCD_WAVESHARE_DIR}" sparse-checkout add \
        "${WAVESHARE_AUDIO_CODEC_RELATIVE_PATH}"
fi

if [[ ! -f "${RLCD_WAVESHARE_AUDIO_CODEC_DIR}/include/esp_codec_dev.h" ]]; then
    echo "未找到音频 codec 依赖: ${RLCD_WAVESHARE_AUDIO_CODEC_DIR}" >&2
    exit 1
fi

if [[ ! -d "${RLCD_IDF_EXTRA_COMPONENTS_DIR}/.git" ]]; then
    git clone --filter=blob:none --no-checkout \
        "${IDF_EXTRA_COMPONENTS_REPOSITORY_URL}" "${RLCD_IDF_EXTRA_COMPONENTS_DIR}"
    git -C "${RLCD_IDF_EXTRA_COMPONENTS_DIR}" sparse-checkout init --cone
    git -C "${RLCD_IDF_EXTRA_COMPONENTS_DIR}" sparse-checkout set \
        "${QRCODE_COMPONENT_RELATIVE_PATH}"
    git -C "${RLCD_IDF_EXTRA_COMPONENTS_DIR}" checkout --detach \
        "${IDF_EXTRA_COMPONENTS_COMMIT}"
fi
verify_checkout "${RLCD_IDF_EXTRA_COMPONENTS_DIR}" \
    "${IDF_EXTRA_COMPONENTS_COMMIT}" "Espressif QR Code component"

if [[ ! -f "${RLCD_IDF_TOOLS_DIR}/idf-env.json" ]]; then
    export IDF_TOOLS_PATH="${RLCD_IDF_TOOLS_DIR}"
    "${RLCD_IDF_DIR}/install.sh" esp32s3
fi

echo "外部依赖准备完成: ${RLCD_EXTERNAL_DEPS_DIR}"
echo "下一步: ./scripts/build.sh"
