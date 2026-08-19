#!/usr/bin/env bash

# Shared path and version discovery for repository scripts. This file is meant
# to be sourced, not executed directly.

RLCD_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RLCD_PROJECT_DIR="$(cd -- "${RLCD_SCRIPT_DIR}/.." && pwd)"

# shellcheck disable=SC1091
source "${RLCD_PROJECT_DIR}/tool-versions.env"

if [[ -n "${RLCD_DEPS_DIR:-}" ]]; then
    RLCD_EXTERNAL_DEPS_DIR="${RLCD_DEPS_DIR}"
elif [[ -d "${RLCD_PROJECT_DIR}/../../third_party" ]]; then
    # Layout used by the current WSL workspace.
    RLCD_EXTERNAL_DEPS_DIR="$(cd -- "${RLCD_PROJECT_DIR}/../../third_party" && pwd)"
else
    # A fresh standalone clone keeps downloaded dependencies beside, not in,
    # the Git repository.
    RLCD_EXTERNAL_DEPS_DIR="${RLCD_PROJECT_DIR}/../esp32-rlcd-firmware-deps"
fi

RLCD_IDF_DIR="${RLCD_EXTERNAL_DEPS_DIR}/toolchains/esp-idf-v${ESP_IDF_VERSION}"
RLCD_IDF_TOOLS_DIR="${RLCD_EXTERNAL_DEPS_DIR}/toolchains/espressif-v${ESP_IDF_VERSION}"
RLCD_WAVESHARE_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/waveshareteam/ESP32-S3-RLCD-4.2"
RLCD_WAVESHARE_COMPONENTS_DIR="${RLCD_WAVESHARE_DIR}/${WAVESHARE_COMPONENTS_RELATIVE_PATH}"
RLCD_IDF_EXTRA_COMPONENTS_DIR="${RLCD_EXTERNAL_DEPS_DIR}/sources/espressif/idf-extra-components"
RLCD_QRCODE_COMPONENT_DIR="${RLCD_IDF_EXTRA_COMPONENTS_DIR}/${QRCODE_COMPONENT_RELATIVE_PATH}"
RLCD_ESPTOOL_WINDOWS_DIR="${RLCD_EXTERNAL_DEPS_DIR}/toolchains/esptool-windows-v${ESPTOOL_WINDOWS_VERSION}"

export RLCD_PROJECT_DIR
export RLCD_EXTERNAL_DEPS_DIR
export RLCD_WAVESHARE_COMPONENTS_DIR
export RLCD_QRCODE_COMPONENT_DIR
export IDF_COMPONENT_MANAGER=0
