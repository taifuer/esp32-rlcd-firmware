#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

readonly RLCD_APP_OFFSET="0x10000"
readonly RLCD_APP_PARTITION_SIZE=$((0x300000))
readonly RLCD_MODEL_OFFSET="0x610000"
readonly RLCD_MODEL_PARTITION_SIZE=$((0x300000))

port="COM5"
baud="460800"
firmware_path="${RLCD_PROJECT_DIR}/build/rlcd_firmware_ota.bin"
model_path="${RLCD_PROJECT_DIR}/build/srmodels/srmodels.bin"
model_explicit=false
confirmed=false

usage() {
    cat <<'EOF'
用法: ./scripts/update-app.sh [--port COM5] [--baud 460800] [--firmware FILE] [--model FILE] --confirm

前提：开发板已手动进入 BOOT 下载模式。脚本在一次 write-flash 中固定写入 ota_0 应用
(0x10000) 与离线语音模型 (0x610000)，随后只重置 OTA 选择数据；不会覆盖位于 0x9000
的 NVS，因此保留 Wi-Fi 和设备偏好。v0.7.0 及更新版本的旧分区表虽然没有 model 条目，
但该固定区域原本未分配，v0.18.0 应用会按同一地址使用它。首次安装、分区恢复或完整清除
仍使用 scripts/flash.sh。指定版本化的 -ota.bin 时，脚本会优先使用同目录同版本的
-model.bin，也可通过 --model 明确指定。
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            port="${2:?--port 缺少参数}"
            shift 2
            ;;
        --baud)
            baud="${2:?--baud 缺少参数}"
            shift 2
            ;;
        --firmware)
            firmware_path="${2:?--firmware 缺少参数}"
            shift 2
            ;;
        --model)
            model_path="${2:?--model 缺少参数}"
            model_explicit=true
            shift 2
            ;;
        --confirm)
            confirmed=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "未知参数: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "${confirmed}" != true ]]; then
    echo "拒绝更新：必须显式添加 --confirm。" >&2
    usage >&2
    exit 2
fi
if [[ ! "${port}" =~ ^COM[0-9]+$ ]]; then
    echo "无效 Windows 串口: ${port}" >&2
    exit 2
fi
if [[ ! "${baud}" =~ ^[0-9]+$ ]]; then
    echo "无效波特率: ${baud}" >&2
    exit 2
fi
if [[ ! -f "${firmware_path}" ]]; then
    echo "找不到 OTA 固件: ${firmware_path}" >&2
    echo "请先执行: ./scripts/build.sh" >&2
    exit 1
fi
if [[ "${model_explicit}" != true ]]; then
    firmware_basename="$(basename -- "${firmware_path}")"
    if [[ "${firmware_basename}" == *-ota.bin ]]; then
        packaged_model_path="$(dirname -- "${firmware_path}")/${firmware_basename%-ota.bin}-model.bin"
        if [[ -f "${packaged_model_path}" ]]; then
            model_path="${packaged_model_path}"
        fi
    fi
fi
if [[ ! -f "${model_path}" ]]; then
    echo "找不到离线语音模型: ${model_path}" >&2
    echo "请先执行: ./scripts/build.sh，或通过 --model 指定 srmodels.bin" >&2
    exit 1
fi
for command_name in powershell.exe wslpath sha256sum realpath stat; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "缺少命令: ${command_name}" >&2
        exit 1
    fi
done

firmware_dir="$(cd -- "$(dirname -- "${firmware_path}")" && pwd)"
firmware_name="$(basename -- "${firmware_path}")"
firmware_path="${firmware_dir}/${firmware_name}"
model_dir="$(cd -- "$(dirname -- "${model_path}")" && pwd)"
model_name="$(basename -- "${model_path}")"
model_path="${model_dir}/${model_name}"
case "${firmware_name}" in
    rlcd_firmware.bin|rlcd_firmware_ota.bin|*-ota.bin)
        ;;
    *)
        echo "拒绝把非 OTA 文件作为应用镜像写入: ${firmware_name}" >&2
        exit 2
        ;;
esac
manifest_path="${firmware_dir}/SHA256SUMS"
verify_sha256_manifest_entry \
    "${manifest_path}" "${firmware_path}" "${firmware_name}" \
    "${firmware_name}"
model_relative_path="$(realpath --relative-to="${firmware_dir}" \
    "${model_path}")"
case "${model_relative_path}" in
    ..|../*)
        echo "模型必须位于固件校验目录中: ${firmware_dir}" >&2
        echo "请把模型与固件放入同一发布目录，或其子目录。" >&2
        exit 1
        ;;
esac
verify_sha256_manifest_entry \
    "${manifest_path}" "${model_path}" "${model_relative_path}" \
    "${model_relative_path}"
model_sha256="$(sha256sum "${model_path}" | awk '{print $1}')"

firmware_bytes="$(stat -c '%s' "${firmware_path}")"
model_bytes="$(stat -c '%s' "${model_path}")"
if ((firmware_bytes == 0 || firmware_bytes > RLCD_APP_PARTITION_SIZE)); then
    echo "应用镜像大小无效: ${firmware_bytes}/${RLCD_APP_PARTITION_SIZE} bytes" >&2
    exit 1
fi
if ((model_bytes == 0 || model_bytes > RLCD_MODEL_PARTITION_SIZE)); then
    echo "模型镜像大小无效: ${model_bytes}/${RLCD_MODEL_PARTITION_SIZE} bytes" >&2
    exit 1
fi

"${RLCD_SCRIPT_DIR}/install-esptool-windows.sh"
esptool_exe="${RLCD_ESPTOOL_WINDOWS_DIR}/esptool.exe"
port_check_script="$(wslpath -w "${RLCD_SCRIPT_DIR}/check-esp32-port-windows.ps1")"
windows_firmware_path="$(wslpath -w "${firmware_path}")"
windows_model_path="$(wslpath -w "${model_path}")"
firmware_sha256="$(sha256sum "${firmware_path}" | awk '{print $1}')"

powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "${port_check_script}" -Port "${port}"

echo "芯片连接测试：${port}"
"${esptool_exe}" --chip esp32s3 --port "${port}" --baud 115200 \
    --before no-reset --after no-reset chip-id

printf '即将串行更新 OTA 应用: %s\n' "${windows_firmware_path}"
printf '  offset=%s size=%u SHA-256=%s\n' \
    "${RLCD_APP_OFFSET}" "${firmware_bytes}" "${firmware_sha256}"
printf '即将串行更新离线语音模型: %s\n' "${windows_model_path}"
printf '  offset=%s size=%u SHA-256=%s\n' \
    "${RLCD_MODEL_OFFSET}" "${model_bytes}" "${model_sha256}"
"${esptool_exe}" --chip esp32s3 --port "${port}" --baud "${baud}" \
    --before no-reset --after no-reset write-flash \
    --flash-mode dio --flash-freq 80m --flash-size 16MB \
    "${RLCD_APP_OFFSET}" "${windows_firmware_path}" \
    "${RLCD_MODEL_OFFSET}" "${windows_model_path}"

# ota_0 has been verified by esptool at this point. Clearing only otadata makes
# the ESP-IDF bootloader select ota_0 without touching the NVS credentials.
"${esptool_exe}" --chip esp32s3 --port "${port}" --baud "${baud}" \
    --before no-reset --after no-reset erase-region 0xd000 0x2000

cat <<EOF
应用和离线语音模型写入及校验完成，NVS/Wi-Fi/设备偏好保持不变。
芯片仍停在下载模式，请：
  1. 长按 PWR 关机；
  2. 不要按 BOOT，短按 PWR 正常开机。
EOF
