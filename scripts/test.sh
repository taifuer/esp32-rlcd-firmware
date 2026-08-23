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

RLCD_CJSON_DIR="${RLCD_IDF_DIR}/components/json/cJSON"
if [[ ! -f "${RLCD_CJSON_DIR}/cJSON.c" || \
      ! -f "${RLCD_CJSON_DIR}/cJSON.h" ]]; then
  echo "未找到 cJSON 测试依赖（需要 cJSON.c 和 cJSON.h）: ${RLCD_CJSON_DIR}" >&2
  echo "请先执行 ./scripts/bootstrap.sh 准备锁定版本的 ESP-IDF，或检查 RLCD_DEPS_DIR。" >&2
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
  src/network/network_retry_policy.c tests/test_network_retry_policy.c \
  -o "${RLCD_TEST_TMP}/test_network_retry_policy"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/network \
  src/network/network_session_policy.c tests/test_network_session_policy.c \
  -o "${RLCD_TEST_TMP}/test_network_session_policy"

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

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/settings/include \
  src/settings/settings_model.c src/settings/settings_power_policy.c \
  tests/test_app_settings.c \
  -o "${RLCD_TEST_TMP}/test_app_settings"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/settings -Isrc/settings/include \
  src/settings/settings_model.c src/settings/settings_record.c \
  tests/test_settings_record.c \
  -o "${RLCD_TEST_TMP}/test_settings_record"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/app \
  src/app/network_screen_policy.c tests/test_network_screen_policy.c \
  -o "${RLCD_TEST_TMP}/test_network_screen_policy"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/update/include \
  src/update/firmware_update_policy.c tests/test_firmware_update_policy.c \
  -o "${RLCD_TEST_TMP}/test_firmware_update_policy"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/update/include \
  src/update/settings_portal_policy.c tests/test_settings_portal_policy.c \
  -o "${RLCD_TEST_TMP}/test_settings_portal_policy"

cc -std=c17 -w \
  -I"${RLCD_CJSON_DIR}" \
  -c "${RLCD_CJSON_DIR}/cJSON.c" \
  -o "${RLCD_TEST_TMP}/cJSON.o"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/update/include \
  -I"${RLCD_CJSON_DIR}" \
  src/update/online_update_manifest.c \
  src/update/online_update_policy.c \
  tests/test_online_update_policy.c \
  "${RLCD_TEST_TMP}/cJSON.o" -lm \
  -o "${RLCD_TEST_TMP}/test_online_update_policy"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/rtc/include \
  src/rtc/rtc_backup_policy.c tests/test_rtc_backup_policy.c \
  -o "${RLCD_TEST_TMP}/test_rtc_backup_policy"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -D_POSIX_C_SOURCE=200809L \
  -Isrc/rtc/include \
  src/rtc/clock_service_policy.c tests/test_clock_service_policy.c \
  -o "${RLCD_TEST_TMP}/test_clock_service_policy"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/audio/include \
  src/audio/audio_level.c tests/test_audio_level.c \
  -o "${RLCD_TEST_TMP}/test_audio_level"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/audio/include \
  src/audio/audio_session_state.c tests/test_audio_session_state.c \
  -o "${RLCD_TEST_TMP}/test_audio_session_state"

"${RLCD_TEST_TMP}/test_chinese_lunar"
"${RLCD_TEST_TMP}/test_calendar_month"
"${RLCD_TEST_TMP}/test_battery_level"
"${RLCD_TEST_TMP}/test_network_credentials"
"${RLCD_TEST_TMP}/test_network_retry_policy"
"${RLCD_TEST_TMP}/test_network_session_policy"
"${RLCD_TEST_TMP}/test_network_qr"
"${RLCD_TEST_TMP}/test_button_state"
"${RLCD_TEST_TMP}/test_page_state"
"${RLCD_TEST_TMP}/test_app_settings"
"${RLCD_TEST_TMP}/test_settings_record"
"${RLCD_TEST_TMP}/test_network_screen_policy"
"${RLCD_TEST_TMP}/test_firmware_update_policy"
"${RLCD_TEST_TMP}/test_settings_portal_policy"
"${RLCD_TEST_TMP}/test_online_update_policy"
"${RLCD_TEST_TMP}/test_rtc_backup_policy"
"${RLCD_TEST_TMP}/test_clock_service_policy"
"${RLCD_TEST_TMP}/test_audio_level"
"${RLCD_TEST_TMP}/test_audio_session_state"
