#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

readonly RLCD_MODEL_PARTITION_OFFSET=$((0x610000))
readonly RLCD_MODEL_PARTITION_SIZE=$((0x300000))

partition_number_to_bytes() {
  local value="$1"
  if [[ "${value}" =~ ^0[xX][0-9a-fA-F]+$ ||
        "${value}" =~ ^[0-9]+$ ]]; then
    printf '%u\n' "$((value))"
    return 0
  fi
  if [[ "${value}" =~ ^([0-9]+)([KM])$ ]]; then
    local multiplier=1024
    if [[ "${BASH_REMATCH[2]}" == M ]]; then
      multiplier=$((1024 * 1024))
    fi
    printf '%u\n' "$((10#${BASH_REMATCH[1]} * multiplier))"
    return 0
  fi
  return 1
}

if [[ ! -f "${RLCD_IDF_DIR}/export.sh" ]]; then
  echo "未找到 ESP-IDF v${ESP_IDF_VERSION}: ${RLCD_IDF_DIR}" >&2
  echo "请先执行: ./scripts/bootstrap.sh" >&2
  exit 1
fi

if [[ ! -f "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/CMakeLists.txt" ||
      ! -f "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2_st7305/CMakeLists.txt" ]]; then
  echo "未找到外部显示依赖: ${RLCD_WAVESHARE_COMPONENTS_DIR}" >&2
  echo "请先执行: ./scripts/bootstrap.sh" >&2
  exit 1
fi

if [[ ! -f "${RLCD_WAVESHARE_AUDIO_CODEC_DIR}/CMakeLists.txt" ]]; then
  echo "未找到外部音频 codec 依赖: ${RLCD_WAVESHARE_AUDIO_CODEC_DIR}" >&2
  echo "请先执行: ./scripts/bootstrap.sh" >&2
  exit 1
fi

export IDF_TOOLS_PATH="${RLCD_IDF_TOOLS_DIR}"
# ESP-IDF's export script prepares the pinned compiler and Python environment.
# shellcheck disable=SC1091
source "${RLCD_IDF_DIR}/export.sh"

cd "${RLCD_PROJECT_DIR}"
# merge-bin depends on the normal build target, so this performs one build and
# then creates the complete first-install/recovery image without touching a
# serial port. The standalone application image is the browser OTA artifact.
idf.py -B build \
  -D "RLCD_PROJECT_VERSION=${RLCD_PROJECT_VERSION:-}" \
  merge-bin --output rlcd_firmware_factory.bin
install -m 0644 build/rlcd_firmware.bin build/rlcd_firmware_ota.bin

if ! grep -Fxq 'CONFIG_FATFS_API_ENCODING_UTF_8=y' sdkconfig || \
   ! grep -Fxq 'CONFIG_FATFS_MAX_LFN=127' sdkconfig; then
  echo '音乐文件名需要 FatFs UTF-8 和 127 字符 LFN；请按 docs/development.md 更新本机 sdkconfig。' >&2
  exit 1
fi
mp3_decoder_objects="$(sed -nE \
  's/.*libesp_audio_codec\.a\((esp_[[:alnum:]_]+_dec\.c\.obj)\).*/\1/p' \
  build/rlcd_firmware.map | LC_ALL=C sort -u)"
if [[ "${mp3_decoder_objects}" != 'esp_mp3_dec.c.obj' ]]; then
  echo '音频解码链接范围改变，请核对组件、资源与许可材料。' >&2
  exit 1
fi

partition_summary="$(python "${RLCD_IDF_DIR}/components/partition_table/gen_esp32part.py" \
  build/partition_table/partition-table.bin)"
for required_partition in 'otadata' 'ota_0' 'ota_1'; do
  if ! grep -Eq -- "^${required_partition}," <<<"${partition_summary}"; then
    echo "构建分区表缺少 ${required_partition}，拒绝生成可发布固件" >&2
    exit 1
  fi
done

model_partition_line="$(grep -E '^model,' <<<"${partition_summary}" || true)"
if [[ -z "${model_partition_line}" ]]; then
  echo "构建分区表缺少 model，拒绝生成离线语音固件" >&2
  exit 1
fi
IFS=',' read -r model_name model_type model_subtype model_offset_value \
  model_size_value _ <<<"${model_partition_line}"
if [[ "${model_name}" != model || "${model_type}" != data ||
      "${model_subtype}" != spiffs ]]; then
  echo "model 分区类型无效: ${model_partition_line}" >&2
  exit 1
