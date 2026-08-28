#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

port="COM5"
session_timeout_seconds="180"
output_path=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            port="${2:?--port 缺少参数}"
            shift 2
            ;;
        --session-timeout-seconds)
            session_timeout_seconds="${2:?--session-timeout-seconds 缺少参数}"
            shift 2
            ;;
        --output)
            output_path="${2:?--output 缺少参数}"
            shift 2
            ;;
        --help|-h)
            echo "用法: ./scripts/collect-voice-diagnostics.sh [--port COM5] [--session-timeout-seconds 180] [--output FILE]"
            exit 0
            ;;
        *)
            echo "未知参数: $1" >&2
            exit 2
            ;;
    esac
done

if [[ ! "${port}" =~ ^COM[0-9]+$ ||
      ! "${session_timeout_seconds}" =~ ^[0-9]+$ ||
      "${session_timeout_seconds}" -lt 30 ||
      "${session_timeout_seconds}" -gt 600 ]]; then
    echo "参数无效：单次会话等待应为 30—600 秒。" >&2
    exit 2
fi
for command_name in powershell.exe wslpath; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "缺少命令: ${command_name}" >&2
        exit 1
    fi
done

if [[ -z "${output_path}" ]]; then
    output_path="${RLCD_PROJECT_DIR}/build/voice-diagnostics-$(date +%Y%m%d-%H%M%S).log"
elif [[ "${output_path}" != /* ]]; then
    output_path="${RLCD_PROJECT_DIR}/${output_path}"
fi
mkdir -p -- "$(dirname -- "${output_path}")"

port_check_script="$(wslpath -w "${RLCD_SCRIPT_DIR}/check-esp32-port-windows.ps1")"
collector_script="$(wslpath -w "${RLCD_SCRIPT_DIR}/collect-voice-diagnostics-windows.ps1")"
windows_output="$(wslpath -w "${output_path}")"

powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "${port_check_script}" -Port "${port}"
powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "${collector_script}" \
    -Port "${port}" \
    -SessionTimeoutSeconds "${session_timeout_seconds}" \
    -OutputFile "${windows_output}"

echo "语音可靠性日志已保存: ${output_path}"
