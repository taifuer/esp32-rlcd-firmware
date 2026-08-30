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

verify_file_sha256() {
    local path="$1"
    local expected_sha256="$2"
    local label="$3"

    if [[ ! -f "${path}" ]]; then
        echo "${label} 尚未下载: ${path}" >&2
        return 1
    fi
    local actual_sha256
    actual_sha256="$(sha256sum "${path}" | awk '{print $1}')"
    if [[ "${actual_sha256}" != "${expected_sha256}" ]]; then
        echo "${label} SHA-256 不匹配" >&2
        echo "  期望: ${expected_sha256}" >&2
        echo "  实际: ${actual_sha256}" >&2
        return 1
    fi
    echo "${label}: ${actual_sha256}"
}

install_component_archive() {
    local archive="$1"
    local url="$2"
    local expected_sha256="$3"
    local destination="$4"
    local label="$5"

    if [[ ! -f "${archive}" ]]; then
        local partial_archive="${archive}.part"
        curl --fail --location --retry 3 --output "${partial_archive}" \
            "${url}"
        verify_file_sha256 "${partial_archive}" "${expected_sha256}" \
            "${label} download"
        mv "${partial_archive}" "${archive}"
    fi
    verify_file_sha256 "${archive}" "${expected_sha256}" "${label} archive"

    if [[ ! -f "${destination}/CMakeLists.txt" ]]; then
        local extract_dir
        extract_dir="$(mktemp -d)"
        trap 'rm -rf -- "${extract_dir}"' RETURN
        unzip -q "${archive}" -d "${extract_dir}"
        if [[ ! -f "${extract_dir}/CMakeLists.txt" ]]; then
            echo "${label} 归档缺少 CMakeLists.txt" >&2
            return 1
        fi
        mv "${extract_dir}" "${destination}"
        trap - RETURN
    fi
}

require_command git
require_command python3
require_command sha256sum

if [[ "${check_only}" == true ]]; then
    verify_checkout "${RLCD_IDF_DIR}" "${ESP_IDF_COMMIT}" "ESP-IDF v${ESP_IDF_VERSION}"
    verify_checkout "${RLCD_WAVESHARE_DIR}" "${WAVESHARE_COMMIT}" "Waveshare board sources"
    verify_checkout "${RLCD_IDF_EXTRA_COMPONENTS_DIR}" \
        "${IDF_EXTRA_COMPONENTS_COMMIT}" "Espressif QR Code component"
    verify_file_sha256 "${RLCD_ESP_SR_ARCHIVE}" \
        "${ESP_SR_ARCHIVE_SHA256}" "ESP-SR v${ESP_SR_VERSION} archive"
    verify_file_sha256 "${RLCD_ESP_DSP_ARCHIVE}" \
        "${ESP_DSP_ARCHIVE_SHA256}" "ESP-DSP v${ESP_DSP_VERSION} archive"
    verify_file_sha256 "${RLCD_DL_FFT_ARCHIVE}" \
        "${DL_FFT_ARCHIVE_SHA256}" "dl_fft v${DL_FFT_VERSION} archive"
    verify_file_sha256 "${RLCD_CJSON_ARCHIVE}" \
        "${CJSON_ARCHIVE_SHA256}" "cJSON v${CJSON_VERSION} archive"
    verify_checkout "${RLCD_ESP_PROTOCOLS_DIR}" \
        "${ESP_WEBSOCKET_CLIENT_COMMIT}" \
        "ESP WebSocket Client v${ESP_WEBSOCKET_CLIENT_VERSION}"
    if [[ ! -f "${RLCD_ESP_SR_DIR}/include/esp32s3/esp_mn_iface.h" ||
          ! -f "${RLCD_ESP_SR_DIR}/model/movemodel.py" ]]; then
        echo "ESP-SR 源码不完整: ${RLCD_ESP_SR_DIR}" >&2
        exit 1
    fi
    if [[ ! -f "${RLCD_ESP_DSP_DIR}/modules/fft/include/dsps_fft2r.h" ||
          ! -f "${RLCD_DL_FFT_DIR}/dl_fft.h" ||
          ! -f "${RLCD_CJSON_DIR}/cJSON/cJSON.h" ]]; then
        echo "ESP-SR 传递依赖不完整" >&2
        exit 1
    fi
    if [[ ! -f "${RLCD_ESP_WEBSOCKET_CLIENT_DIR}/include/esp_websocket_client.h" ]]; then
        echo "ESP WebSocket Client 源码不完整: ${RLCD_ESP_WEBSOCKET_CLIENT_DIR}" >&2
        exit 1
    fi
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
mkdir -p "${RLCD_EXTERNAL_DEPS_DIR}/downloads"

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

require_command curl
require_command unzip
install_component_archive "${RLCD_ESP_SR_ARCHIVE}" \
    "${ESP_SR_ARCHIVE_URL}" "${ESP_SR_ARCHIVE_SHA256}" \
    "${RLCD_ESP_SR_DIR}" "ESP-SR v${ESP_SR_VERSION}"
install_component_archive "${RLCD_ESP_DSP_ARCHIVE}" \
    "${ESP_DSP_ARCHIVE_URL}" "${ESP_DSP_ARCHIVE_SHA256}" \
    "${RLCD_ESP_DSP_DIR}" "ESP-DSP v${ESP_DSP_VERSION}"
install_component_archive "${RLCD_DL_FFT_ARCHIVE}" \
    "${DL_FFT_ARCHIVE_URL}" "${DL_FFT_ARCHIVE_SHA256}" \
    "${RLCD_DL_FFT_DIR}" "dl_fft v${DL_FFT_VERSION}"
install_component_archive "${RLCD_CJSON_ARCHIVE}" \
    "${CJSON_ARCHIVE_URL}" "${CJSON_ARCHIVE_SHA256}" \
    "${RLCD_CJSON_DIR}" "cJSON v${CJSON_VERSION}"
if [[ ! -d "${RLCD_ESP_PROTOCOLS_DIR}/.git" ]]; then
    git clone --filter=blob:none --no-checkout \
        "${ESP_PROTOCOLS_REPOSITORY_URL}" "${RLCD_ESP_PROTOCOLS_DIR}"
    git -C "${RLCD_ESP_PROTOCOLS_DIR}" sparse-checkout init --cone
    git -C "${RLCD_ESP_PROTOCOLS_DIR}" sparse-checkout set \
        "${ESP_WEBSOCKET_CLIENT_RELATIVE_PATH}"
    git -C "${RLCD_ESP_PROTOCOLS_DIR}" checkout --detach \
        "${ESP_WEBSOCKET_CLIENT_COMMIT}"
fi
verify_checkout "${RLCD_ESP_PROTOCOLS_DIR}" \
    "${ESP_WEBSOCKET_CLIENT_COMMIT}" \
    "ESP WebSocket Client v${ESP_WEBSOCKET_CLIENT_VERSION}"
if [[ ! -f "${RLCD_ESP_SR_DIR}/include/esp32s3/esp_mn_iface.h" ||
      ! -f "${RLCD_ESP_SR_DIR}/model/movemodel.py" ]]; then
    echo "ESP-SR 源码不完整: ${RLCD_ESP_SR_DIR}" >&2
    exit 1
fi

if [[ ! -f "${RLCD_IDF_TOOLS_DIR}/idf-env.json" ]]; then
    export IDF_TOOLS_PATH="${RLCD_IDF_TOOLS_DIR}"
    "${RLCD_IDF_DIR}/install.sh" esp32s3
fi

echo "外部依赖准备完成: ${RLCD_EXTERNAL_DEPS_DIR}"
echo "下一步: ./scripts/build.sh"