fi
if ! model_offset_bytes="$(partition_number_to_bytes \
    "${model_offset_value}")" ||
   ! model_size_bytes="$(partition_number_to_bytes \
    "${model_size_value}")"; then
  echo "无法解析 model 分区: ${model_partition_line}" >&2
  exit 1
fi
if ((model_offset_bytes != RLCD_MODEL_PARTITION_OFFSET ||
     model_size_bytes != RLCD_MODEL_PARTITION_SIZE)); then
  printf 'model 分区必须固定为 offset=0x%x size=0x%x，实际为 offset=0x%x size=0x%x\n' \
    "${RLCD_MODEL_PARTITION_OFFSET}" "${RLCD_MODEL_PARTITION_SIZE}" \
    "${model_offset_bytes}" "${model_size_bytes}" >&2
  exit 1
fi

model_path="${RLCD_PROJECT_DIR}/build/srmodels/srmodels.bin"
model_manifest_path="${RLCD_PROJECT_DIR}/src/audio/include/voice_model_manifest.h"
factory_path="${RLCD_PROJECT_DIR}/build/rlcd_firmware_factory.bin"
if [[ ! -s "${model_path}" ]]; then
  echo "未生成离线语音模型: ${model_path}" >&2
  exit 1
fi
model_bytes="$(stat -c '%s' "${model_path}")"
if [[ ! -f "${model_manifest_path}" ]]; then
  echo "未找到离线语音模型清单: ${model_manifest_path}" >&2
  exit 1
fi
manifest_model_bytes="$(sed -nE \
  's/^#define AUDIO_VOICE_MODEL_IMAGE_SIZE ([0-9]+)U$/\1/p' \
  "${model_manifest_path}")"
manifest_model_sha256="$(sed -nE \
  's/^[[:space:]]*"([0-9a-f]{64})"$/\1/p' \
  "${model_manifest_path}")"
if [[ -z "${manifest_model_bytes}" || -z "${manifest_model_sha256}" ]]; then
  echo "无法解析离线语音模型清单: ${model_manifest_path}" >&2
  exit 1
fi
read -r actual_model_sha256 _ < <(sha256sum "${model_path}")
if [[ "${model_bytes}" != "${manifest_model_bytes}" ||
      "${actual_model_sha256}" != "${manifest_model_sha256}" ]]; then
  printf '离线语音模型与固件清单不一致: size=%s/%s sha256=%s/%s\n' \
    "${model_bytes}" "${manifest_model_bytes}" \
    "${actual_model_sha256}" "${manifest_model_sha256}" >&2
  exit 1
fi
if ((model_bytes > RLCD_MODEL_PARTITION_SIZE)); then
  printf '离线语音模型超过 model 分区: %u > %u bytes\n' \
    "${model_bytes}" "${RLCD_MODEL_PARTITION_SIZE}" >&2
  exit 1
fi
if [[ ! -s "${factory_path}" ]]; then
  echo "未生成 Factory 合并镜像: ${factory_path}" >&2
  exit 1
fi
factory_bytes="$(stat -c '%s' "${factory_path}")"
if ((factory_bytes < RLCD_MODEL_PARTITION_OFFSET + model_bytes)); then
  echo "Factory 合并镜像未覆盖完整 model 区域" >&2
  exit 1
fi
if ! cmp --silent --bytes="${model_bytes}" \
    "${model_path}" "${factory_path}" 0 \
    "${RLCD_MODEL_PARTITION_OFFSET}"; then
  echo "Factory 合并镜像中的 model 内容与 srmodels.bin 不一致" >&2
  exit 1
fi
printf '离线语音模型已验证: offset=0x%x size=%u/%u bytes\n' \
  "${RLCD_MODEL_PARTITION_OFFSET}" "${model_bytes}" \
  "${RLCD_MODEL_PARTITION_SIZE}"

RLCD_QRCODE_ARCHIVE="${RLCD_PROJECT_DIR}/build/esp-idf/qrcode/libqrcode.a"
if [[ ! -f "${RLCD_QRCODE_ARCHIVE}" ]]; then
  echo "未生成二维码组件: ${RLCD_QRCODE_ARCHIVE}" >&2
  exit 1
fi
if grep -aFq -- "Encoding below text" "${RLCD_QRCODE_ARCHIVE}"; then
  echo "二维码组件仍包含输入日志，可能泄漏临时热点密码" >&2
  exit 1
fi

(
  cd build
  sha256sum \
    bootloader/bootloader.bin \
    partition_table/partition-table.bin \
    rlcd_firmware.bin \
    rlcd_firmware_factory.bin \
    rlcd_firmware_ota.bin \
    srmodels/srmodels.bin \
    > SHA256SUMS
)
