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

if [[ ! -f "${RLCD_CJSON_DIR}/cJSON/cJSON.c" || \
      ! -f "${RLCD_CJSON_DIR}/cJSON/cJSON.h" ]]; then
  echo "未找到 cJSON 测试依赖（需要 cJSON.c 和 cJSON.h）: ${RLCD_CJSON_DIR}" >&2
  echo "请先执行 ./scripts/bootstrap.sh 准备锁定版本的依赖，或检查 RLCD_DEPS_DIR。" >&2
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
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Isrc/sensors/include \
  src/sensors/environment_comfort.c tests/test_environment_comfort.c -lm \
  -o "${RLCD_TEST_TMP}/test_environment_comfort"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Isrc/alarm/include \
  src/alarm/alarm_scheduler.c tests/test_alarm_scheduler.c \
  -o "${RLCD_TEST_TMP}/test_alarm_scheduler"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Isrc/alarm/include \
  src/alarm/alarm_input_gate.c tests/test_alarm_input_gate.c \
  -o "${RLCD_TEST_TMP}/test_alarm_input_gate"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Isrc/alarm/include \
  src/alarm/alarm_history_record.c tests/test_alarm_history_record.c \
  -o "${RLCD_TEST_TMP}/test_alarm_history_record"

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
  src/network/network_station_link.c tests/test_network_station_link.c \
  -o "${RLCD_TEST_TMP}/test_network_station_link"

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
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Isrc/app \
  src/app/image_delete_ui.c tests/test_image_delete_ui.c \
  -o "${RLCD_TEST_TMP}/test_image_delete_ui"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Isrc/app -Isrc/input/include \
  src/app/image_delete_ui.c src/input/button_state.c \
  tests/test_image_delete_interaction.c \
  -o "${RLCD_TEST_TMP}/test_image_delete_interaction"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Isrc/image/include \
  src/image/monochrome_image.c tests/test_monochrome_image.c \
  -o "${RLCD_TEST_TMP}/test_monochrome_image"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/image/include -Isrc/sd_image \
  src/sd_image/image_catalog.c tests/test_image_catalog.c \
  -o "${RLCD_TEST_TMP}/test_image_catalog"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/sd_image \
  src/sd_image/image_import_policy.c \
  tests/test_sd_image_import_policy.c \
  -o "${RLCD_TEST_TMP}/test_sd_image_import_policy"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/sd_image \
  src/sd_image/image_delete_policy.c \
  tests/test_sd_image_delete_policy.c \
  -o "${RLCD_TEST_TMP}/test_sd_image_delete_policy"

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
  -I"${RLCD_CJSON_DIR}/cJSON" \
  -c "${RLCD_CJSON_DIR}/cJSON/cJSON.c" \
  -o "${RLCD_TEST_TMP}/cJSON.o"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/update/include \
  -I"${RLCD_CJSON_DIR}/cJSON" \
  src/update/online_update_manifest.c \
  src/update/online_update_policy.c \
  tests/test_online_update_policy.c \
  "${RLCD_TEST_TMP}/cJSON.o" -lm \
  -o "${RLCD_TEST_TMP}/test_online_update_policy"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/gallery -Isrc/image/include \
  -I"${RLCD_CJSON_DIR}/cJSON" \
  src/gallery/gallery_manifest.c \
  tests/test_gallery_manifest.c \
  "${RLCD_TEST_TMP}/cJSON.o" -lm \
  -o "${RLCD_TEST_TMP}/test_gallery_manifest"

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

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Isrc/audio/include \
  src/audio/voice_session_state.c tests/test_voice_session_state.c \
  -o "${RLCD_TEST_TMP}/test_voice_session_state"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -Isrc/audio/include \
  src/audio/voice_command_policy.c tests/test_voice_command_policy.c \
  -o "${RLCD_TEST_TMP}/test_voice_command_policy"

"${RLCD_TEST_TMP}/test_chinese_lunar"
"${RLCD_TEST_TMP}/test_calendar_month"
"${RLCD_TEST_TMP}/test_battery_level"
"${RLCD_TEST_TMP}/test_environment_comfort"
"${RLCD_TEST_TMP}/test_alarm_scheduler"
"${RLCD_TEST_TMP}/test_alarm_input_gate"
"${RLCD_TEST_TMP}/test_alarm_history_record"
"${RLCD_TEST_TMP}/test_network_credentials"
"${RLCD_TEST_TMP}/test_network_retry_policy"
"${RLCD_TEST_TMP}/test_network_session_policy"
"${RLCD_TEST_TMP}/test_network_station_link"
"${RLCD_TEST_TMP}/test_network_qr"
"${RLCD_TEST_TMP}/test_button_state"
"${RLCD_TEST_TMP}/test_page_state"
"${RLCD_TEST_TMP}/test_image_delete_ui"
"${RLCD_TEST_TMP}/test_image_delete_interaction"
"${RLCD_TEST_TMP}/test_monochrome_image"
"${RLCD_TEST_TMP}/test_image_catalog"
"${RLCD_TEST_TMP}/test_sd_image_import_policy"
"${RLCD_TEST_TMP}/test_sd_image_delete_policy"
"${RLCD_TEST_TMP}/test_app_settings"
"${RLCD_TEST_TMP}/test_settings_record"
"${RLCD_TEST_TMP}/test_network_screen_policy"
"${RLCD_TEST_TMP}/test_firmware_update_policy"
"${RLCD_TEST_TMP}/test_settings_portal_policy"
"${RLCD_TEST_TMP}/test_online_update_policy"
"${RLCD_TEST_TMP}/test_gallery_manifest"
"${RLCD_TEST_TMP}/test_rtc_backup_policy"
"${RLCD_TEST_TMP}/test_clock_service_policy"
"${RLCD_TEST_TMP}/test_audio_level"
"${RLCD_TEST_TMP}/test_audio_session_state"
"${RLCD_TEST_TMP}/test_voice_session_state"
"${RLCD_TEST_TMP}/test_voice_command_policy"
python3 tests/test_rlcd_image_tool.py
