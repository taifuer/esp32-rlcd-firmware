#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

port="COM5"
baud="460800"
firmware_path="${RLCD_PROJECT_DIR}/build/rlcd_firmware_ota.bin"
confirmed=false

usage() {
    cat <<'EOF'
用法: ./scripts/update-app.sh [--port COM5] [--baud 460800] [--firmware FILE] --confirm

前提：开发板已手动进入 BOOT 下载模式。脚本只更新 ota_0 应用槽并重置 OTA 选择数据，
不会覆盖位于 0x9000 的 NVS，因此保留 Wi-Fi 配置。仅适用于已经安装 v0.7.0 或更新版本
分区表的设备；首次迁移或恢复必须使用 scripts/flash.sh。
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
        ;;
    *)
        echo "拒绝把非 OTA 文件作为应用镜像写入: ${firmware_name}" >&2
        exit 2
        ;;
esac
if [[ ! -f "${firmware_dir}/SHA256SUMS" ]]; then
    echo "固件目录缺少 SHA256SUMS，拒绝更新: ${firmware_dir}" >&2
    exit 1
fi
(cd "${firmware_dir}" && sha256sum --check SHA256SUMS)

"${RLCD_SCRIPT_DIR}/install-esptool-windows.sh"
esptool_exe="${RLCD_ESPTOOL_WINDOWS_DIR}/esptool.exe"
port_check_script="$(wslpath -w "${RLCD_SCRIPT_DIR}/check-esp32-port-windows.ps1")"
windows_firmware_path="$(wslpath -w "${firmware_path}")"
firmware_sha256="$(sha256sum "${firmware_path}" | awk '{print $1}')"

powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "${port_check_script}" -Port "${port}"

echo "芯片连接测试：${port}"
"${esptool_exe}" --chip esp32s3 --port "${port}" --baud 115200 \
    --before no-reset --after no-reset chip-id

echo "即将串行更新 OTA 应用: ${windows_firmware_path}"
echo "SHA-256: ${firmware_sha256}"
"${esptool_exe}" --chip esp32s3 --port "${port}" --baud "${baud}" \
    --before no-reset --after no-reset write-flash \
    --flash-mode dio --flash-freq 80m --flash-size 16MB \
    0x10000 "${windows_firmware_path}"

# ota_0 has been verified by esptool at this point. Clearing only otadata makes
# the ESP-IDF bootloader select ota_0 without touching the NVS credentials.
"${esptool_exe}" --chip esp32s3 --port "${port}" --baud "${baud}" \
    --before no-reset --after no-reset erase-region 0xd000 0x2000

cat <<EOF
应用镜像写入及校验完成，NVS/Wi-Fi 配置保持不变。芯片仍停在下载模式，请：
  1. 长按 PWR 关机；
  2. 不要按 BOOT，短按 PWR 正常开机。
EOF
