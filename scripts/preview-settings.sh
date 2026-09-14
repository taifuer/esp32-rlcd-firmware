#!/usr/bin/env bash
set -euo pipefail

# Emit a native-pixel SVG to stdout, using the same layout/fonts as firmware.
# No device, browser, network or new dependencies are needed.
# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
RLCD_PREVIEW_TMP="$(mktemp -d -t rlcd-settings-preview.XXXXXX)"
trap 'rm -rf -- "${RLCD_PREVIEW_TMP}"' EXIT
cd "${RLCD_PROJECT_DIR}"
cc -std=c17 -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I"${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/csrc" \
  -Isrc/settings/include -Isrc/display -Isrc/app \
  src/settings/settings_model.c src/settings/quick_settings.c \
  src/app/hold_interaction.c tests/test_settings_layout.c \
  "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/csrc/u8g2_font.c" \
  "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/csrc/u8g2_fonts.c" \
  "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/csrc/u8x8_8x8.c" \
  -o "${RLCD_PREVIEW_TMP}/settings-preview"
"${RLCD_PREVIEW_TMP}/settings-preview" --svg
