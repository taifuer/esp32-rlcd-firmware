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
