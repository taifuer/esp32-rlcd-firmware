#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

if [[ ! -f "${RLCD_IDF_DIR}/export.sh" ]]; then
  echo "未找到 ESP-IDF v${ESP_IDF_VERSION}: ${RLCD_IDF_DIR}" >&2
  echo "请先执行: ./scripts/bootstrap.sh" >&2
  exit 1
fi

export IDF_TOOLS_PATH="${RLCD_IDF_TOOLS_DIR}"
# shellcheck disable=SC1091
source "${RLCD_IDF_DIR}/export.sh"

cd "${RLCD_PROJECT_DIR}"
idf.py -B build menuconfig
