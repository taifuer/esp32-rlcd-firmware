#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

port="COM5"
baud="460800"
firmware_path="${RLCD_PROJECT_DIR}/build/rlcd_firmware_factory.bin"
confirmed=false

usage() {
    cat <<'EOF'
用法: ./scripts/flash.sh [--port COM5] [--baud 460800] [--firmware FILE] --confirm

前提：开发板已手动进入 BOOT 下载模式。脚本会核对 Windows VID:PID 303a:1001，
校验固件与工具，然后把完整安装/恢复固件写到 Flash 地址 0x0。
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
    echo "拒绝烧录：必须显式添加 --confirm。" >&2
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
    echo "找不到固件: ${firmware_path}" >&2
    echo "请先执行: ./scripts/build.sh" >&2
    exit 1
fi
for command_name in powershell.exe wslpath sha256sum; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "缺少命令: ${command_name}" >&2
        exit 1
    fi
done

firmware_dir="$(cd -- "$(dirname -- "${firmware_path}")" && pwd)"
firmware_name="$(basename -- "${firmware_path}")"
firmware_path="${firmware_dir}/${firmware_name}"
case "${firmware_name}" in
    rlcd_firmware.bin|rlcd_firmware_ota.bin|*-ota.bin)
        echo "拒绝把 OTA 应用镜像写入 0x0: ${firmware_name}" >&2
        echo "首次安装请使用 factory 镜像；已安装 v0.7.0+ 可使用 update-app.sh。" >&2
        exit 2
        ;;
    srmodels.bin|*-model.bin)
        echo "拒绝把离线语音模型写入 0x0: ${firmware_name}" >&2
        echo "首次安装请使用包含模型的 factory 镜像；旧设备补装模型请使用 update-app.sh。" >&2
        exit 2
        ;;
    rlcd_firmware_factory.bin|rlcd_firmware_merged.bin|*-factory.bin|esp32-rlcd-firmware-v0.[1-6].*.bin)
        ;;
    *)
        echo "拒绝把无法识别的文件写入 0x0: ${firmware_name}" >&2
        echo "请选择本项目发布的 factory 镜像。" >&2
        exit 2
        ;;
esac
verify_sha256_manifest_entry \
    "${firmware_dir}/SHA256SUMS" "${firmware_path}" \
    "${firmware_name}" "${firmware_name}"

"${RLCD_SCRIPT_DIR}/install-esptool-windows.sh"
esptool_exe="${RLCD_ESPTOOL_WINDOWS_DIR}/esptool.exe"
port_check_script="$(wslpath -w "${RLCD_SCRIPT_DIR}/check-esp32-port-windows.ps1")"
windows_firmware_path="$(wslpath -w "${firmware_path}")"
firmware_sha256="$(sha256sum "${firmware_dir}/${firmware_name}" | awk '{print $1}')"

powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "${port_check_script}" -Port "${port}"

echo "芯片连接测试：${port}"
"${esptool_exe}" --chip esp32s3 --port "${port}" --baud 115200 \
    --before no-reset --after no-reset chip-id

echo "即将烧录: ${windows_firmware_path}"
echo "SHA-256: ${firmware_sha256}"
"${esptool_exe}" --chip esp32s3 --port "${port}" --baud "${baud}" \
    --before no-reset --after no-reset write-flash \
    --flash-mode dio --flash-freq 80m --flash-size 16MB \
    0x0 "${windows_firmware_path}"

cat <<EOF
烧录及写后哈希验证完成。芯片仍停在下载模式，请：
  1. 长按 PWR 关机；
  2. 不要按 BOOT，短按 PWR 正常开机；
  3. 支持自动校时的版本按屏幕完成配网；需要手动校时时执行:
     ./scripts/set-rtc.sh --port ${port}
EOF
