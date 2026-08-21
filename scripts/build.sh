#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

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

export IDF_TOOLS_PATH="${RLCD_IDF_TOOLS_DIR}"
# ESP-IDF's export script prepares the pinned compiler and Python environment.
# shellcheck disable=SC1091
source "${RLCD_IDF_DIR}/export.sh"

cd "${RLCD_PROJECT_DIR}"
# merge-bin depends on the normal build target, so this performs one build and
# then creates the complete first-install/recovery image without touching a
# serial port. The standalone application image is the browser OTA artifact.
idf.py -B build merge-bin --output rlcd_firmware_factory.bin
install -m 0644 build/rlcd_firmware.bin build/rlcd_firmware_ota.bin

partition_summary="$(python "${RLCD_IDF_DIR}/components/partition_table/gen_esp32part.py" \
  build/partition_table/partition-table.bin)"
for required_partition in 'otadata' 'ota_0' 'ota_1'; do
  if ! grep -q -- "${required_partition}" <<<"${partition_summary}"; then
    echo "构建分区表缺少 ${required_partition}，拒绝生成可发布固件" >&2
    exit 1
  fi
done

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
    > SHA256SUMS
)
