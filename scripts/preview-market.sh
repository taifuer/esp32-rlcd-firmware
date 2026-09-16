#!/usr/bin/env bash
set -euo pipefail

# Emit an illustrative native-pixel SVG using the firmware's exact layout and
# fonts. No device, network, browser or new dependencies are required.
# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"
RLCD_MARKET_PREVIEW_TMP="$(mktemp -d -t rlcd-market-preview.XXXXXX)"
trap 'rm -rf -- "${RLCD_MARKET_PREVIEW_TMP}"' EXIT
cd "${RLCD_PROJECT_DIR}"
cc -std=c17 -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I"${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/csrc" \
  -Isrc/display/include -Isrc/display \
  src/display/market_display_model.c tests/test_market_layout.c \
  "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/csrc/u8g2_font.c" \
  "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/csrc/u8g2_fonts.c" \
  "${RLCD_WAVESHARE_COMPONENTS_DIR}/u8g2/csrc/u8x8_8x8.c" \
  -o "${RLCD_MARKET_PREVIEW_TMP}/market-preview"
"${RLCD_MARKET_PREVIEW_TMP}/market-preview" --svg
