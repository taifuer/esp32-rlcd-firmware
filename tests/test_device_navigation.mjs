import fs from 'node:fs';
import assert from 'node:assert/strict';

// Guard the application wiring as well as the pure page/button model tests.
const main = fs.readFileSync(new URL('../src/app/main.c', import.meta.url), 'utf8');
const display = fs.readFileSync(new URL('../src/display/display.c', import.meta.url), 'utf8');
assert.match(main, /app_page_is_system\(active_page\)\s*\|\|\s*active_page == APP_PAGE_VOICE/,
  'daily Chat still enters its detail renderer');
assert.match(main, /active_page == APP_PAGE_VOICE[\s\S]*display_show_voice/);
const entry = main.split('key_action == APP_PAGE_ACTION_OPEN_SETTINGS &&')[1]
  .split('previous_update_state =')[0];
assert.match(entry, /const esp_err_t start_error = firmware_update_start\(\);/,
  'settings long press opens the portal directly');
assert.match(entry, /dual_button_release_gate = true;/,
  'the opening press cannot switch the new LAN portal into hotspot mode');
assert.doesNotMatch(entry, /quick_settings_open/);
assert.equal((main.match(/quick_settings_open_volume\(/g) || []).length, 1,
  'only contextual music volume remains');
assert.match(main, /input_page == APP_PAGE_MUSIC[\s\S]*quick_settings_open_volume/);
assert.match(display, /draw_system_header\("CHAT", 0U\)/);
assert.ok(/SYSTEM_PAGE_COUNT = 3,/.test(display), 'three system pages');
const settingsView = display.split('void display_show_settings(')[1]
  .split('void display_show_quick_settings(')[0];
const settingsLayout = fs.readFileSync(new URL('../src/display/settings_display_layout.h', import.meta.url), 'utf8');
assert.match(settingsLayout, /HOLD KEY 2s TO OPEN/);
assert.match(settingsView, /settings_display_lines\(/);
assert.match(settingsView, /draw_centered\(lines\[i\]\.baseline_y, lines\[i\]\.text\)/);
assert.doesNotMatch(settingsView, /draw_system_row\(\d+, "POWER"/);
assert.doesNotMatch(settingsView, /TIME ZONE|TEMP UNIT|VOLUME|ALARM|QUICK SETTINGS/);
// Every manual review must refresh the manifest; background checks stay passive.
const updateAction = main.split('esp_err_t action_error = ESP_ERR_INVALID_STATE;')[1]
  .split('key_action == APP_PAGE_ACTION_OPEN_SETTINGS')[0];
assert.match(updateAction, /else if \(!online_update_busy\)[\s\S]*online_firmware_update_request_confirmation\(\)/);
assert.doesNotMatch(updateAction, /online_firmware_update_request_check\(/);
assert.ok(/online_update_status\.state ==\s+ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION[\s\S]{0,350}dual_button_release_gate = true/.test(main),
  'asynchronous confirmation requires button release before installing');
assert.match(display, /status->target_changed\s*\? "Target changed; confirm again"/);
console.log('Device navigation: daily Chat rendering, direct portal entry and minimal settings passed.');

const marketAction = main.split('key_action == APP_PAGE_ACTION_REFRESH_MARKET &&')[1]
  .split('key_action == APP_PAGE_ACTION_START_VOICE')[0];
assert.match(marketAction, /market_service_request_refresh\(\)/);
assert.doesNotMatch(marketAction, /app_page_state_(?:go_home|open_page)|display_show_status|esp_restart/,
  'market refresh never navigates or opens a blocking status screen');
assert.match(main, /market_visible = active_page == APP_PAGE_MARKET/);
assert.match(main, /market_service_set_activity\(market_visible,[\s\S]{0,100}power_policy.automatic_network/);
assert.match(main, /display_mode == APP_DISPLAY_MARKET[\s\S]{0,550}display_show_market/);
assert.match(main, /static market_service_status_t market_status;/,
  'market snapshots do not grow the main task stack');
assert.match(main, /market_error = recovery_mode \? ESP_ERR_NOT_SUPPORTED/,
  'recovery boot does not start market networking');
console.log('Market navigation: fixed page, nonblocking refresh, power/visibility gates and recovery isolation passed.');

const pageState = fs.readFileSync(new URL('../src/app/page_state.c', import.meta.url), 'utf8');
assert.match(pageState, /state->current = app_page_state_next_daily_page\(state\);/);
assert.match(main, /const char \*next_daily_page = app_page_daily_label\(\s*app_page_state_next_daily_page\(&page_state\)\)/);
assert.match(main, /display_weather.next_daily_page = next_daily_page/);
assert.match(main, /market_view.next_daily_page = next_daily_page/);
assert.match(main, /display_show_calendar\(&dashboard, next_daily_page\)/);
assert.match(main, /display_show_monochrome_image\([\s\S]{0,160}image_count, next_daily_page\)/);
assert.match(main, /\.next_daily_page = next_daily_page/);
assert.doesNotMatch(display, /BOOT: (?:CALENDAR|CHAT) \| KEY: (?:PLAY|PAUSE)/);
assert.match(main, /app_page_state_set_daily_pages_mask\(&page_state, settings.daily_pages_mask\)/);
const refresh = main.split('snapshot.generation != settings_generation) {')[1]
  .split('const bool configured_power_changed')[0];
assert.match(refresh, /app_page_state_set_daily_pages_mask\(\s*&page_state, snapshot.settings.daily_pages_mask\)/,
  'visibility hot-applies from latest saved settings before any power transaction can defer it');
const voiceHandoff = main.split('static bool accept_voice_engine_result(')[1]
  .split('case VOICE_COMMAND_DECISION_UNAVAILABLE:')[0];
assert.match(voiceHandoff, /app_page_state_page_available\(\s*pages, voice_command_page\(decision.action\)\)/,
  'hidden voice targets are unavailable before showing a matched result');
assert.match(main, /app_page_state_open_page\(&page_state,\s*target_page\)/,
  'late voice completion rechecks availability');
assert.doesNotMatch(main, /page_state\.current\s*=/,
  'modal return and asynchronous callbacks cannot bypass visibility by restoring a raw page');
assert.doesNotMatch(main, /"No changes made"/,
  'closing the portal must not imply successfully saved settings were discarded');
assert.match(refresh, /audio_music_set_playback_mode\(requested_music_mode\)/,
  'music mode hot-applies before a power/network transaction may defer other settings');
assert.match(main, /music_sleep_minutes != previous_music_sleep_minutes/,
  'paused music countdown redraws by minute, without relying on playback progress');
assert.match(main, /\.sleep_remaining_seconds = music_status.sleep_remaining_seconds/);
console.log('Daily page visibility: shared BOOT/footer destination, live preference application and guarded voice navigation passed.');

assert.match(main, /app_page_state_set_daily_pages_order\(&page_state, settings.daily_pages_order\)/);
const chatView = display.split('void display_show_voice(')[1].split('void display_show_quick_settings(')[0];
assert.match(chatView, /display_interaction_format_daily_navigation\(\s*navigation, sizeof\(navigation\), status->next_daily_page\)/);
assert.doesNotMatch(chatView, /display_interaction_chat_footer\(/, 'Chat may no longer be last after reorder');
assert.match(refresh, /memcmp\(page_state.daily_pages_order,[\s\S]*app_page_state_set_daily_pages_order\(\s*&page_state, snapshot.settings.daily_pages_order\)/);
const retryArm = main.split('if (cloud_session_selected && voice_session_state_arm_retry(')[1].split('if (key_pressed_or_debounced')[0];
assert.match(retryArm, /!cloud_voice_status.running/);
assert.match(retryArm, /!key_pressed_or_debounced && !boot_pressed_or_debounced/);
assert.match(retryArm, /key_event = BUTTON_EVENT_NONE;[\s\S]*boot_event = BUTTON_EVENT_NONE;/,
  'release begun during PLEASE WAIT cannot execute RETRY in the arming iteration');
const retryAction = main.split('if (cloud_session_selected && voice_session.failure_retained) {')[1].split('} else if (boot_event')[0];
assert.match(retryAction, /voice_session.retry_ready &&[\s\S]*key_event == BUTTON_EVENT_SHORT_PRESS/);
assert.match(retryAction, /APP_POWER_STATE_NORMAL[\s\S]*cloud_audio_ready\(&cloud_voice_status\)/);
assert.match(retryAction, /!cloud_voice_status.running[\s\S]*audio_conversation_dismiss\(\) == ESP_OK[\s\S]*voice_session_state_retry[\s\S]*audio_conversation_start\(generation\)/);
assert.doesNotMatch(retryAction, /audio_voice_start|audio_conversation_continue|audio_conversation_release_key/,
  'retry starts new cloud intent, never replays old audio or bypasses capture-release gate');
console.log('Page ordering and cloud retry: persisted order, explicit input, cleanup and release gating passed.');

assert.match(main, /if \(!recovery_mode\) \{\s*const esp_err_t history_error = environment_history_init\(\)/);
assert.match(main, /environment_view = heap_caps_calloc\(1, sizeof\(\*environment_view\)/,
  'history snapshot belongs in PSRAM, never on the main task stack');
assert.match(main, /const esp_err_t error = shtc3_read\(&measurement\);\s*environment_history_record\(/,
  'only real sensor attempts feed history, not filtered cached home values');
assert.match(main, /dashboard.time_valid \? \(int64_t\)time\(NULL\) : 0/);
assert.match(main, /error == ESP_OK, measurement.temperature_c, measurement.humidity_percent/);
assert.match(main, /active_page == APP_PAGE_ENVIRONMENT/);
assert.match(main, /last_environment_render_ms >= 60000U/,
  'history rendering stays bounded to one minute between explicit changes');
assert.match(main, /if \(environment_history_storage_busy\(\)\) \{\s*pending_music_toggle/,
  'music intent waits for a background SD lease without blocking the UI');
assert.match(main, /environment_history_set_io_allowed\(!history_foreground_busy/);
assert.match(main, /history_foreground_busy = recovery_mode \|\| alarm_modal_active/);
assert.match(main, /history_idle_page == APP_PAGE_HOME[\s\S]{0,130}APP_PAGE_ENVIRONMENT/);
assert.match(display, /environment_display_render\(s_u8g2, history, fahrenheit, next_daily_page\)/);
console.log('Environment integration: real readings, recovery isolation, bounded rendering and SD priority passed.');

const timerAdvance = main.split('const uint64_t timing_now_ms =')[1]
  .split('const uint32_t button_elapsed_ms')[0];
assert.match(timerAdvance, /esp_timer_get_time\(\) \/ 1000U/);
assert.match(timerAdvance, /timing_update\(&timing, timing_now_ms\)/);
assert.doesNotMatch(timerAdvance, /APP_PAGE_TIMER|dashboard.time_valid|network_status/,
  'both timers advance independently of visible page, RTC and network');
assert.match(timerAdvance, /timing_stopwatch_selected\s*\?/,
  'only the selected timing mode contributes display changes');
assert.match(refresh, /timing_preference_minutes = snapshot.settings.timer_minutes/);
assert.doesNotMatch(refresh, /timing_countdown_set_duration/,
  'saving a custom choice must not change the selection or active run');
assert.match(main, /timing_init\(&timing, TIMING_COUNTDOWN_DEFAULT_MINUTES\)/,
  'boot starts at the five-minute choice, even with a legacy custom preference');
assert.match(main, /timing_key_short\(&timing, timing_stopwatch_selected,/);
assert.match(main, /timing_key_hold\(&timing, timing_stopwatch_selected,/);
assert.match(main, /display_show_timing\(&timing_status, next_daily_page\)/);
const timerModal = main.split('const bool timing_notice_blocked =')[1]
  .split('if (alarm_started_this_loop && quick_settings.active)')[0];
for (const gate of ['alarm_modal_active', 'voice_session_active', 'firmware_update_ui_active',
  'gallery_download_ui_active', 'online_update_busy', 'online_update_confirmation_active',
  'quick_settings.active', 'manual_sync_ui', 'power_setting_ui', 'image_delete_ui']) {
  assert(timerModal.includes(gate), `timer notice must respect ${gate}`);
}
assert.match(timerModal, /if \(settings.alarm_volume > 0U\)\s*\{\s*const esp_err_t sound_error = audio_alert_preview/,
  'silent notices must not acquire the audio worker or stop music');
assert.match(timerModal, /audio_alert_cancel_preview\(\)/);
assert.doesNotMatch(timerModal, /audio_alert_start\(|audio_alert_stop\(/,
  'timer cleanup cannot cancel a real RTC alarm');
assert.match(main, /if \(alarm_button_events_suppressed \|\| timing_notice_result.consume_input\)/);
console.log('Timing integration: background monotonic clock, hot preference, modal priority, muted sound and input gates passed.');

// HOME is independent of the shared release gate, which also protects OTA,
// modal exits and long-press actions. Only a fresh, completed chord may navigate.
assert.match(main, /button_chord_init\(&home_shortcut\)/);
assert.ok(main.indexOf('const bool home_shortcut_input_admitted =') <
  main.indexOf('if (key_pressed_or_debounced &&\n            boot_pressed_or_debounced)'),
  'capture external release gating before this sample can set the dual-key gate');
assert.match(main, /!dual_button_release_gate \|\| button_chord_is_tracking\(&home_shortcut\)/);
const homePermission = main.split('const bool home_shortcut_allowed =')[1]
  .split('const bool home_shortcut_triggered =')[0];
for (const gate of ['buttons_ready', '!recovery_mode', 'home_shortcut_input_admitted',
  'display_allows_home_shortcut(previous_display_mode)', '!alarm_modal_active',
  '!alarm_button_events_suppressed', '!timing_notice.visible', '!timing_notice_result.consume_input',
  '!voice_session_active', '!voice_button_release_gate', '!firmware_update_ui_active',
  '!gallery_download_ui_active', '!online_update_busy', '!online_update_confirmation_active',
  '!quick_settings.active', 'manual_sync_ui == MANUAL_SYNC_UI_NONE',
  'power_setting_ui == POWER_SETTING_UI_NONE', '!app_image_delete_ui_is_active(&image_delete_ui)',
  '!image_delete_release_gate_was_active', '!image_delete_wait_for_button_release']) {
  assert(homePermission.includes(gate), `home chord must respect ${gate}`);
}
assert.ok(main.indexOf('const bool home_shortcut_allowed =') >
  main.indexOf('const timing_notice_result_t timing_notice_result ='),
  'alarm and countdown priority are settled before a chord can return home');
const homeAction = main.split('if (home_shortcut_triggered) {')[1]
  .split('if (alarm_button_events_suppressed')[0];
assert.match(homeAction, /app_page_state_go_home\(&page_state\)/);
assert.match(homeAction, /key_event = BUTTON_EVENT_NONE;\s*boot_event = BUTTON_EVENT_NONE;/,
  'a completed chord cannot also select an image, pause music/timer or switch page');
assert.doesNotMatch(homeAction, /audio_|timing_|firmware_update_|app_settings_|network_time_|esp_restart/,
  'HOME changes navigation only, not ongoing music, timing or persistent settings');
const homeDisplays = main.split('static bool display_allows_home_shortcut(')[1]
  .split('} app_display_mode_t;')[0].split('typedef enum')[0];
for (const mode of ['DASHBOARD', 'WEATHER', 'MARKET', 'CALENDAR', 'MONOCHROME_IMAGE',
  'MUSIC', 'TIMING', 'ENVIRONMENT', 'STATUS', 'VOICE', 'SETTINGS', 'ONLINE_UPDATE']) {
  assert(homeDisplays.includes(`case APP_DISPLAY_${mode}:`));
}
assert.doesNotMatch(homeDisplays, /case APP_DISPLAY_(?:NONE|ALARM|TIMING_DONE|HOLD_PROMPT|QUICK_SETTINGS|NETWORK_SETUP|IMAGE_DELETE_|FIRMWARE_UPDATE_)/);
assert.match(homeDisplays, /default:\s*return false;/);
console.log('Home shortcut integration: fresh dual-key input, modal priority, release consumption and navigation-only action passed.');
