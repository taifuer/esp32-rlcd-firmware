import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

// Run the exact firmware HTML/JS. No browser package, network or real keys.
const source = fs.readFileSync(new URL('../src/update/firmware_update.c', import.meta.url), 'utf8');
const config = fs.readFileSync(new URL('../src/conversation/include/conversation_config_model.h', import.meta.url), 'utf8');
const definitions = [...(source + config).replace(/\\\n/g, '').matchAll(/^#define\s+(\w+)\s+("(?:[^"\\]|\\.)*")/gm)];
let block = source.split('static const char SETTINGS_PAGE[] =')[1].split('static const char UPDATE_SUCCESS_PAGE[] =')[0];
block = block.replace(/("(?:[^"\\]|\\.)*")|(\b[A-Z][A-Z0-9_]+\b)/g, (token, literal, macro) => {
  if (literal) return literal;
  const definition = definitions.find(match => match[1] === macro);
  assert.ok(definition, 'unresolved C macro ' + macro);
  return definition[2];
});
const html = [...block.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(match => JSON.parse(match[0])).join('');
const javascript = html.match(/<script>([\s\S]*)<\/script>/)[1];
new vm.Script(javascript);
const ids = [...html.matchAll(/id="([^"]+)"/g)].map(match => match[1]);
assert.equal(new Set(ids).size, ids.length, 'HTML IDs are unique');
assert.match(html, /<footer>© .*target="_blank" rel="noopener noreferrer">ESP32 固件/);
assert.match(html, /<details><summary class="section-toggle">天气/);
assert.match(html, /<details><summary class="section-toggle">AI 对话 Beta/);
assert.equal((html.match(/<details(?:\s[^>]*)?>/g) || []).length, (html.match(/<\/details>/g) || []).length);
assert.doesNotMatch(javascript, /localStorage|sessionStorage|navigator\.clipboard|execCommand/);

class Element {
  value = ''; checked = false; disabled = false; hidden = false;
  dataset = {}; children = []; selectedIndex = 0; textContent = '';
  files = []; open = false;
  appendChild(child) { this.children.push(child); }
  removeChild(child) { this.children.splice(this.children.indexOf(child), 1); }
  get firstChild() { return this.children[0] || null; }
  setAttribute() {}
  focus() {}
  querySelectorAll() { return settingsInputs; }
}
const elements = Object.fromEntries(ids.map(id => [id, new Element()]));
const days = [1, 2, 4, 8, 16, 32, 64].map(bit => Object.assign(new Element(), {dataset: {bit: String(bit)}}));
const settingsNames = {timezone: 'timezone', unit: 'unit', volume: 'volume', alarm: 'alarm',
  alarmHour: 'alarm_hour', alarmMinute: 'alarm_minute', alarmDays: 'alarm_days', updates: 'updates'};
const settingsInputs = [...Object.keys(settingsNames).map(id => elements[id]), elements.alarmTime, ...days];
elements.wifiForm.hidden = true;
let state = {
  token: 'test-token', timezone: 480, unit: 'c', volume: 50, updates: 'stable',
  alarm: 'off', alarm_hour: 7, alarm_minute: 30, alarm_days: 62,
  wifi_configured: true, wifi_ssid: 'Test Wi-Fi', sd_state: 'ready', image_count: 0,
  weather: {available: true, enabled: false, configured: false, key_saved: false,
    api_host: '', province_id: 0, city_id: 0},
  conversation: {available: true, service: 'aliyun_realtime', configured: false, enabled: false,
    model: 'qwen3-omni-flash-realtime', api_host: 'dashscope.aliyuncs.com', shared_endpoint: true},
};
let failPost = false, lastBody = '', pendingPost;
let lastPath = '', confirmed = true, lastConfirmation = '', request;
let music = {ready: true, scanned: true, truncated: false, state: 'stopped', elapsed: 0,
  selected_index: 0, tracks: [{name: '01 - 音乐 & 测试.mp3', bytes: 1234567}, {name: '02.wav', bytes: 5678}]};
class FakeRequest {
  constructor() { request = this; this.headers = {}; this.upload = {}; }
  open(method, path) { this.method = method; this.path = path; }
  setRequestHeader(key, value) { this.headers[key] = value; }
  send(body) { this.body = body; }
}
const context = vm.createContext({
  document: {
    getElementById(id) { assert.ok(elements[id], 'missing element ' + id); return elements[id]; },
    querySelectorAll(selector) { assert.equal(selector, '.alarm-day'); return days; },
    createElement() { return new Element(); },
  },
  fetch: async (path, options = {}) => {
    if (options.method === 'POST') {
      lastPath = path;
      lastBody = options.body;
      if (pendingPost) await pendingPost;
      return {ok: !failPost, text: async () => failPost ? '保存失败' : '已保存'};
    }
    if (path === '/api/state') return {ok: true, json: async () => structuredClone(state)};
    if (path === '/api/images') return {ok: true, json: async () => ({images: [], selected: ''})};
    if (path === '/api/music') return {ok: true, json: async () => structuredClone(music)};
    if (path.startsWith('/api/weather/regions')) return {ok: true, json: async () => ({items: [{id: 1, name: '测试'}]})};
    assert.fail('unexpected request ' + path);
  },
  FormData: class extends Map {
    constructor(form) {
      super();
      assert.equal(form, elements.settings);
      for (const [id, name] of Object.entries(settingsNames)) {
        if (!elements[id].disabled) this.set(name, String(elements[id].value));
      }
    }
  },
  URLSearchParams, TextEncoder, confirm: message => { lastConfirmation = message; return confirmed; },
  setInterval() {}, XMLHttpRequest: FakeRequest,
});
vm.runInContext(javascript, context);
await new Promise(resolve => setImmediate(resolve));
assert.equal(elements.volume.value, 50);
assert.equal(elements.portalYear.textContent, String(new Date().getFullYear()));
elements.conversationKey.value = 'unsaved-test-key';
elements.conversationApiHost.value = 'draft.example';
elements.weatherKey.value = 'weather-test-key';
elements.weatherApiHost.value = 'draft.qweatherapi.com';
elements.wifiForm.hidden = false;
elements.wifiSsid.value = 'Unsaved network';
state.volume = 70;
await vm.runInContext("load('settings')", context);
assert.equal(elements.volume.value, 70);
assert.equal(elements.conversationKey.value, 'unsaved-test-key');
assert.equal(elements.weatherKey.value, 'weather-test-key');
assert.equal(elements.wifiSsid.value, 'Unsaved network');
elements.volume.value = 90;
await vm.runInContext("load('images')", context);
assert.equal(elements.volume.value, 90);
assert.equal(elements.conversationKey.value, 'unsaved-test-key');
assert.equal(elements.weatherKey.value, 'weather-test-key');
await vm.runInContext("load('conversation')", context);
assert.equal(elements.conversationKey.value, '');
assert.equal(elements.volume.value, 90);
assert.equal(elements.weatherKey.value, 'weather-test-key');
await vm.runInContext("load('weather')", context);
assert.equal(elements.weatherKey.value, '');
assert.equal(elements.volume.value, 90);
let finishPost;
pendingPost = new Promise(resolve => { finishPost = resolve; });
const submission = elements.settings.onsubmit({preventDefault() {}, target: elements.settings});
assert.equal(elements.volume.disabled, true);
assert.match(lastBody, /volume=90/); // serialize BEFORE disabling the current form
elements.conversationKey.value = 'edited-while-other-form-saves';
finishPost();
await submission;
assert.equal(elements.volume.disabled, false);
assert.equal(elements.conversationKey.value, 'edited-while-other-form-saves');
pendingPost = null;
failPost = true;
elements.volume.value = 42;
await elements.settings.onsubmit({preventDefault() {}, target: elements.settings});
assert.equal(elements.volume.value, 42, 'failed save keeps the submitted draft');
assert.equal(elements.volume.disabled, false);
assert.equal(elements.settingsMessage.textContent, '保存失败');
failPost = false;
await vm.runInContext("load('music')", context);
assert.equal(elements.volume.value, 42, 'music refresh preserves settings draft');
assert.equal(elements.conversationKey.value, 'edited-while-other-form-saves');
assert.match(elements.musicTracks.children[0].textContent, /1\.23 MB/);
assert.equal(elements.musicPlay.disabled, false);
await elements.musicPlay.onclick();
assert.equal(lastPath, '/api/music/play');
assert.equal(lastBody, 'name=' + encodeURIComponent(music.tracks[0].name));
confirmed = false;
await elements.musicDelete.onclick();
assert.equal(lastPath, '/api/music/play', 'cancelled deletion makes no POST');
assert.match(lastConfirmation, /01 - 音乐 & 测试\.mp3/);
confirmed = true;
await elements.musicDelete.onclick();
assert.equal(lastPath, '/api/music/delete');
assert.match(lastBody, /&confirm=DELETE$/);
elements.musicFile.files = [{name: '新歌 + a.mp3', size: 9000}];
elements.musicFile.onchange();
assert.equal(elements.musicUpload.disabled, false);
elements.musicUpload.onclick();
assert.equal(request.path, '/api/music/upload?name=' + encodeURIComponent('新歌 + a.mp3'));
assert.equal(request.headers['X-RLCD-Token'], 'test-token');
assert.equal(elements.musicPlay.disabled, true);
request.upload.onprogress({lengthComputable: true, loaded: 9000, total: 9000});
assert.match(elements.musicMessage.textContent, /校验/);
request.onerror();
assert.equal(elements.musicPlay.disabled, false, 'connection failure releases controls');
assert.equal(elements.musicFile.files.length, 1, 'failed upload keeps chosen file');
elements.musicUpload.onclick();
request.status = 200; request.responseText = '歌曲已写入';
await request.onload();
assert.equal(elements.musicMessage.textContent, '歌曲已写入');
assert.equal(elements.conversationKey.value, 'edited-while-other-form-saves');
request = null;
elements.musicFile.files = [{name: 'large.mp3', size: 32000001}];
elements.musicUpload.onclick();
assert.equal(request, null, 'oversized upload never sent');
elements.musicFile.files = [{name: '01 - 音乐 & 测试.MP3', size: 9000}];
elements.musicUpload.onclick();
assert.equal(request, null, 'case-insensitive duplicate not sent');
music.truncated = true;
await vm.runInContext('loadMusic()', context);
assert.equal(elements.musicUpload.disabled, true);
assert.equal(elements.musicDelete.disabled, false, 'full library still deletable');
music = {...music, ready: false, tracks: []};
await vm.runInContext('loadMusic()', context);
assert.equal(elements.musicFile.disabled, true);
assert.equal(elements.musicPlay.disabled, true);
assert.match(elements.musicState.textContent, /未检测到/);
assert.doesNotMatch(source.split('static const char RECOVERY_SETTINGS_PAGE[] =')[1].split('static const char SETTINGS_PAGE[] =')[0], /api\/music|musicTracks/);
console.log('Settings portal: exact JS, scoped drafts, music CRUD, cancellation, upload errors, no card and recovery isolation passed.');
