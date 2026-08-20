#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

RLCD_TEST_TMP="$(mktemp -d -t esp32-rlcd-tests.XXXXXX)"
trap 'rm -rf -- "${RLCD_TEST_TMP}"' EXIT

cd "${RLCD_PROJECT_DIR}"

if [[ ! -f "${RLCD_QRCODE_COMPONENT_DIR}/qrcodegen.c" ]]; then
  echo "未找到二维码测试依赖，请先执行: ./scripts/bootstrap.sh" >&2
  exit 1
fi

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/calendar/include \
  src/calendar/chinese_lunar.c tests/test_chinese_lunar.c \
  -o "${RLCD_TEST_TMP}/test_chinese_lunar"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/calendar/include \
  src/calendar/calendar_month.c tests/test_calendar_month.c \
  -o "${RLCD_TEST_TMP}/test_calendar_month"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/power/include \
  src/power/battery_level.c tests/test_battery_level.c \
  -o "${RLCD_TEST_TMP}/test_battery_level"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/network/include \
  src/network/network_credentials.c tests/test_network_credentials.c \
  -o "${RLCD_TEST_TMP}/test_network_credentials"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/network/include \
  -I"${RLCD_QRCODE_COMPONENT_DIR}" \
  src/network/network_credentials.c \
  "${RLCD_QRCODE_COMPONENT_DIR}/qrcodegen.c" \
  tests/test_network_qr.c \
  -o "${RLCD_TEST_TMP}/test_network_qr"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/input/include \
  src/input/button_state.c tests/test_button_state.c \
  -o "${RLCD_TEST_TMP}/test_button_state"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/app \
  src/app/page_state.c tests/test_page_state.c \
  -o "${RLCD_TEST_TMP}/test_page_state"

"${RLCD_TEST_TMP}/test_chinese_lunar"
"${RLCD_TEST_TMP}/test_calendar_month"
"${RLCD_TEST_TMP}/test_battery_level"
"${RLCD_TEST_TMP}/test_network_credentials"
"${RLCD_TEST_TMP}/test_network_qr"
"${RLCD_TEST_TMP}/test_button_state"
"${RLCD_TEST_TMP}/test_page_state"
