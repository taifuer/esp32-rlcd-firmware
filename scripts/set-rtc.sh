#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

port="COM5"
read_seconds="8"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            port="${2:?--port 缺少参数}"
            shift 2
            ;;
        --read-seconds)
            read_seconds="${2:?--read-seconds 缺少参数}"
            shift 2
            ;;
        --help|-h)
            echo "用法: ./scripts/set-rtc.sh [--port COM5] [--read-seconds 8]"
            exit 0
            ;;
        *)
            echo "未知参数: $1" >&2
            exit 2
            ;;
    esac
done

if [[ ! "${port}" =~ ^COM[0-9]+$ || ! "${read_seconds}" =~ ^[0-9]+$ ]]; then
    echo "参数无效。" >&2
    exit 2
fi
for command_name in powershell.exe wslpath; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "缺少命令: ${command_name}" >&2
        exit 1
    fi
done

port_check_script="$(wslpath -w "${RLCD_SCRIPT_DIR}/check-esp32-port-windows.ps1")"
rtc_script="$(wslpath -w "${RLCD_SCRIPT_DIR}/set-rtc-windows.ps1")"
powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "${port_check_script}" -Port "${port}"
powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "${rtc_script}" -Port "${port}" -ReadSeconds "${read_seconds}"
