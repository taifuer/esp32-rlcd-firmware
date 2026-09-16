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
