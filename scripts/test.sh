#!/usr/bin/env bash
set -euo pipefail

# shellcheck disable=SC1091
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

RLCD_TEST_TMP="$(mktemp -d -t esp32-rlcd-tests.XXXXXX)"
trap 'rm -rf -- "${RLCD_TEST_TMP}"' EXIT

cd "${RLCD_PROJECT_DIR}"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/calendar/include \
  src/calendar/chinese_lunar.c tests/test_chinese_lunar.c \
  -o "${RLCD_TEST_TMP}/test_chinese_lunar"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/power/include \
  src/power/battery_level.c tests/test_battery_level.c \
  -o "${RLCD_TEST_TMP}/test_battery_level"

"${RLCD_TEST_TMP}/test_chinese_lunar"
"${RLCD_TEST_TMP}/test_battery_level"
