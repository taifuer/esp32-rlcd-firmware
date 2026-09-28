#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

port="${1:-COM5}"
seconds="${2:-900}"
if [[ $# -gt 2 || ! "$port" =~ ^COM[0-9]+$ ||
      ! "$seconds" =~ ^[0-9]{1,3}$ || "$seconds" -lt 15 || "$seconds" -gt 900 ]]; then
    echo "用法: ./scripts/collect-chat-diagnostics.sh [COM5] [15—900 秒]" >&2
    exit 2
fi
for command_name in powershell.exe wslpath; do
    command -v "$command_name" >/dev/null || { echo "缺少命令: $command_name" >&2; exit 1; }
done
collector="$(wslpath -w "${RLCD_SCRIPT_DIR}/collect-chat-diagnostics-windows.ps1")"
mkdir -p "${RLCD_PROJECT_DIR}/build"
output_path="$(mktemp "${RLCD_PROJECT_DIR}/build/chat-diagnostics-XXXXXXXX.log")"
echo "仅采集受控诊断，日志: ${output_path}"
echo "打开原生 USB 串口可能复位；出现 MONITOR_OPEN 后，由你在设备上发起 AI Chat。"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
    -File "$collector" -SelfTest
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
    -File "$collector" -Port "$port" -Seconds "$seconds" | tee "$output_path"
