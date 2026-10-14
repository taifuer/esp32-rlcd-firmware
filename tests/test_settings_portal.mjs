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
// Validate the actual C-embedded markup, not just the mock form map below.
const markup = html.replace(/<script>[\s\S]*?<\/script>|<style>[\s\S]*?<\/style>/g, '');
const stack = [], voidTags = new Set(['meta', 'input', 'hr', 'br', 'img', 'link']);
for (const [, closing, tag] of markup.matchAll(/<(\/?)([a-z][a-z0-9]*)\b[^>]*>/g)) {
  if (voidTags.has(tag)) continue;
  if (closing) assert.equal(stack.pop(), tag, 'balanced portal HTML: ' + tag);
  else stack.push(tag);
}
assert.deepEqual(stack, []);
for (const [id, expected] of Object.entries({
  settings: ['section', 'timezone', 'unit', 'volume'],
  timerForm: ['section', 'timer_minutes'],
  environmentForm: ['section'],
  alarmForm: ['section', 'alarm_volume', 'alarm', 'alarm_hour', 'alarm_minute', 'alarm_days'],
  updatesForm: ['section', 'updates'],
  pagesForm: ['section', 'daily_pages_mask', 'daily_pages_order'],
  musicSettingsForm: ['section', 'music_mode'],
  weatherForm: ['enabled', 'api_host', 'province', 'city'],
  weatherKeyForm: ['api_key'],
  conversationPreferencesForm: ['enabled', 'voice', 'reply_style', 'model', 'api_host'],
  conversationForm: ['api_key'],
})) {
  const form = html.split('<form id="' + id + '"')[1].split('</form>')[0];
  assert.deepEqual([...form.matchAll(/\bname="([^"]+)"/g)].map(match => match[1]), expected);
}
assert.match(html, /<footer>© .*target="_blank" rel="noopener noreferrer">ESP32 固件/);
assert.match(html, /<details><summary class="section-toggle">天气/);
assert.match(html, /<details><summary class="section-toggle">AI 对话 Beta/);
assert.match(html, /<details id="conversationCredentials" class="advanced">/);
assert.doesNotMatch(html, /<details id="conversationCredentials"[^>]*\sopen(?:\s|>)/);
assert.match(html, /<details id="weatherCredentials" class="advanced">/);
assert.doesNotMatch(html, /<details id="weatherCredentials"[^>]*\sopen(?:\s|>)/);
assert.doesNotMatch(html, /qwen3-omni-flash-realtime/);
assert.match(html, /Qwen Audio 3\.0 Realtime Flash（默认）/);
assert.match(html, /保存配置不会自动连接或发起收费测试/);
assert.match(html, /<details><summary class="section-toggle">市场看板/);
assert.match(html, /<details><summary class="section-toggle">显示页面/);
assert.match(html, /<details><summary class="section-toggle">计时设置/);
assert.match(html, /<label for="timerMinutes">自定义时长（分钟）<\/label>/);
assert.match(html, /id="timerMinutes" name="timer_minutes" type="number" min="1" max="180" step="1" value="5" required/);
const timerMarkup = html.split('<form id="timerForm">')[1].split('</form>')[0];
assert.match(timerMarkup, /自定义 1—180 分钟，默认 5 分钟/);
assert.match(timerMarkup, /与设备 1\/5\/10\/30 分钟不重复时增加为第 5 档/);
assert.match(timerMarkup, /保存不改当前选中时长或正在进行的计时，也不自动开始/);
assert.match(timerMarkup, /设备未开始时短按 KEY 选时长，按住 KEY 2 秒开始/);
assert.doesNotMatch(timerMarkup, /下次重置|新计时|默认 25|按 KEY 开始/);
assert.match(html, /隐藏只跳过页面，不停用功能，也不删除配置或文件/);
assert.match(html, /保存后可继续修改其他设置/);
assert.doesNotMatch(html, /保存并获取天气|随后将关闭热点并获取天气/);
assert.match(html, /报价时间为北京时间（UTC\+8）/);
assert.equal((html.match(/<details(?:\s[^>]*)?>/g) || []).length, (html.match(/<\/details>/g) || []).length);
assert.doesNotMatch(javascript, /localStorage|sessionStorage|navigator\.clipboard|execCommand/);
const serverDefinition = source.split('static esp_err_t start_web_server(void)')[1].split('static void stop_web_server')[0];
assert.doesNotMatch(serverDefinition, /\.uri\s*=\s*"\/api\/timer/, 'no remote countdown control API');
assert.ok(source.includes('"\\"timer_minutes\\":%u') || source.includes('\\"timer_minutes\\":%u'), 'state includes the saved duration');
assert.match(source, /settings\.timer_minutes/);
const routeCount = [...serverDefinition.matchAll(/\.uri\s*=/g)].length;
assert.ok(routeCount <= Number(serverDefinition.match(/max_uri_handlers\s*=\s*(\d+)/)[1]), 'all portal routes fit the server handler limit');
for (const route of ['/api/conversation/key', '/api/conversation/preferences', '/api/conversation/clear',
  '/api/weather/key', '/api/weather/preferences', '/api/weather/clear']) {
  assert.ok(serverDefinition.includes('.uri = "' + route + '"'), 'registered route ' + route);
}
for (const uri of ['weather_preferences_uri', 'weather_api_key_uri', 'weather_clear_uri',
  'conversation_preferences_uri', 'conversation_api_key_uri', 'conversation_clear_uri']) {
  assert.match(serverDefinition, new RegExp('if \\(error == ESP_OK && !recovery_mode\\) \\{\\s*' +
    'error = httpd_register_uri_handler\\(s_http_server,\\s*&' + uri + '\\);'), uri + ' excluded from recovery');
}

class Element {
  value = ''; checked = false; disabled = false; hidden = false;
  dataset = {}; children = []; selectedIndex = 0; textContent = '';
  files = []; open = false;
  appendChild(child) { if (this.children.includes(child)) this.removeChild(child); this.children.push(child); }
  removeChild(child) { this.children.splice(this.children.indexOf(child), 1); }
  get firstChild() { return this.children[0] || null; }
  attributes = {}; controls = [];
  setAttribute(name, value) { this.attributes[name] = value; }
  pointerCaptures = new Set();
  getBoundingClientRect() { return {left: 0, top: 0, width: 200, height: 150}; }
  setPointerCapture(id) { this.pointerCaptures.add(id); }
  hasPointerCapture(id) { return this.pointerCaptures.has(id); }
  releasePointerCapture(id) { this.pointerCaptures.delete(id); }
  focus() {}
  click() {}
  getContext() { return null; }
  querySelectorAll() { return this.controls; }
}
const elements = Object.fromEntries(ids.map(id => [id, new Element()]));
const days = [1, 2, 4, 8, 16, 32, 64].map(bit => Object.assign(new Element(), {dataset: {bit: String(bit)}}));
const forms = {
  settings: {section: 'general', names: {timezone: 'timezone', unit: 'unit', volume: 'volume'}},
  timerForm: {section: 'timer', names: {timerMinutes: 'timer_minutes'}},
  environmentForm: {section: 'environment', names: {environmentSaveEnabled: 'environment_history_save_enabled'}},
  alarmForm: {section: 'alarm', names: {alarmVolume: 'alarm_volume', alarm: 'alarm', alarmHour: 'alarm_hour', alarmMinute: 'alarm_minute', alarmDays: 'alarm_days'}},
  updatesForm: {section: 'updates', names: {updates: 'updates'}},
  pagesForm: {section: 'pages', names: {dailyPagesMask: 'daily_pages_mask', dailyPagesOrder: 'daily_pages_order'}},
  musicSettingsForm: {section: 'music', names: {musicMode: 'music_mode'}},
  weatherForm: {names: {weatherEnabled: 'enabled', weatherApiHost: 'api_host', weatherProvince: 'province', weatherCity: 'city'}},
  weatherKeyForm: {names: {weatherKey: 'api_key'}},
  conversationPreferencesForm: {names: {conversationEnabled: 'enabled', conversationVoice: 'voice', conversationReplyStyle: 'reply_style', conversationModel: 'model', conversationApiHost: 'api_host'}},
  conversationForm: {names: {conversationKey: 'api_key'}},
};
for (const [id, form] of Object.entries(forms)) elements[id].controls = Object.keys(form.names).map(name => elements[name]);
elements.alarmForm.controls.push(elements.alarmTime, elements.alarmPreview, ...days);
elements.timerForm.controls.push(elements.timerSave);
const pageIds = ['pageWeather', 'pageMarket', 'pageCalendar', 'pageImage', 'pageMusic', 'pageChat', 'pageTimer', 'pageEnvironment'];
elements.pagesForm.controls.push(...pageIds.flatMap(id => [elements[id], elements[id+'Up'], elements[id+'Down']]), elements.pagesSave);
elements.panelNetwork.hidden = elements.panelMedia.hidden = elements.panelMaintenance.hidden = true;
elements.musicSection.open = true;
elements.musicSleep.value = '15';
elements.wifiForm.hidden = true;
let state = {
  token: 'test-token', timezone: 480, unit: 'c', volume: 50, alarm_volume: 68, timer_minutes: 5, updates: 'stable',
  daily_pages_mask: 255, daily_pages_order: [1,2,4,64,8,16,32,128], music_mode: 'sequential', environment_history_save_enabled: false, environment_archive_save_enabled: false, environment_card_available: true,
  alarm: 'off', alarm_hour: 7, alarm_minute: 30, alarm_days: 62,
  wifi_configured: true, wifi_ssid: 'Test Wi-Fi', sd_state: 'ready', image_count: 0,
  weather: {available: true, enabled: false, configured: false, key_saved: false,
    api_host: '', province_id: 0, city_id: 0},
  conversation: {available: true, service: 'aliyun_realtime', configured: false, enabled: false,
    model: 'qwen-audio-3.0-realtime-flash', api_host: 'dashscope.aliyuncs.com', shared_endpoint: true,
    voice: 'longanqian', reply_style: 'concise',
    migration: 'none', last_result: 'not_tested', last_error: 'none', last_hint: '', completed_turns: 0},
};
let market = {enabled: false, ids: [0, 3, 7, 9, 11],
  presets: ['上证指数', '深证成指', '创业板指', '沪深300', '中证500', '上证50', '科创50', '恒生指数',
    '恒生科技', '纳斯达克综合', '纳斯达克100', '标普500', '道琼斯', '日经225', '台湾加权'].map((name, id) => ({id, name}))};
let failPost = false, lastBody = '', pendingPost;
let disconnectedPost = false;
let unauthorized = false;
const failedGets = new Map(), writes = [], timers = new Map(), frames = new Map(), windowEvents = new Map();
let nextFrame = 0;
let nextTimer = 0, hangRead = false, hangPost = false, hangBody = false;
const abortWait = signal => new Promise((resolve, reject) => {
  signal.addEventListener('abort', () => reject(Object.assign(new Error('aborted'), {name: 'AbortError'})), {once: true});
});
const getRequests = [];
const sessionToken = '00123456789abcdeffedcba987654321';
let lastPath = '', confirmed = true, lastConfirmation = '', request;
let music = {ready: true, scanned: true, truncated: false, state: 'stopped', elapsed: 0, playback_mode: 'sequential', sleep_remaining_seconds: 0,
  selected_index: 0, tracks: [{name: '01 - 音乐 & 测试.mp3', bytes: 1234567}, {name: '02.wav', bytes: 5678}]};
const backupData = {format:'esp32-rlcd-preferences',version:1,preferences:{
  utc_offset_minutes:480,temperature_unit:'c',audio_playback_volume:40,alarm_volume:60,
  alarm_enabled:false,alarm_hour:8,alarm_minute:15,alarm_weekdays:62,
  daily_pages_mask:255,daily_pages_order:[1,128,2,4,64,8,16,32],music_mode:'repeat_all',timer_minutes:10,
}};
let environment = {recording_age:3600,restored:false,card_available:true,storage_enabled:false,
  storage_error:false,saved_this_boot:false,points:[
    {age:900,valid:true,restored:false,t:2300,h:4500,failures:0},
    {age:600,valid:false,restored:false,t:0,h:0,failures:1},
    {age:300,valid:true,restored:false,t:2500,h:4700,failures:0},
  ]};
let archivePage = {files:[{month:'2026-10',bytes:1024},{month:'2026-09',bytes:512}],next_cursor:'2026-09'};
let archiveText = 'unix_seconds,temperature\n1790000000,24.5\n';
let archiveType = 'text/csv; charset=utf-8', archiveBytes, downloads = 0;
class FakeRequest {
  constructor() { request = this; this.headers = {}; this.upload = {}; }
  open(method, path) { this.method = method; this.path = path; }
  setRequestHeader(key, value) { this.headers[key] = value; }
  send(body) { this.body = body; }
}
const context = vm.createContext({
  window: {addEventListener(name, listener) { windowEvents.set(name, listener); }},
  document: {
    getElementById(id) { assert.ok(elements[id], 'missing element ' + id); return elements[id]; },
    querySelectorAll(selector) { assert.equal(selector, '.alarm-day'); return days; },
    createElement() { return new Element(); },
  },
  fetch: async (path, options = {}) => {
    if (options.method === 'POST') {
      writes.push({path, body: options.body});
      lastPath = path;
      lastBody = options.body;
      if (hangPost) { hangPost = false; return abortWait(options.signal); }
      if (path === '/api/pair') {
        assert.equal(options.headers['X-RLCD-Pair'], '1');
        unauthorized = false;
        return {ok: true, text: async () => sessionToken};
      }
      if (unauthorized) return {ok: false, status: 401, text: async () => '需要授权'};
      if (pendingPost) await pendingPost;
      if (disconnectedPost) throw new TypeError('network disconnected');
      if (path === '/api/settings' && !failPost) {
        const fields = new URLSearchParams(options.body);
        if (fields.get('section') === 'pages') {
          state.daily_pages_mask = Number(fields.get('daily_pages_mask'));
          state.daily_pages_order = fields.get('daily_pages_order').split(',').map(Number);
        }
        if (fields.get('section') === 'music') state.music_mode = fields.get('music_mode');
        if (fields.get('section') === 'timer') state.timer_minutes = Number(fields.get('timer_minutes'));
        if (fields.get('section') === 'environment') {
          if (fields.get('environment_archive_save_enabled') === 'on') assert.equal(options.headers['X-RLCD-Confirm'],'ENABLE_ARCHIVE');
          else if (fields.get('environment_history_save_enabled') === 'on') assert.equal(options.headers['X-RLCD-Confirm'],'ENABLE_HISTORY');
          state.environment_history_save_enabled = fields.get('environment_history_save_enabled') === 'on';
          state.environment_archive_save_enabled = fields.get('environment_archive_save_enabled') === 'on';
        }
      }
      if (path === '/api/settings/defaults' && !failPost) { state.daily_pages_mask = 255; state.daily_pages_order = [1,128,2,4,64,8,16,32]; state.music_mode = 'sequential'; state.timer_minutes = 5; state.environment_history_save_enabled = false; state.environment_archive_save_enabled = false; }
      if (path === '/api/settings/restore' && !failPost) {
        assert.equal(options.headers['X-RLCD-Confirm'], 'RESTORE_PREFERENCES');
        const p = JSON.parse(options.body).preferences;
        Object.assign(state,{timezone:p.utc_offset_minutes,unit:p.temperature_unit,volume:p.audio_playback_volume,
          alarm_volume:p.alarm_volume,alarm:p.alarm_enabled?'on':'off',alarm_hour:p.alarm_hour,
          alarm_minute:p.alarm_minute,alarm_days:p.alarm_weekdays,daily_pages_mask:p.daily_pages_mask,
          daily_pages_order:[...p.daily_pages_order],music_mode:p.music_mode,timer_minutes:p.timer_minutes});
      }
      if (path === '/api/conversation/key' && !failPost) {
        const fields = new URLSearchParams(options.body);
        state.conversation = {...state.conversation,
          configured: state.conversation.configured || fields.get('api_key').length > 0};
      }
      if (path === '/api/conversation/preferences' && !failPost && state.conversation.configured) {
        const fields = new URLSearchParams(options.body);
        const host = fields.get('api_host');
        state.conversation = {...state.conversation, enabled: fields.get('enabled') === 'on',
          voice: fields.get('voice'), reply_style: fields.get('reply_style'),
          model: fields.get('model'), api_host: host || 'dashscope.aliyuncs.com',
          shared_endpoint: !host || ['dashscope.aliyuncs.com', 'dashscope-intl.aliyuncs.com'].includes(host)};
      }
      if (path === '/api/conversation/clear' && !failPost) {
        state.conversation = {...state.conversation, configured: false, enabled: false,
          model: 'qwen-audio-3.0-realtime-flash', api_host: 'dashscope.aliyuncs.com', shared_endpoint: true,
          voice: 'longanqian', reply_style: 'concise'};
      }
      if (path === '/api/music/sleep' && !failPost) music.sleep_remaining_seconds = Number(new URLSearchParams(options.body).get('minutes')) * 60;
      if (path === '/api/weather/preferences' && !failPost) {
        const fields = new URLSearchParams(options.body);
        state.weather = {...state.weather, enabled: fields.get('enabled') === 'on',
          configured: state.weather.key_saved && !!fields.get('api_host') && Number(fields.get('province')) > 0 && Number(fields.get('city')) > 0,
          api_host: fields.get('api_host'), province_id: Number(fields.get('province')), city_id: Number(fields.get('city'))};
      }
      if (path === '/api/weather/key' && !failPost) {
        const fields = new URLSearchParams(options.body);
        state.weather = {...state.weather, key_saved: state.weather.key_saved || fields.get('api_key').length > 0};
        state.weather.configured = state.weather.key_saved && !!state.weather.api_host && state.weather.province_id > 0 && state.weather.city_id > 0;
      }
      if (path === '/api/weather/clear' && !failPost) {
        state.weather = {...state.weather, enabled: false, configured: false, key_saved: false,
          api_host: '', province_id: 0, city_id: 0};
      }
      if (path === '/api/market/config' && !failPost) {
        const fields = new URLSearchParams(options.body);
        market.enabled = fields.get('enabled') === 'on';
        market.ids = fields.get('ids').split(',').map(Number);
      }
      return {ok: !failPost, text: async () => failPost ? '保存失败' : '已保存'};
    }
    getRequests.push({path, options});
    if (hangRead) { hangRead = false; return abortWait(options.signal); }
    if (hangBody) { hangBody = false; return {ok: true, json: () => abortWait(options.signal)}; }
    if (failedGets.has(path)) return {ok: false, status: 503, text: async () => failedGets.get(path)};
    if (unauthorized) return {ok: false, status: 401, text: async () => '需要授权'};
    if (path === '/api/state') return {ok: true, json: async () => structuredClone(state)};
    if (path === '/api/market/config') return {ok: true, json: async () => structuredClone(market)};
    if (path === '/api/images') return {ok: true, json: async () => ({images: [], selected: ''})};
    if (path === '/api/music') return {ok: true, json: async () => structuredClone(music)};
    if (path === '/api/settings/backup') return {ok:true,text:async()=>JSON.stringify(backupData)};
    if (path === '/api/environment') return {ok:true,json:async()=>structuredClone(environment)};
    if (path === '/api/environment.csv') return {ok:true,text:async()=> 'unix_seconds,age_seconds,valid\n,600,0\n'};
    if (path.startsWith('/api/environment/archives')) return {ok:true,json:async()=>structuredClone(archivePage)};
    if (path.startsWith('/api/environment/archive.csv')) return {ok:true,
      headers:{get:name=>name==='Content-Type'?archiveType:name==='X-RLCD-CSV-Bytes'?String(archiveBytes??Buffer.byteLength(archiveText)):null},
      text:async()=>archiveText};
    if (path.startsWith('/api/weather/regions')) return {ok: true, json: async () => ({items: [{id: 1, name: '测试'}]})};
    assert.fail('unexpected request ' + path);
  },
  FormData: class extends Map {
    constructor(form) {
      super();
      const definition = Object.entries(forms).find(([id]) => form === elements[id])?.[1];
      assert.ok(definition, 'known independent form');
      if (definition.section) this.set('section', definition.section);
      for (const [name, value] of Object.entries(definition.fixed || {})) this.set(name, value);
      for (const [id, name] of Object.entries(definition.names)) {
        if (!elements[id].disabled) this.set(name, String(elements[id].value));
      }
    }
  },
  URLSearchParams, TextEncoder, confirm: message => { lastConfirmation = message; return confirmed; },
  setInterval() {},
  setTimeout(callback, ms) { const id = ++nextTimer; timers.set(id, {callback, ms}); return id; },
  clearTimeout(id) { timers.delete(id); },
  AbortController, XMLHttpRequest: FakeRequest,
  Blob,
  URL: {createObjectURL() { downloads += 1; return 'blob:download'; }, revokeObjectURL() {}},
  requestAnimationFrame(callback) { const id = ++nextFrame; frames.set(id, callback); return id; },
  cancelAnimationFrame(id) { frames.delete(id); },
});
vm.runInContext(javascript, context);
const imagePagehide = windowEvents.get('pagehide');
await new Promise(resolve => setImmediate(resolve));
// Reading/rendering AI state never tests a paid endpoint or starts a session.
const aiWritesBefore = writes.length;
assert.equal(elements.conversationActiveModel.textContent, '当前模型：qwen-audio-3.0-realtime-flash');
assert.equal(elements.conversationLastResult.textContent, '最近对话：尚未测试');
assert.equal(elements.conversationMigration.hidden, true);
const renderConversation = overrides => vm.runInContext(
  'setConversationState(' + JSON.stringify({...state.conversation, ...overrides}) + ')', context);
renderConversation({configured: true, enabled: true, migration: 'completed'});
assert.equal(elements.conversationStatus.textContent, '已开启');
assert.equal(elements.conversationLastResult.textContent, '最近对话：尚未测试', 'enabled is not proof of a successful conversation');
assert.equal(elements.conversationMigration.hidden, false);
assert.match(elements.conversationMigration.textContent, /API Key、Host 和开关保持不变/);
renderConversation({migration: 'pending'});
assert.match(elements.conversationMigration.textContent, /尚未保存/);
renderConversation({last_result: 'success', last_hint: 'stale error'});
assert.equal(elements.conversationLastResult.textContent, '最近对话：成功');
renderConversation({last_result: 'failed', last_hint: '身份验证失败，请检查 API Key。'});
assert.equal(elements.conversationLastResult.textContent, '最近对话：失败 · 身份验证失败，请检查 API Key。');
assert.equal(elements.conversationDiagnostics.hidden, true, 'older/absent diagnostics stay hidden');
const connectionDiag = {websocket_connected: true, session_created: true,
  session_update_sent: true, session_ready: false, close_code: 1008, transport_end: 2};
renderConversation({last_result: 'failed', diagnostics: connectionDiag});
assert.equal(elements.conversationDiagnostics.hidden, false);
assert.equal(elements.conversationDiagnostics.open, false, 'details stay collapsed initially');
assert.match(elements.conversationDiagnosticText.textContent, /会话确认：未收到/);
assert.match(elements.conversationDiagnosticText.textContent, /服务端关闭/);
assert.match(elements.conversationDiagnosticText.textContent, /关闭码：1008/);
renderConversation({last_result: 'failed', diagnostics: {...connectionDiag, close_code: 0, transport_end: 3}});
assert.match(elements.conversationDiagnosticText.textContent, /会话结束/);
assert.match(elements.conversationDiagnosticText.textContent, /关闭码：未提供/);
for (const malformed of [null, {}, {...connectionDiag, close_code: '<svg onload=bad>'},
  {...connectionDiag, close_code: -1}, {...connectionDiag, close_code: 65536},
  {...connectionDiag, transport_end: 99}, {...connectionDiag, session_ready: 'true'}]) {
  renderConversation({last_result: 'failed', diagnostics: malformed});
  assert.equal(elements.conversationDiagnostics.hidden, true);
  assert.equal(elements.conversationDiagnosticText.textContent, '');
}
for (const result of ['success', 'cancelled', 'not_tested']) {
  renderConversation({last_result: result, diagnostics: connectionDiag});
  assert.equal(elements.conversationDiagnostics.hidden, true);
  assert.equal(elements.conversationDiagnosticText.textContent, '');
}
renderConversation({last_result: 'cancelled', last_hint: 'stale error'});
assert.equal(elements.conversationLastResult.textContent, '最近对话：已取消');
renderConversation({last_result: '__proto__', migration: '<script>bad</script>'});
assert.equal(elements.conversationLastResult.textContent, '最近对话：尚未测试');
assert.equal(elements.conversationMigration.hidden, true);
renderConversation({available: false, last_result: 'success'});
assert.equal(elements.conversationLastResult.textContent, '最近对话：暂不可用');
assert.equal(elements.conversationSave.disabled, true);
assert.equal(elements.conversationPreferencesSave.disabled, true);
renderConversation({});
assert.equal(writes.length, aiWritesBefore, 'state display must not generate a mutation or cloud test');
assert.equal(elements.volume.value, 50);
assert.equal(elements.timerMinutes.value, 5);
assert.ok(pageIds.every(id => elements[id].checked), 'migration/default all pages are offered');
assert.equal(elements.marketChoices.children.length, 15);
assert.equal(elements.marketCount.textContent, '5 / 5');
assert.equal(elements.marketEnabled.value, 'off');
assert.equal(elements.marketOrder.children.length, 5);
const marketInput = id => elements.marketChoices.children[id].children[0];
const selectedMarket = () => JSON.parse(vm.runInContext('JSON.stringify(marketIds)', context));
assert.equal(marketInput(1).disabled, true, 'unselected indices disabled at five');
marketInput(1).checked = true;
marketInput(1).onchange();
assert.deepEqual(selectedMarket(), [0, 3, 7, 9, 11], 'sixth selection rejected even if handler invoked');
assert.equal(marketInput(1).checked, false);
marketInput(7).checked = false;
marketInput(7).onchange();
assert.equal(marketInput(1).disabled, false);
marketInput(14).checked = true;
marketInput(14).onchange();
assert.deepEqual(selectedMarket(), [0, 3, 9, 11, 14]);
elements.marketOrder.children[1].children[1].onclick();
assert.deepEqual(selectedMarket(), [3, 0, 9, 11, 14], 'move-up changes display order');
assert.equal(elements.marketOrder.children[0].children[1].disabled, true);
assert.equal(elements.marketOrder.children[4].children[2].disabled, true);
assert.equal(elements.alarmVolume.value, 68);
elements.alarmVolume.value = 25;
await elements.alarmPreview.onclick();
assert.equal(lastPath, '/api/alarm/preview');
assert.equal(lastBody, 'volume=25');
assert.equal(state.alarm_volume, 68, 'preview does not persist');
assert.equal(elements.portalYear.textContent, String(new Date().getFullYear()));
elements.conversationKey.value = 'unsaved-test-key';
elements.conversationApiHost.value = 'draft.example';
elements.weatherKey.value = 'weather-test-key';
elements.weatherApiHost.value = 'draft.qweatherapi.com';
elements.wifiForm.hidden = false;
elements.wifiSsid.value = 'Unsaved network';
state.volume = 70;
await vm.runInContext("load('settings')", context);
assert.deepEqual(selectedMarket(), [3, 0, 9, 11, 14], 'general refresh preserves market draft');
assert.equal(elements.volume.value, 70);
assert.equal(elements.alarmVolume.value, 25, 'general refresh preserves alarm draft');
assert.equal(elements.conversationKey.value, 'unsaved-test-key');
assert.equal(elements.weatherKey.value, 'weather-test-key');
assert.equal(elements.wifiSsid.value, 'Unsaved network');
elements.volume.value = 90;
elements.updates.value = 'beta';
for (const name of ['Network', 'Media', 'Maintenance', 'General']) {
  elements['nav' + name].onclick();
  for (const section of ['General', 'Network', 'Media', 'Maintenance']) {
    assert.equal(elements['panel' + section].hidden, section !== name);
    assert.equal(elements['nav' + section].attributes['aria-pressed'], section === name ? 'true' : 'false');
  }
  assert.equal(elements.volume.value, 90);
  assert.equal(elements.alarmVolume.value, 25);
  assert.equal(elements.updates.value, 'beta');
  assert.equal(elements.conversationKey.value, 'unsaved-test-key');
  assert.deepEqual(selectedMarket(), [3, 0, 9, 11, 14], 'section navigation preserves index order');
}
elements.marketEnabled.value = 'on';
const marketGets = getRequests.filter(request => request.path === '/api/market/config').length;
await elements.marketForm.onsubmit({preventDefault() {}});
assert.equal(lastPath, '/api/market/config');
assert.equal(lastBody, 'enabled=on&ids=3%2C0%2C9%2C11%2C14');
assert.deepEqual(market.ids, [3, 0, 9, 11, 14]);
assert.equal(elements.marketStatus.textContent, '已开启 · 5 项');
assert.equal(getRequests.filter(request => request.path === '/api/market/config').length, marketGets, 'save does not reload other forms');
assert.equal(elements.conversationKey.value, 'unsaved-test-key');
assert.equal(elements.weatherKey.value, 'weather-test-key');
assert.equal(elements.wifiSsid.value, 'Unsaved network');
assert.equal(elements.volume.value, 90);
elements.marketOrder.children[0].children[2].onclick();
failPost = true;
await elements.marketForm.onsubmit({preventDefault() {}});
assert.deepEqual(selectedMarket(), [0, 3, 9, 11, 14], 'failed save retains order draft');
assert.deepEqual(market.ids, [3, 0, 9, 11, 14], 'failed save preserves stored order');
assert.equal(elements.marketMessage.textContent, '保存失败');
assert.equal(elements.marketSave.disabled, false);
failPost = false;
await vm.runInContext("load('market')", context);
assert.deepEqual(selectedMarket(), market.ids, 'explicit market reload reads saved order');
assert.equal(elements.volume.value, 90);
assert.equal(elements.weatherKey.value, 'weather-test-key');
for (const id of selectedMarket()) {
  marketInput(id).checked = false;
  marketInput(id).onchange();
}
assert.equal(elements.marketSave.disabled, true, 'empty selection is not saved');
lastPath = 'empty-selection';
await elements.marketForm.onsubmit({preventDefault() {}});
assert.equal(lastPath, 'empty-selection');
assert.match(elements.marketMessage.textContent, /1—5/);
await vm.runInContext("load('market')", context);
let finishMarketPost;
pendingPost = new Promise(resolve => { finishMarketPost = resolve; });
const marketSubmission = elements.marketForm.onsubmit({preventDefault() {}});
assert.equal(elements.marketSave.disabled, true);
assert.equal(elements.marketEnabled.disabled, true);
assert.equal(marketInput(3).disabled, true);
assert.equal(elements.volume.disabled, false, 'market save does not lock unrelated settings');
assert.equal(elements.weatherKey.disabled, false, 'market save does not lock weather draft');
const busyOrder = selectedMarket();
elements.marketOrder.children[0].children[2].onclick();
assert.deepEqual(selectedMarket(), busyOrder, 'busy market form cannot reorder');
finishMarketPost();
await marketSubmission;
pendingPost = null;
assert.equal(elements.marketSave.disabled, false);
const savedPresets = market.presets;
market.presets = market.presets.slice(1);
await vm.runInContext("load('market')", context);
assert.equal(elements.marketSave.disabled, true, 'invalid catalogue rejected');
assert.equal(elements.volume.value, 90, 'invalid market response leaves other drafts');
market.presets = savedPresets;
await vm.runInContext("load('market')", context);
assert.equal(elements.marketSave.disabled, false);
assert.equal(elements.marketMessage.textContent, '');
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
assert.equal(new URLSearchParams(lastBody).get('section'), 'general');
assert.doesNotMatch(lastBody, /alarm|updates/);
assert.equal(elements.alarmVolume.disabled, false, 'saving general does not lock alarm');
elements.conversationKey.value = 'edited-while-other-form-saves';
finishPost();
await submission;
assert.equal(elements.volume.disabled, false);
assert.equal(elements.conversationKey.value, 'edited-while-other-form-saves');
pendingPost = null;
elements.alarmTime.value = '08:45';
await elements.alarmForm.onsubmit({preventDefault() {}, target: elements.alarmForm});
assert.equal(lastPath, '/api/settings');
assert.match(lastBody, /section=alarm/);
assert.match(lastBody, /alarm_volume=25/);
assert.match(lastBody, /alarm_hour=8&alarm_minute=45/);
assert.doesNotMatch(lastBody, /timezone|unit|updates/);
assert.equal(elements.updates.value, 'beta', 'alarm save leaves update draft untouched');
const previousBody = lastBody;
confirmed = false;
await elements.updatesForm.onsubmit({preventDefault() {}, target: elements.updatesForm});
assert.equal(lastBody, previousBody, 'Beta requires confirmation');
confirmed = true;
await elements.updatesForm.onsubmit({preventDefault() {}, target: elements.updatesForm});
assert.equal(lastBody, 'section=updates&updates=beta');
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
const originalMusicTracks = structuredClone(music.tracks);
elements.musicFile.files = [{name: '新歌 + a.mp3', size: 9000}];
elements.musicFile.onchange();
assert.equal(elements.musicUpload.disabled, false);
let upload = elements.musicUpload.onclick();
assert.equal(request.path, '/api/music/upload?name=' + encodeURIComponent('新歌 + a.mp3'));
assert.equal(request.headers['X-RLCD-Token'], 'test-token');
assert.equal(elements.musicPlay.disabled, true);
request.upload.onprogress({lengthComputable: true, loaded: 9000, total: 9000});
assert.match(elements.musicMessage.textContent, /校验/);
request.onerror();
await upload;
assert.equal(elements.musicPlay.disabled, false, 'connection failure releases controls');
assert.equal(elements.musicFile.files.length, 1, 'failed upload keeps chosen file');
assert.equal(elements.musicFile.disabled, true, 'current-page queue retains its File object');
assert.match(elements.musicQueueState.textContent, /已完成 0 · 失败 0 · 待上传 0 · 待核对 1/);
assert.equal(elements.musicReconcile.hidden, false);
assert.equal(elements.musicRetryFailed.hidden, true, 'an uncertain write is never replayed');
assert.equal(elements.musicContinuePending.hidden, true);
request = null;
failedGets.set('/api/music', '另一个操作正在进行');
await elements.musicReconcile.onclick();
assert.equal(request, null, 'a failed catalog read never starts an upload');
assert.match(elements.musicQueueState.textContent, /待核对 1/);
assert.equal(elements.musicReconcile.disabled, true);
failedGets.delete('/api/music');
await vm.runInContext('loadMusic()', context);
for (const incomplete of [
  {ready: false, scanned: true, truncated: false},
  {ready: true, scanned: false, truncated: false},
  {ready: true, scanned: true, truncated: true},
]) {
  Object.assign(music, incomplete);
  request = null;
  await elements.musicReconcile.onclick();
  assert.equal(request, null, 'an incomplete catalog never starts an upload');
  assert.match(elements.musicQueueState.textContent, /待核对 1/);
  assert.equal(elements.musicContinuePending.hidden, true);
  assert.equal(elements.musicReconcile.disabled, true);
  Object.assign(music, {ready: true, scanned: true, truncated: false});
  await vm.runInContext('loadMusic()', context);
  assert.equal(elements.musicReconcile.disabled, false);
}
await elements.musicReconcile.onclick();
assert.match(elements.musicMessage.textContent, /未找到.*现可手动重试/);
assert.equal(elements.musicRetryFailed.hidden, false);
let retry = elements.musicRetryFailed.onclick();
await new Promise(resolve => setImmediate(resolve));
assert.equal(request.path, '/api/music/upload?name=' + encodeURIComponent('新歌 + a.mp3'));
request.status = 200; request.responseText = '歌曲已写入'; request.onload();
await retry;
assert.match(elements.musicMessage.textContent, /本次队列已完成 1 首/);
assert.equal(elements.musicQueueState.hidden, true);
assert.equal(elements.conversationKey.value, 'edited-while-other-form-saves');
request = null;
elements.musicFile.files = [{name: 'first.mp3', size: 9000}, {name: 'second.wav', size: 9000}, {name: 'third.mp3', size: 9000}];
upload = elements.musicUpload.onclick();
const firstRequest = request;
assert.match(request.path, /first\.mp3$/);
request.status = 200; request.onload();
await new Promise(resolve => setImmediate(resolve));
assert.notEqual(request, firstRequest, 'next file starts only after first finished');
assert.match(request.path, /second\.wav$/);
request.status = 409; request.responseText = '写入失败'; request.onload();
await upload;
assert.match(elements.musicQueueState.textContent, /已完成 1 · 失败 1 · 待上传 1/);
assert.equal(elements.musicRetryFailed.hidden, false);
assert.equal(elements.musicContinuePending.hidden, false);
assert.match(request.path, /second\.wav$/, 'third file was not sent');
let continuation = elements.musicContinuePending.onclick();
await new Promise(resolve => setImmediate(resolve));
assert.match(request.path, /third\.mp3$/);
request.status = 200; request.onload();
await continuation;
assert.match(elements.musicQueueState.textContent, /已完成 2 · 失败 1 · 待上传 0/);
assert.equal(elements.musicRetryFailed.hidden, false, 'continuing pending files retains failures');
music.tracks.push({name: 'second.wav', bytes: 9000});
request = null;
retry = elements.musicRetryFailed.onclick();
await retry;
assert.equal(request, null, 'a failed item matching a catalog entry is never replayed or marked complete');
assert.match(elements.musicQueueState.textContent, /已完成 2 · 失败 0 · 待上传 0 · 待核对 1/);
assert.equal(elements.musicSkipUncertain.hidden, false);
elements.musicSkipUncertain.onclick();
assert.match(elements.musicQueueState.textContent, /已完成 2 · 失败 1 · 待上传 0/);
elements.musicFinishQueue.onclick();
assert.equal(elements.musicQueueState.hidden, true);

// A dropped response may have committed, but an equal name and size still
// cannot prove which request created the catalog entry.
music.tracks = structuredClone(originalMusicTracks);
request = null;
elements.musicFile.files = [{name: 'uncertain.mp3', size: 9000}, {name: 'after.wav', size: 9100}];
upload = elements.musicUpload.onclick();
request.onerror();
await upload;
music.tracks.push({name: 'uncertain.mp3', bytes: 9000});
request = null;
await elements.musicReconcile.onclick();
assert.equal(request, null, 'catalog reconciliation performs no upload');
assert.match(elements.musicQueueState.textContent, /已完成 0 · 失败 0 · 待上传 1 · 待核对 1/);
assert.equal(elements.musicContinuePending.hidden, true);
assert.equal(elements.musicSkipUncertain.hidden, false);
elements.musicSkipUncertain.onclick();
assert.match(elements.musicQueueState.textContent, /已完成 0 · 失败 1 · 待上传 1/);
continuation = elements.musicContinuePending.onclick();
await new Promise(resolve => setImmediate(resolve));
assert.match(request.path, /after\.wav$/);
request.status = 200; request.onload();
await continuation;
assert.match(elements.musicQueueState.textContent, /已完成 1 · 失败 1 · 待上传 0/);
elements.musicFinishQueue.onclick();
assert.equal(elements.musicQueueState.hidden, true);

// A never-sent pending item with an equal-size catalog entry is also a
// conflict, not a completed upload.
music.tracks = structuredClone(originalMusicTracks);
request = null;
elements.musicFile.files = [{name: 'blocked.mp3', size: 9000}, {name: 'pending.wav', size: 9200}];
upload = elements.musicUpload.onclick();
request.status = 409; request.responseText = '写入失败'; request.onload();
await upload;
music.tracks.push({name: 'pending.wav', bytes: 9200});
request = null;
await elements.musicContinuePending.onclick();
assert.equal(request, null, 'a pending item matching a catalog entry is never sent or marked complete');
assert.match(elements.musicQueueState.textContent, /已完成 0 · 失败 1 · 待上传 0 · 待核对 1/);
assert.equal(elements.musicSkipUncertain.hidden, false);
assert.equal(elements.musicContinuePending.hidden, true);
elements.musicSkipUncertain.onclick();
assert.match(elements.musicQueueState.textContent, /已完成 0 · 失败 2 · 待上传 0/);
assert.equal(elements.musicFinishQueue.hidden, false);
elements.musicFinishQueue.onclick();
assert.equal(elements.musicQueueState.hidden, true);
music.tracks = structuredClone(originalMusicTracks);
music.tracks = Array.from({length: 31}, (_, index) => ({name: `catalog-${index}.mp3`, bytes: 9000}));
await vm.runInContext('loadMusic()', context);
request = null;
elements.musicFile.files = [{name: 'slot-a.mp3', size: 9000}, {name: 'slot-b.mp3', size: 9000}];
await elements.musicUpload.onclick();
assert.equal(request, null, 'catalog plus selected files cannot exceed the 32-song limit');
assert.match(elements.musicMessage.textContent, /不能超过 32 首/);
music.tracks = structuredClone(originalMusicTracks);
await vm.runInContext('loadMusic()', context);
request = null;
elements.musicFile.files = [{name: 'a.mp3', size: 9000}, {name: 'A.MP3', size: 9000}];
await elements.musicUpload.onclick();
assert.equal(request, null, 'duplicates in queue rejected before any upload');
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
state.local_network = true;
state.token = sessionToken;
unauthorized = true;
await assert.rejects(vm.runInContext('load()', context), /授权/);
assert.equal(elements.pairPanel.hidden, false);
elements.pairCode.value = 'abc12345';
const requestsBeforePair = getRequests.length;
await elements.pairForm.onsubmit({preventDefault() {}});
assert.equal(lastBody, 'code=ABC12345');
assert.equal(elements.pairCode.value, '');
assert.equal(elements.pairPanel.hidden, true);
assert.equal(elements.lanPanel.hidden, false);
assert.equal(elements.wifiLabel.textContent, '当前连接');
for (const id of ['wifiEdit','forgetWifi','weatherKeySave','conversationSave','defaults','file','upload','starterImages']) {
  assert.equal(elements[id].disabled, true, id + ' requires hotspot');
}
assert.equal(elements.conversationPreferencesSave.disabled, true, 'preferences remain unavailable before an API Key exists');
assert.match(elements.conversationPreferencesHint.textContent, /首次使用请切换到设备热点.*API Key/);
for (const id of ['weatherSave','weatherClear','conversationClear','weatherApiHost','weatherProvince','conversationApiHost','conversationModel']) {
  assert.equal(elements[id].disabled, false, id + ' is available on authenticated LAN');
}
assert.equal(elements.marketSave.disabled, false, 'preset-only market configuration available on authenticated LAN');
await elements.marketForm.onsubmit({preventDefault() {}});
assert.equal(lastPath, '/api/market/config');
const beforeBlockedPost = lastPath;
await vm.runInContext('userActivity()', context);
assert.equal(lastPath, '/api/activity');
lastPath = 'unchanged';
await vm.runInContext('userActivity()', context);
assert.equal(lastPath, 'unchanged', 'user activity is throttled; polling never renews');
lastPath = beforeBlockedPost;
await assert.rejects(vm.runInContext("post('/api/conversation/config','api_key=never-send-this')", context), /热点/);
await assert.rejects(vm.runInContext("post('/api/conversation/key','api_key=never-send-this')", context), /热点/);
await assert.rejects(vm.runInContext("post('/api/weather/key','api_key=never-send-this')", context), /热点/);
await assert.rejects(vm.runInContext("post('/api/weather/config','api_key=never-send-this')", context), /热点/);
assert.equal(lastPath, beforeBlockedPost, 'blocked credentials never leave the browser');
for (const item of getRequests.slice(requestsBeforePair)) assert.equal(item.options.headers['X-RLCD-Token'], sessionToken);
confirmed = false;
await elements.useHotspot.onclick();
assert.equal(lastPath, beforeBlockedPost);
confirmed = true;
await elements.useHotspot.onclick();
assert.equal(lastPath, '/api/hotspot');
assert.equal(lastBody, 'confirm=HOTSPOT');
const originalHandlers = Object.fromEntries(ids.map(id => [id, Object.fromEntries(['onclick','onsubmit','onchange','oninput','ontoggle','onpointerdown','onpointermove','onpointerup','onpointercancel','onlostpointercapture'].map(event => [event,elements[id][event]]))]));
const originalDefaultsHandler = elements.defaults.onclick;
const qrContext = vm.createContext({...context,
  location: {hash: '#' + sessionToken, pathname: '/'},
  history: {replaceState(data, title, path) { assert.equal(path, '/'); }},
});
const getCount = getRequests.length;
vm.runInContext(javascript, qrContext);
await new Promise(resolve => setImmediate(resolve));
assert.equal(getRequests[getCount].options.headers['X-RLCD-Token'], sessionToken, 'QR fragment authorizes the first request');
assert.doesNotMatch(source.split('static const char RECOVERY_SETTINGS_PAGE[] =')[1].split('static const char SETTINGS_PAGE[] =')[0], /api\/music|musicTracks|marketForm|api\/market/);
for (const route of ['market_get_uri', 'market_post_uri']) {
  assert.ok(source.includes('if (error == ESP_OK && !recovery_mode) {\n        error = httpd_register_uri_handler(s_http_server, &' + route + ');'), 'market handlers excluded from recovery');
}
// Page selection is a separate form, including on authenticated LAN. All off
// is valid: HOME and maintenance entry are intentionally not checkboxes.
// Restore handlers after the separate QR-context test above.
for (const id of ids) Object.assign(elements[id], originalHandlers[id]);
elements.defaults.onclick = originalDefaultsHandler;
assert.equal(elements.pagesSave.disabled, false);
assert.equal(elements.pageHome, undefined);
elements.volume.value = 43;
elements.alarmVolume.value = 31;
elements.conversationKey.value = 'keep-ai-draft';
elements.weatherKey.value = 'keep-weather-draft';
const marketDraft = selectedMarket();
for (let mask = 0; mask <= 255; mask++) {
  pageIds.forEach((id, index) => { elements[id].checked = (mask & (1 << index)) !== 0; });
  await elements.pagesForm.onsubmit({preventDefault() {}, target: elements.pagesForm});
  assert.equal(lastPath, '/api/settings');
  assert.equal(lastBody, 'section=pages&daily_pages_mask=' + mask + '&daily_pages_order=1%2C2%2C4%2C64%2C8%2C16%2C32%2C128');
  assert.equal(state.daily_pages_mask, mask);
  assert.equal(elements.volume.value, 43);
  assert.equal(elements.alarmVolume.value, 31);
  assert.equal(elements.conversationKey.value, 'keep-ai-draft');
  assert.equal(elements.weatherKey.value, 'keep-weather-draft');
  assert.deepEqual(selectedMarket(), marketDraft);
}
elements.pageCalendar.checked = false;
await vm.runInContext("load('settings')", context);
assert.equal(elements.pageCalendar.checked, false, 'general reload preserves page draft');
elements.volume.value = 43;
failPost = true;
await elements.pagesForm.onsubmit({preventDefault() {}, target: elements.pagesForm});
assert.equal(elements.pageCalendar.checked, false, 'failed save preserves page draft');
assert.equal(state.daily_pages_mask, 255);
assert.equal(elements.pagesMessage.textContent, '保存失败');
assert.equal(elements.pagesSave.disabled, false);
failPost = false;
let finishPagesPost;
pendingPost = new Promise(resolve => { finishPagesPost = resolve; });
const pagesSubmission = elements.pagesForm.onsubmit({preventDefault() {}, target: elements.pagesForm});
assert.equal(elements.pagesSave.disabled, true);
assert.ok(pageIds.every(id => elements[id].disabled));
assert.equal(elements.volume.disabled, false);
finishPagesPost();
await pagesSubmission;
pendingPost = null;
state.daily_pages_mask = 256;
await vm.runInContext("load('pages')", context);
assert.equal(elements.pagesSave.disabled, true, 'unsupported mask is not silently truncated');
state.daily_pages_mask = 0;
await vm.runInContext("load('pages')", context);
assert.equal(elements.pagesSave.disabled, false);
assert.ok(pageIds.every(id => !elements[id].checked));

// A successful enabled weather save is now an ordinary mutation. The exact
// browser JS must release its own controls and reload only the weather form.
state.local_network = false;
await vm.runInContext("load('weather')", context);
elements.weatherEnabled.value = 'on';
elements.weatherApiHost.value = 'test.qweatherapi.com';
elements.weatherKey.value = 'test-not-a-real-key';
elements.weatherProvince.value = '1';
elements.weatherCity.value = '1';
vm.runInContext('weatherControls()', context);
const beforeMissingWeatherKey = writes.length;
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.equal(writes.length, beforeMissingWeatherKey, 'unsaved key draft does not enable weather');
assert.match(elements.weatherMessage.textContent, /保存 API Key/);
const beforeWeatherKeyGets = getRequests.length;
await elements.weatherKeyForm.onsubmit({preventDefault() {}, target: elements.weatherKeyForm});
assert.equal(lastPath, '/api/weather/key');
assert.deepEqual([...new URLSearchParams(lastBody)], [['api_key', 'test-not-a-real-key']]);
assert.equal(state.weather.enabled, false, 'first key save never enables weather');
assert.equal(elements.weatherEnabled.value, 'on');
assert.equal(elements.weatherApiHost.value, 'test.qweatherapi.com');
assert.equal(elements.weatherProvince.value, '1');
assert.equal(elements.weatherCity.value, '1');
assert.equal(elements.weatherKey.value, '');
assert.ok(getRequests.slice(beforeWeatherKeyGets).every(({path}) => path === '/api/state'), 'key reload does not fetch or reset region choices');
elements.weatherKey.value = 'unsaved-weather-rotation';
elements.pageMusic.checked = true;
elements.conversationKey.value = 'keep-ai-draft';
elements.wifiForm.hidden = false;
elements.wifiSsid.value = 'keep-wifi-draft';
confirmed = true;
let finishWeatherPost;
pendingPost = new Promise(resolve => { finishWeatherPost = resolve; });
const beforeWeatherRequests = getRequests.length;
const weatherSubmission = elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.equal(elements.weatherSave.disabled, true);
assert.equal(elements.pagesSave.disabled, false);
assert.equal(elements.volume.disabled, false);
finishWeatherPost();
await weatherSubmission;
pendingPost = null;
assert.equal(lastPath, '/api/weather/preferences');
assert.equal(new URLSearchParams(lastBody).get('enabled'), 'on');
assert.deepEqual([...new URLSearchParams(lastBody).keys()], ['enabled', 'api_host', 'province', 'city']);
assert.equal(elements.weatherSave.disabled, false, 'enabled weather save does not leave a closing/busy page');
assert.equal(elements.weatherClear.disabled, false);
assert.equal(elements.weatherKey.value, 'unsaved-weather-rotation', 'preferences preserve key draft');
assert.equal(elements.conversationKey.value, 'keep-ai-draft');
assert.equal(elements.volume.value, 43);
assert.equal(elements.pageMusic.checked, true);
assert.equal(elements.wifiSsid.value, 'keep-wifi-draft');
assert.deepEqual(selectedMarket(), marketDraft);
assert.ok(getRequests.slice(beforeWeatherRequests).every(({path}) =>
  path === '/api/state' || path.startsWith('/api/weather/regions')), 'weather save reloads only its own inputs');
// Repeated saves still work. Disconnect is not presented as a successful
// handoff; errors keep non-secret drafts and unlock retry.
elements.weatherApiHost.value = 'next.qweatherapi.com';
disconnectedPost = true;
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
disconnectedPost = false;
assert.match(elements.weatherMessage.textContent, /操作结果尚未确认/);
assert.equal(elements.weatherSave.disabled, false);
assert.equal(elements.weatherApiHost.value, 'next.qweatherapi.com');
assert.equal(state.weather.api_host, 'test.qweatherapi.com');
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.equal(state.weather.api_host, 'next.qweatherapi.com');
elements.weatherEnabled.value = 'off';
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.equal(state.weather.enabled, false);
assert.equal(elements.weatherSave.disabled, false);
assert.equal(elements.pageMusic.checked, true);
assert.match(html, /恢复时区、单位、音量、显示页面、计时时长、音乐播放方式、闹钟与更新偏好/);
let finishDefaultsPost;
pendingPost = new Promise(resolve => { finishDefaultsPost = resolve; });
const defaultsSubmission = elements.defaults.onclick();
assert.match(lastConfirmation, /显示页面/);
assert.equal(elements.pagesSave.disabled, true);
assert.ok(pageIds.every(id => elements[id].disabled));
finishDefaultsPost();
await defaultsSubmission;
pendingPost = null;
assert.ok(pageIds.every(id => elements[id].checked), 'restoring defaults reloads page visibility');
assert.equal(elements.pagesSave.disabled, false);
assert.equal(elements.conversationKey.value, 'keep-ai-draft');
assert.equal(elements.weatherApiHost.value, 'next.qweatherapi.com');
const recoveryMarkup = source.split('static const char RECOVERY_SETTINGS_PAGE[] =')[1].split('static const char SETTINGS_PAGE[] =')[0];
assert.doesNotMatch(recoveryMarkup, /pagesForm|daily_pages_mask|timerForm|timer_minutes|imageFit/);
assert.ok(serverDefinition.includes('if (error == ESP_OK && !recovery_mode) {\n        error = httpd_register_uri_handler(s_http_server, &settings_uri);'));

// Music preferences are independent of catalog polling and of the temporary
// sleep timer. In particular saving with no card must not start playback.
state.local_network = false;
await vm.runInContext("load('musicSettings')", context);
assert.equal(elements.musicMode.value, 'sequential');
assert.equal(elements.musicMode.disabled, false, 'playback preference can be edited without an SD card');
elements.musicMode.value = 'repeat_all';
elements.volume.value = 73;
elements.weatherKey.value = 'keep-weather-secret';
const beforeModeWrites = writes.length;
await elements.musicSettingsForm.onsubmit({preventDefault() {}, target: elements.musicSettingsForm});
assert.equal(lastBody, 'section=music&music_mode=repeat_all');
assert.equal(state.music_mode, 'repeat_all');
assert.equal(elements.volume.value, 73);
assert.equal(elements.weatherKey.value, 'keep-weather-secret');
assert.ok(writes.slice(beforeModeWrites).every(item => item.path === '/api/settings'), 'mode save never starts playback or a timer');
elements.musicMode.value = 'repeat_one';
await vm.runInContext('loadMusic()', context);
assert.equal(elements.musicMode.value, 'repeat_one', 'periodic song refresh preserves mode draft');
failPost = true;
await elements.musicSettingsForm.onsubmit({preventDefault() {}, target: elements.musicSettingsForm});
failPost = false;
assert.equal(elements.musicMode.value, 'repeat_one');
assert.equal(elements.musicMode.disabled, false);
assert.equal(elements.musicSettingsMessage.textContent, '保存失败');
assert.equal(elements.musicSleepStart.disabled, true, 'stopped music cannot start a timer');
const beforeStoppedTimer = writes.length;
await elements.musicSleepStart.onclick();
assert.equal(writes.length, beforeStoppedTimer);
music = {...music, ready: true, state: 'playing', playback_mode: 'repeat_all'};
await vm.runInContext('loadMusic()', context);
elements.musicSleep.value = '30';
await elements.musicSleepStart.onclick();
assert.equal(lastPath, '/api/music/sleep');
assert.equal(lastBody, 'minutes=30');
assert.match(elements.musicSleepState.textContent, /30 分钟后停止/);
assert.match(elements.musicPlayback.textContent, /列表循环/);
assert.equal(elements.musicMode.value, 'repeat_one');
assert.equal(elements.musicSleepCancel.disabled, false);
music.state = 'paused';
await vm.runInContext('loadMusic()', context);
assert.equal(elements.musicSleepStart.disabled, false, 'paused playback can set a timer');
state.local_network = true;
await vm.runInContext("load('session')", context);
await elements.musicSleepCancel.onclick();
assert.equal(lastBody, 'minutes=0', 'timer cancellation is allowed over authorized LAN');
assert.equal(elements.musicSleepState.textContent, '未开启定时');
assert.equal(elements.musicSleepCancel.disabled, true);
await elements.musicSettingsForm.onsubmit({preventDefault() {}, target: elements.musicSettingsForm});
assert.equal(state.music_mode, 'repeat_one', 'playback mode can be saved over authorized LAN');
state.local_network = false;
await vm.runInContext("load('session')", context);
music.sleep_remaining_seconds = 900;
const beforeDefaultsWrites = writes.length;
await elements.defaults.onclick();
assert.equal(elements.musicMode.value, 'sequential');
assert.equal(music.sleep_remaining_seconds, 900);
assert.ok(writes.slice(beforeDefaultsWrites).every(item => item.path !== '/api/music/sleep'), 'defaults never changes this playback timer');
assert.doesNotMatch(recoveryMarkup, /musicSettingsForm|musicSleep/);
assert.match(source, /settings_portal_parse_music_sleep_form\(body, length, &minutes\)/);
assert.match(source, /if \(boot_recovery_is_active\(\)\) return send_mutation_unavailable\(request\);/);

// Retry only the failed section, leaving all other drafts (including secrets)
// in memory. It is a read, never an automatic replay of a previous mutation.
failedGets.set('/api/market/config', '行情设置暂时不可用');
elements.volume.value = 81;
elements.musicMode.value = 'repeat_all';
elements.conversationKey.value = 'preserve-ai-secret';
await vm.runInContext("load('market')", context);
assert.equal(elements.marketSave.disabled, true);
assert.equal(elements.volume.disabled, false);
assert.equal(elements.loadNotice.hidden, false);
assert.match(elements.loadMessage.textContent, /市场看板/);
failedGets.delete('/api/market/config');
const beforeRetryWrites = writes.length, beforeRetryGets = getRequests.length;
await elements.retryLoads.onclick();
assert.equal(elements.marketSave.disabled, false);
assert.equal(elements.volume.value, 81);
assert.equal(elements.musicMode.value, 'repeat_all');
assert.equal(elements.conversationKey.value, 'preserve-ai-secret');
assert.equal(writes.length, beforeRetryWrites);
assert.ok(getRequests.slice(beforeRetryGets).every(item => ['/api/state', '/api/market/config'].includes(item.path)));
failedGets.set('/api/weather/regions?province=1', '城市列表读取失败');
elements.weatherProvince.value = '1';
elements.weatherApiHost.value = 'keep-host.qweatherapi.com';
elements.weatherKey.value = 'keep-key-during-city-retry';
await elements.weatherProvince.onchange();
assert.match(elements.loadMessage.textContent, /城市列表/);
failedGets.delete('/api/weather/regions?province=1');
await elements.retryLoads.onclick();
assert.equal(elements.weatherApiHost.value, 'keep-host.qweatherapi.com');
assert.equal(elements.weatherKey.value, 'keep-key-during-city-retry');
assert.equal(elements.musicMode.value, 'repeat_all');

// Bound both the response headers and its body. A timed-out POST is explicitly
// uncertain and is never replayed by the read-only retry control.
assert.equal(timers.size, 0);
function expireRequest(expectedMs) {
  assert.equal(timers.size, 1);
  const [id, timer] = timers.entries().next().value;
  assert.equal(timer.ms, expectedMs);
  timers.delete(id);
  timer.callback();
}
hangRead = true;
let timed = vm.runInContext("load('pages')", context);
expireRequest(15000);
await assert.rejects(timed, /读取超时/);
assert.equal(timers.size, 0);
await elements.retryLoads.onclick();
hangBody = true;
timed = vm.runInContext("load('pages')", context);
await new Promise(resolve => setImmediate(resolve));
expireRequest(15000);
await assert.rejects(timed, /读取超时/);
await elements.retryLoads.onclick();
hangPost = true;
elements.musicMode.value = 'repeat_one';
const beforeTimedPost = writes.length;
timed = elements.musicSettingsForm.onsubmit({preventDefault() {}, target: elements.musicSettingsForm});
expireRequest(45000);
await timed;
assert.equal(writes.length, beforeTimedPost + 1);
assert.match(elements.musicSettingsMessage.textContent, /操作结果尚未确认/);
assert.equal(elements.musicMode.value, 'repeat_one');
assert.equal(elements.musicMode.disabled, false);
assert.equal(elements.loadNotice.hidden, false);
assert.match(elements.loadMessage.textContent, /播放设置/);
const getsBeforeUncertainRead = getRequests.length;
await elements.retryLoads.onclick();
assert.equal(writes.length, beforeTimedPost + 1, 'retry does not replay timed-out mutation');
assert.equal(getRequests.length, getsBeforeUncertainRead + 1, 'uncertain mode save offers one scoped state read');
assert.equal(elements.musicMode.value, state.music_mode, 'explicit retry shows actual saved mode');
assert.equal(elements.musicSettingsMessage.textContent, '', 'successful verification clears stale uncertain message');
assert.equal(elements.loadNotice.hidden, true);

// Ordering keeps the eight stable IDs, preserves hidden pages and independent
// drafts, and changes only after explicit save. Invalid server data blocks it.
state.daily_pages_order = [1,2,4,64,8,16,32,128];
await vm.runInContext("load('pages')", context);
assert.equal(elements.pageWeatherUp.disabled, true);
assert.equal(elements.pageEnvironmentDown.disabled, true);
elements.pageChatUp.onclick();
const orderedBits = () => Array.from(vm.runInContext('dailyPageOrder', context));
assert.deepEqual(orderedBits(), [1,2,4,64,8,32,16,128]);
assert.deepEqual(state.daily_pages_order, [1,2,4,64,8,16,32,128]);
elements.pageChat.checked = false;
await vm.runInContext("load('settings')", context);
elements.navNetwork.onclick(); elements.navGeneral.onclick();
assert.deepEqual(orderedBits(), [1,2,4,64,8,32,16,128]);
assert.equal(elements.pageChat.checked, false);
await elements.pagesForm.onsubmit({preventDefault() {}, target: elements.pagesForm});
assert.deepEqual(state.daily_pages_order, [1,2,4,64,8,32,16,128]);
assert.equal(elements.pageOrder.children[5], elements.pageChatRow);
failPost = true;
elements.pageChatUp.onclick();
await elements.pagesForm.onsubmit({preventDefault() {}, target: elements.pagesForm});
assert.deepEqual(orderedBits(), [1,2,4,64,32,8,16,128], 'failed save retains ordered draft');
assert.deepEqual(state.daily_pages_order, [1,2,4,64,8,32,16,128]);
failPost = false;
for (const invalid of [undefined, [], [1,2,4,8,16,32], [1,1,4,64,8,16,32], [1,2,4,8,16,64,128], ['1',2,4,64,8,16,32]]) {
  state.daily_pages_order = invalid;
  await vm.runInContext("load('pages')", context);
  assert.equal(elements.pagesSave.disabled, true);
  const previous = orderedBits();
  elements.pageChatUp.onclick();
  assert.deepEqual(orderedBits(), previous);
}
state.daily_pages_order = [1,2,4,64,8,16,32,128];
await vm.runInContext("load('pages')", context);

// Daily AI preferences and service credentials are separate forms and trust
// boundaries. Credentials remain AP-only; configured preferences also work on
// authenticated household Wi-Fi. Neither scoped reload overwrites the other
// form's unsaved draft.
state.local_network = false;
state.conversation = {...state.conversation, configured: false, enabled: false,
  voice: 'longanqian', reply_style: 'concise', model: 'qwen-audio-3.0-realtime-flash',
  api_host: 'dashscope.aliyuncs.com', shared_endpoint: true};
await vm.runInContext("load('conversation')", context);
assert.equal(elements.conversationCredentials.open, true, 'missing credentials expand their otherwise-collapsed panel');
assert.equal(elements.conversationPreferencesSave.disabled, true);
assert.match(elements.conversationPreferencesHint.textContent, /下方保存 API Key/);
assert.equal(elements.conversationSave.disabled, false);
for (const id of ['conversationEnabled', 'conversationVoice', 'conversationReplyStyle', 'conversationModel', 'conversationApiHost']) {
  assert.equal(elements[id].disabled, false, 'preferences may be drafted before a key is saved');
}
const beforeUnconfiguredPreference = writes.length;
await elements.conversationPreferencesForm.onsubmit({preventDefault() {}, target: elements.conversationPreferencesForm});
assert.equal(writes.length, beforeUnconfiguredPreference, 'preferences cannot save before an API Key exists');

elements.conversationEnabled.value = 'on';
elements.conversationVoice.value = 'longanlingxi';
elements.conversationReplyStyle.value = 'english_practice';
elements.conversationKey.value = 'test-credential-not-real';
elements.conversationApiHost.value = 'draft.cn-beijing.maas.aliyuncs.com';
await vm.runInContext("load('conversationPreferences')", context);
assert.equal(elements.conversationKey.value, 'test-credential-not-real', 'preference reload preserves credential secret draft');
assert.equal(elements.conversationApiHost.value, '', 'preference reload refreshes Host as part of the same form');
elements.conversationEnabled.value = 'on';
elements.conversationVoice.value = 'longanlingxi';
elements.conversationReplyStyle.value = 'english_practice';
elements.conversationApiHost.value = 'draft.ap-southeast-1.maas.aliyuncs.com';
elements.conversationModel.value = 'qwen-audio-3.0-realtime-flash';
elements.volume.value = 37;
const aiMutationsStart = writes.length;
await elements.conversationForm.onsubmit({preventDefault() {}, target: elements.conversationForm});
assert.equal(lastPath, '/api/conversation/key');
let aiFields = new URLSearchParams(lastBody);
assert.deepEqual([...aiFields.keys()], ['api_key']);
assert.equal(aiFields.get('service'), null);
assert.equal(aiFields.get('api_key'), 'test-credential-not-real');
assert.equal(aiFields.get('enabled'), null);
assert.equal(aiFields.get('voice'), null);
assert.equal(aiFields.get('reply_style'), null);
assert.equal(aiFields.get('api_host'), null);
assert.equal(aiFields.get('model'), null);
assert.equal(state.conversation.configured, true);
assert.equal(state.conversation.enabled, false, 'credential save does not overwrite daily preferences');
assert.equal(state.conversation.voice, 'longanqian');
assert.equal(elements.conversationEnabled.value, 'on', 'credential reload preserves preference draft');
assert.equal(elements.conversationVoice.value, 'longanlingxi');
assert.equal(elements.conversationReplyStyle.value, 'english_practice');
assert.equal(elements.conversationApiHost.value, 'draft.ap-southeast-1.maas.aliyuncs.com');
assert.equal(elements.conversationModel.value, 'qwen-audio-3.0-realtime-flash');
assert.equal(state.conversation.api_host, 'dashscope.aliyuncs.com');
assert.equal(elements.conversationKey.value, '');
assert.equal(elements.volume.value, 37);

elements.conversationKey.value = 'unsaved-credential-secret';
elements.conversationApiHost.value = 'selected.cn-beijing.maas.aliyuncs.com';
await elements.conversationPreferencesForm.onsubmit({preventDefault() {}, target: elements.conversationPreferencesForm});
assert.equal(lastPath, '/api/conversation/preferences');
aiFields = new URLSearchParams(lastBody);
assert.deepEqual([...aiFields.keys()], ['enabled', 'voice', 'reply_style', 'model', 'api_host']);
assert.equal(aiFields.get('enabled'), 'on');
assert.equal(aiFields.get('voice'), 'longanlingxi');
assert.equal(aiFields.get('reply_style'), 'english_practice');
assert.equal(aiFields.get('service'), null);
assert.equal(aiFields.get('api_key'), null);
assert.equal(aiFields.get('model'), 'qwen-audio-3.0-realtime-flash');
assert.equal(aiFields.get('api_host'), 'selected.cn-beijing.maas.aliyuncs.com');
assert.equal(state.conversation.enabled, true);
assert.equal(state.conversation.voice, 'longanlingxi');
assert.equal(state.conversation.reply_style, 'english_practice');
assert.equal(elements.conversationKey.value, 'unsaved-credential-secret', 'preference save preserves credential secret draft');
assert.equal(elements.conversationApiHost.value, 'selected.cn-beijing.maas.aliyuncs.com');
assert.equal(state.conversation.api_host, 'selected.cn-beijing.maas.aliyuncs.com');
assert.ok(writes.slice(aiMutationsStart).every(item =>
  ['/api/conversation/key', '/api/conversation/preferences'].includes(item.path)),
  'saving local AI settings never starts a cloud conversation or paid test');

// Definite failures retain the edited form and the other form's draft. Secret
// input is deliberately cleared after a credential submission attempt.
elements.conversationEnabled.value = 'off';
elements.conversationVoice.value = 'longanxiaoxin';
elements.conversationReplyStyle.value = 'concise';
const savedConversation = structuredClone(state.conversation);
failPost = true;
await elements.conversationPreferencesForm.onsubmit({preventDefault() {}, target: elements.conversationPreferencesForm});
failPost = false;
assert.deepEqual(state.conversation, savedConversation);
assert.equal(elements.conversationEnabled.value, 'off');
assert.equal(elements.conversationVoice.value, 'longanxiaoxin');
assert.equal(elements.conversationPreferencesSave.disabled, false);
assert.equal(elements.conversationPreferencesMessage.textContent, '保存失败');
assert.equal(elements.conversationKey.value, 'unsaved-credential-secret');

elements.conversationKey.value = 'failed-secret';
elements.conversationApiHost.value = 'failed.credentials.example';
failPost = true;
await elements.conversationForm.onsubmit({preventDefault() {}, target: elements.conversationForm});
failPost = false;
assert.deepEqual(state.conversation, savedConversation);
assert.equal(elements.conversationKey.value, '', 'credential attempt clears the secret field even on failure');
assert.equal(elements.conversationApiHost.value, 'failed.credentials.example');
assert.equal(elements.conversationVoice.value, 'longanxiaoxin');
assert.equal(elements.conversationMessage.textContent, '保存失败');
assert.equal(elements.conversationSave.disabled, false);

renderConversation({voice: 'unknown'});
assert.equal(elements.conversationPreferencesSave.disabled, true);
assert.equal(elements.conversationSave.disabled, true);
renderConversation({reply_style: 'unknown'});
assert.equal(elements.conversationPreferencesSave.disabled, true);
assert.equal(elements.conversationSave.disabled, true);
await vm.runInContext("load('conversation')", context);

state.local_network = true;
await vm.runInContext("load('session')", context);
assert.equal(elements.conversationPreferencesSave.disabled, false, 'configured daily preferences are available on authenticated LAN');
assert.equal(elements.conversationSave.disabled, true, 'credentials stay AP-only');
assert.equal(elements.conversationClear.disabled, false, 'confirmed clear is available on authenticated LAN');
assert.equal(elements.conversationCredentialsHint.hidden, false);
assert.match(html, /conversationCredentialsHint[^>]*[^<]*<\/p>/);
assert.match(html, /填写或更换 API Key，请使用页面顶部的“切换到设备热点”。其他对话设置可直接保存/);
elements.conversationKey.value = 'never-send-over-lan';
const beforeLanCredentialSubmit = writes.length;
await elements.conversationForm.onsubmit({preventDefault() {}, target: elements.conversationForm});
assert.equal(writes.length, beforeLanCredentialSubmit, 'credential form guard sends nothing over LAN');
assert.equal(elements.conversationKey.value, 'never-send-over-lan');
elements.conversationEnabled.value = 'off';
elements.conversationVoice.value = 'longanlufeng';
elements.conversationReplyStyle.value = 'concise';
elements.conversationApiHost.value = 'dashscope-intl.aliyuncs.com';
await elements.conversationPreferencesForm.onsubmit({preventDefault() {}, target: elements.conversationPreferencesForm});
assert.equal(lastPath, '/api/conversation/preferences');
assert.equal(state.conversation.enabled, false);
assert.equal(state.conversation.voice, 'longanlufeng');
assert.equal(state.conversation.api_host, 'dashscope-intl.aliyuncs.com');
assert.equal(elements.conversationKey.value, 'never-send-over-lan');
const beforeLanClear = writes.length;
confirmed = false;
await elements.conversationClear.onclick();
assert.equal(writes.length, beforeLanClear, 'cancelled confirmation never clears over LAN');
assert.match(lastConfirmation, /未保存的对话设置也会清空/);
assert.equal(state.conversation.configured, true);
confirmed = true;

// A 401 never mutates saved preferences or silently replays the request. Pairing
// again restores controls while retaining the unsaved preference draft.
elements.conversationVoice.value = 'longanlingxin';
const beforeUnauthorizedState = structuredClone(state.conversation);
const preferencesBeforeUnauthorized = writes.filter(item => item.path === '/api/conversation/preferences').length;
unauthorized = true;
await elements.conversationPreferencesForm.onsubmit({preventDefault() {}, target: elements.conversationPreferencesForm});
assert.deepEqual(state.conversation, beforeUnauthorizedState);
assert.equal(writes.filter(item => item.path === '/api/conversation/preferences').length, preferencesBeforeUnauthorized + 1);
assert.equal(elements.pairPanel.hidden, false);
assert.match(elements.conversationPreferencesMessage.textContent, /重新授权/);
assert.equal(elements.conversationVoice.value, 'longanlingxin');
assert.equal(elements.conversationPreferencesSave.disabled, true);
elements.pairCode.value = 'abc12345';
await elements.pairForm.onsubmit({preventDefault() {}});
assert.equal(writes.filter(item => item.path === '/api/conversation/preferences').length, preferencesBeforeUnauthorized + 1,
  'pairing retries only state reads and never replays the rejected preference write');
assert.equal(elements.conversationPreferencesSave.disabled, false);
assert.equal(elements.conversationVoice.value, 'longanlingxin');
state.local_network = false;
await vm.runInContext("load('session')", context);
assert.equal(elements.conversationSave.disabled, false);
assert.equal(elements.conversationCredentialsHint.hidden, true);

state.local_network = true;
await vm.runInContext("load('session')", context);
confirmed = true;
elements.conversationApiHost.value = 'discarded.cn-beijing.maas.aliyuncs.com';
elements.conversationKey.value = 'discarded-key-draft';
const beforeFailedConversationClear = structuredClone(state.conversation);
failPost = true;
await elements.conversationClear.onclick();
failPost = false;
assert.deepEqual(state.conversation, beforeFailedConversationClear);
assert.equal(elements.conversationApiHost.value, 'discarded.cn-beijing.maas.aliyuncs.com');
assert.equal(elements.conversationKey.value, 'discarded-key-draft');
assert.equal(elements.conversationMessage.textContent, '保存失败');
await elements.conversationClear.onclick();
assert.equal(lastPath, '/api/conversation/clear');
assert.equal(lastBody, 'confirm=CLEAR_CONVERSATION');
assert.equal(state.conversation.configured, false);
assert.equal(elements.conversationPreferencesSave.disabled, true);
assert.equal(elements.conversationEnabled.value, 'off');
assert.equal(elements.conversationVoice.value, 'longanqian');
assert.equal(elements.conversationReplyStyle.value, 'concise');
assert.equal(elements.conversationApiHost.value, '');
assert.equal(elements.conversationKey.value, '');
assert.equal(elements.conversationModel.value, 'qwen-audio-3.0-realtime-flash');
assert.equal(elements.conversationCredentials.open, true);
state.local_network = false;
await vm.runInContext("load('session')", context);

// Weather follows the same split boundary: ordinary settings and confirmed
// clear work on LAN; only key entry requires the AP. Each reload owns only its
// form, and credentials do not depend on the province/city directory loading.
state.weather = {...state.weather, enabled: false, configured: true, key_saved: true,
  api_host: 'saved.qweatherapi.com', province_id: 1, city_id: 1};
await vm.runInContext("load('weather')", context);
elements.weatherKey.value = 'weather-secret-draft';
elements.weatherApiHost.value = 'draft.qweatherapi.com';
elements.weatherProvince.value = '2';
elements.weatherCity.value = '3';
elements.weatherEnabled.value = 'on';
const keyReloadGets = getRequests.length;
await vm.runInContext("load('weatherKey')", context);
assert.equal(elements.weatherKey.value, '');
assert.equal(elements.weatherApiHost.value, 'draft.qweatherapi.com');
assert.equal(elements.weatherProvince.value, '2');
assert.equal(elements.weatherCity.value, '3');
assert.equal(elements.weatherEnabled.value, 'on');
assert.ok(getRequests.slice(keyReloadGets).every(item => item.path === '/api/state'));
elements.weatherKey.value = 'keep-key-on-preference-read';
await vm.runInContext("load('weatherPreferences')", context);
assert.equal(elements.weatherApiHost.value, 'saved.qweatherapi.com');
assert.equal(elements.weatherKey.value, 'keep-key-on-preference-read');

// Definite key-save failures clear the sensitive input, but never discard
// independently edited Host, location or on/off selections.
elements.weatherApiHost.value = 'draft.qweatherapi.com';
elements.weatherProvince.value = '2';
elements.weatherCity.value = '3';
elements.weatherEnabled.value = 'on';
const weatherBeforeKeyFailure = structuredClone(state.weather);
failPost = true;
await elements.weatherKeyForm.onsubmit({preventDefault() {}, target: elements.weatherKeyForm});
failPost = false;
assert.equal(lastPath, '/api/weather/key');
assert.equal(elements.weatherKey.value, '');
assert.equal(elements.weatherApiHost.value, 'draft.qweatherapi.com');
assert.equal(elements.weatherProvince.value, '2');
assert.equal(elements.weatherCity.value, '3');
assert.equal(elements.weatherEnabled.value, 'on');
assert.equal(elements.weatherKeySave.disabled, false);
assert.equal(elements.weatherKeyMessage.textContent, '保存失败');
assert.deepEqual(state.weather, weatherBeforeKeyFailure);
elements.weatherKey.value = 'weather-key-rotation';
const beforeRotatedWeatherKeyGets = getRequests.length;
await elements.weatherKeyForm.onsubmit({preventDefault() {}, target: elements.weatherKeyForm});
assert.deepEqual([...new URLSearchParams(lastBody)], [['api_key', 'weather-key-rotation']]);
assert.deepEqual(state.weather, weatherBeforeKeyFailure, 'key replacement preserves weather preferences');
assert.ok(getRequests.slice(beforeRotatedWeatherKeyGets).every(item => item.path === '/api/state'));
assert.equal(elements.weatherApiHost.value, 'draft.qweatherapi.com');
assert.equal(elements.weatherProvince.value, '2');
assert.equal(elements.weatherCity.value, '3');

state.local_network = true;
await vm.runInContext("load('session')", context);
for (const id of ['weatherEnabled', 'weatherApiHost', 'weatherProvince', 'weatherCity', 'weatherSave', 'weatherClear']) {
  assert.equal(elements[id].disabled, false, id + ' is editable on LAN');
}
assert.equal(elements.weatherKey.disabled, true);
assert.equal(elements.weatherKeySave.disabled, true);
assert.equal(elements.weatherKeyHint.hidden, false);
elements.weatherKey.value = 'never-send-weather-key-over-lan';
const beforeLanWeatherKey = writes.length;
await elements.weatherKeyForm.onsubmit({preventDefault() {}, target: elements.weatherKeyForm});
assert.equal(writes.length, beforeLanWeatherKey);
assert.equal(elements.weatherKey.value, 'never-send-weather-key-over-lan');
elements.weatherApiHost.value = 'lan.qweatherapi.com';
elements.weatherProvince.value = '1';
elements.weatherCity.value = '1';
confirmed = false;
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.equal(writes.length, beforeLanWeatherKey, 'initial enable requires consent');
confirmed = true;
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.equal(lastPath, '/api/weather/preferences');
assert.deepEqual([...new URLSearchParams(lastBody).keys()], ['enabled', 'api_host', 'province', 'city']);
assert.equal(state.weather.enabled, true);
assert.equal(state.weather.api_host, 'lan.qweatherapi.com');
assert.equal(elements.weatherKey.value, 'never-send-weather-key-over-lan');
const savedLanWeather = structuredClone(state.weather);
elements.weatherApiHost.value = 'failed.qweatherapi.com';
failPost = true;
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
failPost = false;
assert.deepEqual(state.weather, savedLanWeather);
assert.equal(elements.weatherApiHost.value, 'failed.qweatherapi.com');
assert.equal(elements.weatherMessage.textContent, '保存失败');
assert.equal(elements.weatherSave.disabled, false);

// An expired token blocks mutation. Authorizing again only reads session
// status; it must not replay weather writes or replace the pending draft.
unauthorized = true;
const weatherWritesBefore401 = writes.filter(item => item.path === '/api/weather/preferences').length;
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.deepEqual(state.weather, savedLanWeather);
assert.equal(writes.filter(item => item.path === '/api/weather/preferences').length, weatherWritesBefore401 + 1);
assert.match(elements.weatherMessage.textContent, /重新授权/);
assert.equal(elements.weatherSave.disabled, true);
elements.pairCode.value = 'abc12345';
await elements.pairForm.onsubmit({preventDefault() {}});
assert.equal(writes.filter(item => item.path === '/api/weather/preferences').length, weatherWritesBefore401 + 1);
assert.equal(elements.weatherApiHost.value, 'failed.qweatherapi.com');
assert.equal(elements.weatherKey.value, 'never-send-weather-key-over-lan');
assert.equal(elements.weatherSave.disabled, false);

const beforeCancelledWeatherClear = writes.length;
confirmed = false;
await elements.weatherClear.onclick();
assert.equal(writes.length, beforeCancelledWeatherClear);
assert.match(lastConfirmation, /未保存的天气设置也会清空/);
assert.deepEqual(state.weather, savedLanWeather);
confirmed = true;
failPost = true;
await elements.weatherClear.onclick();
failPost = false;
assert.deepEqual(state.weather, savedLanWeather);
assert.equal(elements.weatherApiHost.value, 'failed.qweatherapi.com');
assert.equal(elements.weatherKey.value, 'never-send-weather-key-over-lan');
await elements.weatherClear.onclick();
assert.equal(lastPath, '/api/weather/clear');
assert.equal(lastBody, 'confirm=CLEAR_WEATHER');
assert.equal(state.weather.key_saved, false);
assert.equal(state.weather.enabled, false);
assert.equal(elements.weatherEnabled.value, 'off');
assert.equal(elements.weatherApiHost.value, '');
assert.equal(elements.weatherProvince.value, '');
assert.equal(elements.weatherCity.value, '');
assert.equal(elements.weatherKey.value, '');
assert.equal(elements.weatherCredentials.open, true);
assert.equal(elements.weatherKeySave.disabled, true);

// Disabled weather may save partial non-secret setup even before the key is
// provisioned. Enabling it still checks the separately saved key, not a draft.
elements.weatherApiHost.value = 'partial.qweatherapi.com';
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.equal(lastPath, '/api/weather/preferences');
assert.equal(state.weather.enabled, false);
assert.equal(state.weather.key_saved, false);
assert.equal(state.weather.api_host, 'partial.qweatherapi.com');
assert.equal(new URLSearchParams(lastBody).get('province'), '0');
assert.equal(new URLSearchParams(lastBody).get('city'), '0');
elements.weatherEnabled.value = 'on';
elements.weatherProvince.value = '1';
elements.weatherCity.value = '1';
const beforeUnkeyedEnable = writes.length;
await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
assert.equal(writes.length, beforeUnkeyedEnable);
assert.match(elements.weatherMessage.textContent, /先连接设备热点.*保存 API Key/);
state.local_network = false;
await vm.runInContext("load('session')", context);

// Failed province/city reads must not leave half-loaded settings submittable.
// Saving or re-reading only the API Key cannot bypass the settings-ready gate.
for (const failedRegionPath of ['/api/weather/regions', '/api/weather/regions?province=1']) {
  state.weather.province_id = 1;
  state.weather.city_id = 1;
  failedGets.set(failedRegionPath, '地区列表读取失败');
  elements.weatherKey.value = 'keep-key-on-region-failure';
  await vm.runInContext("load('weatherPreferences')", context);
  assert.equal(elements.weatherSave.disabled, true);
  assert.equal(elements.weatherKeySave.disabled, false, 'directory failure does not prevent provisioning the key');
  assert.equal(elements.weatherKey.value, 'keep-key-on-region-failure');
  const beforeIncompleteWeatherSave = writes.length;
  await elements.weatherForm.onsubmit({preventDefault() {}, target: elements.weatherForm});
  assert.equal(writes.length, beforeIncompleteWeatherSave);
  const beforeKeyWhileRegionsFailGets = getRequests.length;
  await elements.weatherKeyForm.onsubmit({preventDefault() {}, target: elements.weatherKeyForm});
  assert.equal(lastPath, '/api/weather/key');
  assert.equal(elements.weatherSave.disabled, true, 'key save/reload cannot unlock incomplete preferences');
  assert.ok(getRequests.slice(beforeKeyWhileRegionsFailGets).every(item => item.path === '/api/state'));
  elements.weatherKey.value = 'retain-after-region-retry';
  failedGets.delete(failedRegionPath);
  const beforeRegionRetryWrites = writes.length;
  await elements.retryLoads.onclick();
  assert.equal(writes.length, beforeRegionRetryWrites, 'retry only fetches the failed directory/settings');
  assert.equal(elements.weatherSave.disabled, false);
  assert.equal(elements.weatherKey.value, 'retain-after-region-retry');
}
elements.weatherApiHost.value = 'keep-city-retry.qweatherapi.com';
elements.weatherProvince.value = '1';
failedGets.set('/api/weather/regions?province=1', '城市列表读取失败');
await elements.weatherProvince.onchange();
assert.equal(elements.weatherSave.disabled, true);
assert.equal(elements.weatherKeySave.disabled, false);
await vm.runInContext("load('weatherKey')", context);
assert.equal(elements.weatherSave.disabled, true, 'key status read also cannot unlock a failed city selection');
assert.equal(elements.weatherApiHost.value, 'keep-city-retry.qweatherapi.com');
failedGets.delete('/api/weather/regions?province=1');
await elements.retryLoads.onclick();
assert.equal(elements.weatherSave.disabled, false);
assert.equal(elements.weatherApiHost.value, 'keep-city-retry.qweatherapi.com');

// Countdown configuration saves only a custom option, on AP or authorized
// LAN. Independent drafts survive reads, errors and reauthorization.
await vm.runInContext("load('timer')", context);
elements.timerMinutes.value = '45';
elements.volume.value = 57;
elements.weatherKey.value = 'keep-timer-test-secret';
elements.navNetwork.onclick(); elements.navGeneral.onclick();
await vm.runInContext("load('settings')", context);
assert.equal(elements.timerMinutes.value, '45');
elements.volume.value = 57;
const timerWritesBefore = writes.length;
let finishTimerPost;
pendingPost = new Promise(resolve => { finishTimerPost = resolve; });
const timerSubmission = elements.timerForm.onsubmit({preventDefault() {}, target: elements.timerForm});
assert.equal(lastBody, 'section=timer&timer_minutes=45');
assert.equal(elements.timerSave.disabled, true);
assert.equal(elements.timerMinutes.disabled, true);
assert.equal(elements.volume.disabled, false);
assert.equal(elements.pagesSave.disabled, false);
assert.equal(elements.defaults.disabled, true, 'defaults cannot race a duration save');
finishTimerPost(); await timerSubmission; pendingPost = null;
assert.equal(state.timer_minutes, 45);
assert.equal(elements.timerMinutes.value, 45);
assert.equal(elements.volume.value, 57);
assert.equal(elements.weatherKey.value, 'keep-timer-test-secret');
assert.ok(writes.slice(timerWritesBefore).every(item => item.path === '/api/settings'), 'duration never starts a timer remotely');
for (const invalid of ['', '0', '181', '-1', '1.5', '1e2', 'NaN']) {
  const before = writes.length;
  elements.timerMinutes.value = invalid;
  await elements.timerForm.onsubmit({preventDefault() {}, target: elements.timerForm});
  assert.equal(writes.length, before, 'invalid duration remains local: ' + invalid);
  assert.match(elements.timerMessage.textContent, /1—180/);
}
state.local_network = true;
await vm.runInContext("load('session')", context);
for (const value of ['1', '180']) {
  elements.timerMinutes.value = value;
  await elements.timerForm.onsubmit({preventDefault() {}, target: elements.timerForm});
  assert.equal(lastBody, 'section=timer&timer_minutes=' + value);
  assert.equal(state.timer_minutes, Number(value));
}
elements.timerMinutes.value = '26'; failPost = true;
await elements.timerForm.onsubmit({preventDefault() {}, target: elements.timerForm});
failPost = false;
assert.equal(state.timer_minutes, 180);
assert.equal(elements.timerMinutes.value, '26');
assert.equal(elements.timerSave.disabled, false);
assert.equal(elements.timerMessage.textContent, '保存失败');
hangPost = true;
const timerBeforeTimeout = writes.length;
const timerTimeout = elements.timerForm.onsubmit({preventDefault() {}, target: elements.timerForm});
expireRequest(45000); await timerTimeout;
assert.match(elements.timerMessage.textContent, /结果尚未确认/);
assert.equal(elements.timerMinutes.value, '26');
assert.match(elements.loadMessage.textContent, /计时设置/);
await elements.retryLoads.onclick();
assert.equal(writes.length, timerBeforeTimeout + 1, 'verification never replays the timer save');
assert.equal(elements.timerMinutes.value, 180);
assert.equal(elements.volume.value, 57);
elements.timerMinutes.value = '27'; unauthorized = true;
await elements.timerForm.onsubmit({preventDefault() {}, target: elements.timerForm});
const timerUnauthorizedWrites = writes.length;
assert.equal(elements.timerSave.disabled, true);
assert.equal(elements.timerMinutes.value, '27');
elements.pairCode.value = 'abc12345';
await elements.pairForm.onsubmit({preventDefault() {}});
assert.equal(writes.length, timerUnauthorizedWrites + 1, 'only the explicit pair request is sent');
assert.equal(elements.timerMinutes.value, '27');
assert.equal(elements.timerSave.disabled, false);
for (const invalid of [undefined, 0, 181, 1.5, '25', null]) {
  state.timer_minutes = invalid;
  await vm.runInContext("load('timer')", context);
  assert.equal(elements.timerSave.disabled, true, 'invalid duration state blocks the form');
  assert.equal(elements.timerMinutes.value, '27', 'bad reads keep the draft');
  const before = writes.length;
  await elements.timerForm.onsubmit({preventDefault() {}, target: elements.timerForm});
  assert.equal(writes.length, before);
}
state.timer_minutes = 30;
await elements.retryLoads.onclick();
assert.equal(elements.timerMinutes.value, 30);
assert.equal(elements.timerSave.disabled, false);
state.local_network = false;
await vm.runInContext("load('session')", context);
elements.timerMinutes.value = '44';
await elements.defaults.onclick();
assert.equal(state.timer_minutes, 5);
assert.equal(elements.timerMinutes.value, 5, 'defaults explicitly clears custom duration drafts');
assert.equal(elements.weatherKey.value, 'keep-timer-test-secret');
assert.match(lastConfirmation, /当前选中时长、正在进行的计时.*不变/);
state.daily_pages_mask = 64;
await vm.runInContext("load('pages')", context);
assert.equal(elements.pageTimer.checked, true);
assert.ok(pageIds.filter(id => id !== 'pageTimer').every(id => !elements[id].checked));
elements.pageTimerUp.onclick();
assert.deepEqual(orderedBits(), [1,128,2,64,4,8,16,32]);
await elements.pagesForm.onsubmit({preventDefault() {}, target: elements.pagesForm});
assert.equal(state.daily_pages_mask, 64);
assert.deepEqual(state.daily_pages_order, [1,128,2,64,4,8,16,32]);

// Environment history is lazy and read-only. Missing media cannot disable the
// regular forms, the RAM chart, or CSV export; enabling writes requires consent.
assert.equal(getRequests.some(item => item.path === '/api/environment'), false);
const historyWritesBefore = writes.length;
elements.environmentSection.open = true;
elements.environmentSection.ontoggle();
await new Promise(resolve => setImmediate(resolve));
assert.match(elements.environmentSummary.textContent, /2 组有效记录/);
assert.match(elements.environmentRange.textContent, /本次开机开始记录/);
assert.equal(writes.length, historyWritesBefore);
elements.timerMinutes.value = '27';
elements.environmentSaveEnabled.value = 'on';
confirmed = false;
await elements.environmentForm.onsubmit({preventDefault(){},target:elements.environmentForm});
assert.equal(writes.length, historyWritesBefore, 'canceling SD consent performs no write');
confirmed = true;
await elements.environmentForm.onsubmit({preventDefault(){},target:elements.environmentForm});
assert.equal(state.environment_history_save_enabled, true);
assert.equal(lastBody, 'section=environment&environment_history_save_enabled=on&environment_archive_save_enabled=off');
assert.equal(elements.timerMinutes.value, '27', 'history save leaves timer draft alone');
state.sd_state = 'missing'; state.environment_card_available = false; environment.card_available = false;
await vm.runInContext("load('environment')", context);
assert.equal(elements.environmentSave.disabled, false, 'existing SD consent may be disabled without media');
elements.environmentSaveEnabled.value = 'off';
await elements.environmentForm.onsubmit({preventDefault(){},target:elements.environmentForm});
assert.equal(state.environment_history_save_enabled, false);
assert.equal(elements.environmentSave.disabled, true, 'no media cannot newly enable writes');
assert.equal(elements.volume.disabled, false);
assert.equal(elements.environmentCsv.disabled, false);
const beforeCsv = writes.length;
await elements.environmentCsv.onclick();
assert.equal(writes.length, beforeCsv, 'CSV download is a GET only');
assert.equal(getRequests.at(-1).path, '/api/environment.csv');
assert.match(elements.environmentMessage.textContent, /不会写入 SD/);
for (const [id,timer] of timers) if (timer.ms===1000) { timer.callback(); timers.delete(id); }
const validPoints = environment.points;
environment.points = [{age:0,valid:true,restored:false,t:'<svg>',h:4000,failures:0}];
await elements.environmentRefresh.onclick();
assert.match(elements.environmentMessage.textContent, /格式无效/);
for (const invalid of [undefined, null, 0, 'false']) {
  environment.points = [{...validPoints[0],restored:invalid}];
  await elements.environmentRefresh.onclick();
  assert.match(elements.environmentMessage.textContent, /格式无效/, 'restored is a required boolean');
}
environment.points = validPoints; environment.restored = true;
await elements.environmentRefresh.onclick();
assert.match(elements.environmentRange.textContent, /SD 卡恢复/);
state.sd_state = 'ready'; state.environment_card_available = true; environment.card_available = true;
await vm.runInContext("load('environment')", context);
state.sd_state = 'full';
await vm.runInContext("load('environment')", context);
assert.equal(elements.environmentSave.disabled,false,'image catalog limits do not deny a usable history card');
state.sd_state = 'ready';

// Long retention is distinct consent, never inferred from existing snapshot
// settings; file reads are lazy, bounded, authenticated GETs only.
assert.equal(getRequests.some(item=>item.path.startsWith('/api/environment/archive')),false);
state.environment_history_save_enabled=true;
state.environment_archive_save_enabled=false;
await vm.runInContext("load('environment')",context);
assert.equal(elements.environmentSaveEnabled.value,'on');
elements.environmentSaveEnabled.value='archive';
const beforeArchiveConsent=writes.length;
confirmed=false;
await elements.environmentForm.onsubmit({preventDefault(){}});
assert.equal(writes.length,beforeArchiveConsent);
assert.match(lastConfirmation,/持续占用 SD 空间/);
confirmed=true;
await elements.environmentForm.onsubmit({preventDefault(){}});
assert.equal(state.environment_archive_save_enabled,true);
assert.equal(lastBody,'section=environment&environment_history_save_enabled=on&environment_archive_save_enabled=on');
assert.equal(elements.timerMinutes.value,'27','archive consent leaves unrelated drafts alone');
environment.archive_enabled=true;environment.archive_full=true;
await elements.environmentRefresh.onclick();
assert.match(elements.environmentStorage.textContent,/长期历史已暂停.*安全限制/);
environment.storage_full=true;environment.storage_error=true;environment.archive_enabled=false;
await elements.environmentRefresh.onclick();
assert.match(elements.environmentStorage.textContent,/SD 可用空间不足.*暂停保存.*自动重试/,'snapshot-only writes expose the same full-card condition');
assert.doesNotMatch(elements.environmentStorage.textContent,/最近一次保存失败/,'the actionable no-space reason takes priority over generic failure');
environment.storage_full=false;environment.storage_error=false;environment.archive_enabled=true;
environment.archive_full=false;environment.archive_error=true;
await elements.environmentRefresh.onclick();
assert.match(elements.environmentStorage.textContent,/长期历史写入失败/);
environment.archive_error=false;
elements.environmentArchiveSection.open=true;
const archiveWrites=writes.length;
elements.environmentArchiveSection.ontoggle();
await new Promise(resolve=>setImmediate(resolve));
assert.equal(writes.length,archiveWrites);
assert.equal(getRequests.at(-1).path,'/api/environment/archives');
assert.equal(elements.environmentArchiveList.children.length,2);
assert.equal(elements.environmentArchiveMore.hidden,false);
assert.match(elements.environmentArchiveList.children[0].children[0].textContent,/2026-10.*MB.*下载 CSV/);
const beforeArchiveDownload=downloads;
await elements.environmentArchiveList.children[0].children[0].onclick();
assert.equal(downloads,beforeArchiveDownload+1);
assert.equal(getRequests.at(-1).path,'/api/environment/archive.csv?month=2026-10');
assert.ok(getRequests.at(-1).options.headers['X-RLCD-Token']);
assert.equal(writes.length,archiveWrites);
for(const [type,bytes,text] of [
  ['text/html',undefined,'<html>error</html>\n'],
  ['text/csv',1,archiveText],
  ['text/csv',undefined,'unterminated'],
  ['text/csv',8388609,archiveText],
  ['text/csv',0,''],
]) {
  const savedText=archiveText;
  archiveType=type;archiveBytes=bytes;archiveText=text;
  const previous=downloads;
  await context.downloadEnvironmentArchive('2026-10');
  assert.equal(downloads,previous,'bad CSV response is never saved as a download');
  assert.match(elements.environmentArchiveMessage.textContent,/未生成下载/);
  archiveText=savedText;
}
archiveType='text/csv; charset=utf-8';archiveBytes=undefined;
failedGets.set('/api/environment/archive.csv?month=2026-10','SD 卡正在使用，请先停止音乐播放。');
await context.downloadEnvironmentArchive('2026-10');
assert.match(elements.environmentArchiveMessage.textContent,/停止音乐/);
failedGets.delete('/api/environment/archive.csv?month=2026-10');
const beforeInvalidMonth=getRequests.length;
for(const month of ['../2026-10','2026-13','2026-01&x=1','2026-1','1999-12','2101-01','<svg>']) {
  await context.downloadEnvironmentArchive(month);
}
assert.equal(getRequests.length,beforeInvalidMonth,'invalid month cannot become a URL');
const savedPage=archivePage;
archivePage={files:[{month:'2026-08',bytes:100}],next_cursor:''};
await elements.environmentArchiveMore.onclick();
assert.equal(getRequests.at(-1).path,'/api/environment/archives?before=2026-09');
assert.equal(elements.environmentArchiveMore.hidden,true);
assert.match(elements.environmentArchiveList.children[0].children[0].textContent,/2026-08/);
for(const invalidPage of [
  {files:[{month:'<svg>',bytes:100}],next_cursor:''},
  {files:[{month:'2026-10',bytes:8388609}],next_cursor:''},
  {files:[{month:'2026-10',bytes:100},{month:'2026-10',bytes:100}],next_cursor:''},
  {files:[{month:'2026-09',bytes:100},{month:'2026-10',bytes:100}],next_cursor:''},
  {files:[{month:'2026-10',bytes:100}],next_cursor:'2026-11'},
  {files:[],next_cursor:'2026-13'},
]) {
  archivePage=invalidPage;
  await elements.environmentArchiveRefresh.onclick();
  assert.match(elements.environmentArchiveMessage.textContent,/列表无效/);
}
archivePage={files:[{month:'2026-10',bytes:100}],next_cursor:'2026-08'};
await elements.environmentArchiveRefresh.onclick();
assert.equal(elements.environmentArchiveMore.hidden,false,'cursor may skip a damaged month after the last returned file');
archivePage={files:[],next_cursor:'2026-05'};
await elements.environmentArchiveMore.onclick();
assert.equal(getRequests.at(-1).path,'/api/environment/archives?before=2026-08');
assert.equal(elements.environmentArchiveMore.hidden,false,'a full scanned page can contain no valid files');
assert.match(elements.environmentArchiveMessage.textContent,/继续查看更早月份/);
for(const cursor of ['2026-05','2026-06']) {
  archivePage={files:[],next_cursor:cursor};
  await elements.environmentArchiveMore.onclick();
  assert.match(elements.environmentArchiveMessage.textContent,/列表无效/,'a next cursor must progress beyond the submitted cursor');
}
archivePage={files:[{month:'2026-04',bytes:100}],next_cursor:''};
await elements.environmentArchiveMore.onclick();
assert.equal(getRequests.at(-1).path,'/api/environment/archives?before=2026-05');
assert.match(elements.environmentArchiveList.children[0].children[0].textContent,/2026-04/);
archivePage=savedPage;
hangRead=true;
const cancelledDownload=context.downloadEnvironmentArchive('2026-10');
await new Promise(resolve=>setImmediate(resolve));
assert.equal(elements.environmentArchiveCancel.hidden,false);
elements.environmentArchiveCancel.onclick();
await cancelledDownload;
assert.equal(elements.environmentArchiveCancel.hidden,true);
assert.match(elements.environmentArchiveMessage.textContent,/取消或超时/);
elements.environmentSaveEnabled.value='off';
await elements.environmentForm.onsubmit({preventDefault(){}});
assert.equal(state.environment_archive_save_enabled,false);
await elements.environmentArchiveRefresh.onclick();
assert.equal(elements.environmentArchiveList.children.length,2,'turning writes off retains archive downloads');
state.environment_card_available=false;
await vm.runInContext("load('environment')",context);
assert.equal(elements.environmentArchiveList.children.length,0);
assert.equal(elements.environmentArchiveRefresh.disabled,true);
assert.equal(elements.environmentCsv.disabled,false,'RAM CSV remains available without media');
state.environment_card_available=true;
await vm.runInContext("load('environment')",context);
environment.archive_enabled=false;

// Exercise actual canvas path commands, not a copied continuity predicate.
// A restart shorter than one bucket must still leave the chart disconnected.
const historyCanvas = {
  paths:[], current:[], labels:[], clearRect(){this.paths=[];this.labels=[];}, fillText(text){this.labels.push(text);}, fillRect(){},
  beginPath(){this.current=[];},moveTo(x,y){this.current.push(['move',x,y]);},
  lineTo(x,y){this.current.push(['line',x,y]);},stroke(){this.paths.push([...this.current]);},
};
elements.environmentTemperature.getContext=()=>historyCanvas;
const chartPoint=(age,restored=false,extra={})=>({age,restored,valid:true,t:2300,h:4500,failures:0,...extra});
const chartCommands=points=>{
  context.environmentPlot('environmentTemperature',points,'t','温度 °C');
  assert.equal(historyCanvas.paths.length,2,'axes and trace are separate paths');
  return historyCanvas.paths[1].map(command=>command[0]);
};
assert.deepEqual(chartCommands([chartPoint(900),chartPoint(600),chartPoint(300)]),['move','line','line']);
assert.deepEqual(chartCommands([chartPoint(900,true),chartPoint(600,true),chartPoint(300,false)]),['move','line','move'],'boot boundary breaks an otherwise consecutive sequence');
assert.deepEqual(chartCommands([chartPoint(900),chartPoint(300)]),['move','move'],'missing bucket breaks line');
assert.deepEqual(chartCommands([chartPoint(900),chartPoint(750)]),['move','move'],'partial spacing cannot appear continuous');
assert.deepEqual(chartCommands([chartPoint(300),chartPoint(600)]),['move','move'],'reverse chronology never connects');
assert.deepEqual(chartCommands([chartPoint(900),chartPoint(600,false,{valid:false}),chartPoint(300)]),['move','move']);
assert.deepEqual(chartCommands([chartPoint(900),chartPoint(600,false,{failures:1}),chartPoint(300)]),['move','move','move'],'failed bucket breaks both adjacent segments');
assert.deepEqual(chartCommands([chartPoint(86400),chartPoint(86100)]),['move'],'the expired 24h boundary is not drawn');
const savedEnvironment=structuredClone(environment);
environment.recording_age=600;environment.restored=false;
environment.points=[chartPoint(600),chartPoint(300),chartPoint(0)];
await elements.environmentRefresh.onclick();
assert.equal(historyCanvas.paths[1][0][1],40,'new session starts at the left edge');
assert.ok(historyCanvas.paths[1].at(-1)[1]<44,'ten minutes does not fill a 24h graph');
assert.ok(historyCanvas.labels.includes('开始记录'));
assert.ok(historyCanvas.labels.includes('+24 小时'));
environment.recording_age=86400;
await elements.environmentRefresh.onclick();
assert.equal(historyCanvas.paths[1].at(-1)[1],468,'a full day uses the rolling right edge');
assert.ok(historyCanvas.labels.includes('24 小时前'));
environment.recording_age=600;environment.restored=true;
environment.points=[chartPoint(3600,true),chartPoint(300),chartPoint(0)];
await elements.environmentRefresh.onclick();
assert.equal(historyCanvas.paths[1][0][1],40,'recovered oldest valid history anchors the left');
assert.ok(historyCanvas.paths[1].at(-1)[1]>57&&historyCanvas.paths[1].at(-1)[1]<58);
environment=savedEnvironment;
elements.environmentTemperature.getContext=()=>null;

// Backup selection and preview never upload. Only the confirmed ordinary
// restore is posted; unsubmitted drafts and all excluded fields survive.
const backupWritesBefore = writes.length;
const selectBackup = async value => {
  const text=typeof value==='string'?value:JSON.stringify(value);
  elements.backupFile.files=[{size:new TextEncoder().encode(text).length,text:async()=>text}];
  await elements.backupFile.onchange();
};
await selectBackup(backupData);
assert.equal(elements.backupPreview.hidden, false);
assert.match(elements.backupSummary.textContent, /环境趋势/);
assert.match(elements.backupSummary.textContent, /10 分钟/);
assert.equal(writes.length, backupWritesBefore);
for (const invalid of ['not JSON', {...backupData,api_key:'hidden'},
  {...backupData,version:2}, {...backupData,preferences:{...backupData.preferences,timer_minutes:181}},
  {...backupData,preferences:{...backupData.preferences,daily_pages_order:[1,1,2,4,64,8,16,32]}},
  {...backupData,preferences:{...backupData.preferences,temperature_unit:'<img src=x>'}}]) {
  await selectBackup(invalid);
  assert.equal(elements.backupPreview.hidden, true);
  assert.equal(elements.backupRestore.disabled, true);
}
await selectBackup(' '.repeat(4097));
assert.match(elements.backupMessage.textContent, /4 KiB/);
let resolveOldBackup;
elements.backupFile.files=[{size:100,text:()=>new Promise(resolve=>{resolveOldBackup=resolve;})}];
const oldSelection=elements.backupFile.onchange();
await selectBackup({...backupData,preferences:{...backupData.preferences,timer_minutes:15}});
resolveOldBackup(JSON.stringify(backupData));await oldSelection;
assert.match(elements.backupSummary.textContent, /15 分钟/, 'late file read cannot replace newer selection');
await selectBackup(backupData);
confirmed=false;await elements.backupRestore.onclick();
assert.equal(writes.length, backupWritesBefore);
confirmed=true;
state.local_network=true;state.updates='beta';state.environment_history_save_enabled=true;
await vm.runInContext("load('session')",context);
elements.volume.value=77;elements.settings.oninput();
elements.timerMinutes.value='37';elements.timerForm.oninput();
elements.weatherKey.value='preserve-weather-key';
elements.conversationKey.value='preserve-ai-key';
const historyDraft = elements.environmentSaveEnabled.value;
await elements.backupRestore.onclick();
assert.equal(lastPath,'/api/settings/restore');
assert.equal(state.volume,40);
assert.equal(elements.volume.value,77,'restore keeps existing ordinary form draft');
assert.equal(state.timer_minutes,10);
assert.equal(elements.timerMinutes.value,'37');
assert.match(elements.settingsMessage.textContent,/仍保留未保存/);
assert.equal(state.updates,'beta');
assert.equal(state.environment_history_save_enabled,true);
assert.equal(elements.environmentSaveEnabled.value,historyDraft);
assert.equal(elements.weatherKey.value,'preserve-weather-key');
assert.equal(elements.conversationKey.value,'preserve-ai-key');
assert.equal(elements.backupPreview.hidden,true);
await selectBackup(backupData);
failPost=true;await elements.backupRestore.onclick();failPost=false;
assert.equal(elements.backupPreview.hidden,false,'definite failure keeps preview for correction or retry');
disconnectedPost=true;await elements.backupRestore.onclick();disconnectedPost=false;
assert.equal(elements.backupPreview.hidden,true,'uncertain result cannot be blindly resubmitted');
assert.equal(elements.backupRestore.disabled,true);
assert.match(elements.backupMessage.textContent,/下载当前偏好备份核对/);
await elements.backupExport.onclick();
assert.equal(getRequests.at(-1).path,'/api/settings/backup');
for (const [id,timer] of timers) if(timer.ms===1000){timer.callback();timers.delete(id);}
state.local_network=false;state.environment_history_save_enabled=false;
await vm.runInContext("load('session')",context);

// C-side trust boundary: recovery never registers these endpoints, all reads
// are authorized, and mutation re-parses against the latest locked settings.
for(const name of ['backup_uri','restore_uri','environment_uri','environment_csv_uri','environment_archives_uri','environment_archive_csv_uri']) {
  assert.match(serverDefinition,new RegExp('if \\(error == ESP_OK && !recovery_mode\\) \\{\\s*error = httpd_register_uri_handler\\(s_http_server, &'+name+'\\);'));
}
const ordinaryRead=source.split('static bool ordinary_read_allowed(')[1].split('static esp_err_t settings_backup_get_handler')[0];
assert.match(ordinaryRead,/authorize_get\(request\)/);
assert.match(ordinaryRead,/boot_recovery_is_active\(\)/);
const restoreHandler=source.split('static esp_err_t settings_restore_post_handler(')[1].split('static esp_err_t environment_get_handler')[0];
assert.match(restoreHandler,/authorize_post\(request\)/);
assert.match(restoreHandler,/RESTORE_PREFERENCES/);
assert.match(restoreHandler,/content_len > APP_SETTINGS_BACKUP_MAX_LENGTH/);
assert.ok(restoreHandler.indexOf('settings_backup_parse(')<restoreHandler.indexOf('begin_regular_mutation()'));
assert.match(restoreHandler,/app_settings_import_backup\(body, length, &candidate\)/);
assert.doesNotMatch(restoreHandler,/esp_restart|network_time|sd_image|app_settings_save\(/);
const environmentHandler=source.split('static esp_err_t environment_get_handler(')[1].split('static esp_err_t finish_reprovisioning_request')[0];
assert.match(environmentHandler,/ordinary_read_allowed\(request\)/);
assert.match(environmentHandler,/heap_caps_malloc\(/);
assert.match(environmentHandler,/environment_history_snapshot\(snapshot\)/);
assert.match(environmentHandler,/environment_history_csv_row\(/);
assert.match(environmentHandler,/point->restored \? "true" : "false"/);
assert.match(environmentHandler,/httpd_resp_send_chunk/);
assert.doesNotMatch(environmentHandler,/environment_history_record|set_storage_enabled|sd_image|app_settings_save|begin_regular_mutation/);
const settingsHandler=source.split('static esp_err_t settings_post_handler(')[1].split('static esp_err_t market_config_post_handler')[0];
assert.match(settingsHandler,/app_settings_save_form\(body, length, &settings\)/);
assert.match(settingsHandler,/ENABLE_HISTORY/);
assert.match(settingsHandler,/ENABLE_ARCHIVE/);
assert.match(settingsHandler,/!current\.environment_archive_save_enabled/);
const archiveHandler=source.split('static esp_err_t environment_archive_get_handler(')[1].split('static esp_err_t finish_reprovisioning_request')[0];
assert.match(archiveHandler,/ordinary_read_allowed\(request\)/);
assert.match(archiveHandler,/sd_media_begin_read\(&lease, 0U\)/);
assert.match(archiveHandler,/environment_archive_inspect\(/);
assert.match(archiveHandler,/environment_archive_stream\(/);
assert.match(archiveHandler,/state\.sent != size/);
assert.match(archiveHandler,/X-RLCD-CSV-Bytes/);
assert.match(archiveHandler,/sd_media_end_read\(lease\)/);
assert.doesNotMatch(archiveHandler,/audio_music_stop|begin_storage_change|begin_regular_mutation|app_settings_save|environment_archive_append/);
const archiveQuery=source.split('static bool environment_archive_request_month(')[1].split('static esp_err_t environment_archive_error_response')[0];
assert.match(archiveQuery,/length != prefix_length \+ 7U/);
assert.match(archiveQuery,/memcmp\(query, prefix, prefix_length\)/);
assert.match(archiveQuery,/environment_archive_valid_month\(month\)/);
const archiveCheckpoint=source.split('static bool environment_archive_request_checkpoint(')[1].split('static bool environment_archive_response_write')[0];
assert.match(archiveCheckpoint,/pdMS_TO_TICKS\(20000U\)/);
assert.match(archiveCheckpoint,/session_deadline_expired\(\)/);
assert.match(archiveCheckpoint,/!portal_is_ready\(\)/,'physical portal cancellation also ends a read');
assert.doesNotMatch(source,/restore_previous_settings/);
assert.doesNotMatch(recoveryMarkup,/backupFile|environmentSection|environment_history_save_enabled/);

// Pure geometry runs the firmware functions, including extreme aspect ratios,
// contain compatibility and source-bound clamping in every drag direction.
assert.match(html, /<option value="contain" selected>完整显示/);
assert.match(html, /<option value="cover">铺满裁剪/);
assert.match(html, /#imageCanvas\[data-fit=cover\]\{touch-action:none/);
const geometry = (width, height, mode, focus) => context.imageGeometry(width, height, mode, focus);
const plain = value => JSON.parse(JSON.stringify(value));
assert.deepEqual(plain(geometry(800, 500, 'cover')), {
  sx: 0, sy: 0, sw: 800, sh: 500, dx: 0, dy: 0, dw: 400, dh: 250, scale: .5, maxX: 0, maxY: 0,
});
assert.deepEqual(plain(geometry(400, 800, 'contain')), {
  sx: 0, sy: 0, sw: 400, sh: 800, dx: 137, dy: 0, dw: 125, dh: 250, scale: .3125, maxX: 0, maxY: 0,
});
assert.equal(geometry(1000, 250, 'cover').sx, 300);
assert.equal(geometry(400, 1000, 'cover').sy, 375);
for (const [width, height] of [[400,250],[800,600],[300,800],[1,40000000],[40000000,1],[1,1],[777,333]]) {
  const contain = geometry(width, height, 'contain');
  assert.equal(contain.sw, width); assert.equal(contain.sh, height);
  assert.ok(contain.dx >= 0 && contain.dy >= 0 && contain.dx + contain.dw <= 400 && contain.dy + contain.dh <= 250);
  for (const x of [-100, 0, .25, .5, 1, 100, NaN]) for (const y of [-100, 0, .5, 1, 100, NaN]) {
    const crop = geometry(width, height, 'cover', {x, y});
    assert.ok(crop.sx >= 0 && crop.sy >= 0);
    assert.ok(crop.sx + crop.sw <= width + 1e-8 && crop.sy + crop.sh <= height + 1e-8);
    assert.equal(crop.dw, 400); assert.equal(crop.dh, 250);
    assert.ok(Math.abs(crop.sw / crop.sh - 1.6) < 1e-9);
    for (const delta of [-1e10, 0, 1e10]) {
      const focus = context.imagePan(crop, delta, -delta);
      assert.ok(focus.x >= 0 && focus.x <= 1 && focus.y >= 0 && focus.y <= 1);
      const moved = geometry(width, height, 'cover', focus);
      assert.ok(moved.sx + moved.sw <= width + 1e-8 && moved.sy + moved.sh <= height + 1e-8);
    }
  }
}
for (const [width, height] of [[0, 10], [-1, 20], [NaN, 1], [1, Infinity]]) {
  assert.throws(() => geometry(width, height, 'cover'), /尺寸/);
}
assert.equal(context.imagePan(geometry(1000,250,'cover'), 100, 0).x, 1 / 3, 'drag right moves image right / source window left');

// A tiny host canvas is sufficient to verify binary packing and reserved rows.
// Browser decoding stays mocked: these tests make no network or image upload.
const canvas = {
  pixels: new Uint8ClampedArray(400 * 300 * 4), draws: [], fail: false,
  fillRect() { this.pixels.fill(0); for (let i = 3; i < this.pixels.length; i += 4) this.pixels[i] = 255; },
  drawImage(bitmap, sx, sy, sw, sh, dx, dy, dw, dh) {
    if (this.fail) throw new Error('测试绘制失败');
    assert.equal(bitmap.closed || 0, 0, 'closed image resources are never reused');
    this.draws.push({bitmap, sx, sy, sw, sh, dx, dy, dw, dh});
    assert.ok(dy + dh <= 250, 'all source rendering leaves the footer untouched');
    for (let y = dy; y < dy + dh; y++) for (let x = dx; x < dx + dw; x++) {
      const value = bitmap.sample ? bitmap.sample(sx + (x-dx)*sw/dw, sy + (y-dy)*sh/dh) : 180;
      const offset = (y*400+x)*4;
      this.pixels[offset] = this.pixels[offset+1] = this.pixels[offset+2] = value;
    }
  },
  getImageData() { return {data: this.pixels.slice()}; },
  createImageData(width, height) { return {data: new Uint8ClampedArray(width*height*4)}; },
  putImageData(preview) { this.preview = preview; },
};
elements.imageCanvas.getContext = () => canvas;
elements.threshold.value = '128'; elements.dither.checked = false;
const bitmap = (width, height, sample) => ({width, height, sample, closed: 0, close() { ++this.closed; }});
context.createImageBitmap = async (file, options) => {
  assert.equal(options.imageOrientation, 'from-image');
  return file.promise || file.bitmap;
};
context.window.createImageBitmap = context.createImageBitmap;
const selectImage = file => {
  elements.imageFile.files = file ? [file] : [];
  elements.imageFile.value = file?.name || '';
  return elements.imageFile.onchange({target: elements.imageFile});
};
const fileFor = (source, name='test.png') => ({name, type:'image/png', size:100, bitmap:source});
const flushFrames = () => {
  for (const [id, callback] of [...frames]) { frames.delete(id); callback(); }
};
const currentPbm = async () => new Uint8Array(await vm.runInContext('imagePbm', context).arrayBuffer());
const imageWritesBefore = writes.length, imageReadsBefore = getRequests.length;
request = null;
const firstBitmap = bitmap(1000, 250, x => x % 256);
await selectImage(fileFor(firstBitmap));
assert.equal(elements.imagePreview.hidden, false);
assert.equal(elements.imageFit.value, 'contain');
assert.equal(elements.imageUpload.disabled, false);
assert.equal(firstBitmap.closed, 0, 'current source is retained for recropping');
const containBytes = await currentPbm();
assert.equal(containBytes.length, 15011);
assert.equal(new TextDecoder().decode(containBytes.slice(0, 11)), 'P4\n400 300\n');
assert.ok(containBytes.slice(11+50*250).every(value => value === 255), '50 reserved rows are packed black');
elements.imageFit.value = 'cover'; elements.imageFit.onchange();
assert.equal(elements.imageUpload.disabled, true, 'pending geometry cannot upload an obsolete preview');
flushFrames();
assert.equal(elements.imageCanvas.dataset.fit, 'cover');
assert.equal(elements.imageCenter.hidden, false);
assert.equal(canvas.draws.at(-1).sx, 300);
assert.notDeepEqual(await currentPbm(), containBytes);
const pointer = (overrides={}) => ({pointerId:1, button:0, clientX:100, clientY:50, preventDefault() {}, ...overrides});
elements.imageCanvas.onpointerdown(pointer({clientY:140}));
assert.equal(elements.imageCanvas.pointerCaptures.size, 0, 'footer cannot initiate a crop');
elements.imageCanvas.onpointerdown(pointer({button:2}));
assert.equal(elements.imageCanvas.pointerCaptures.size, 0, 'secondary button does not pan');
elements.imageCanvas.onpointerdown(pointer({pointerType:'touch'}));
assert.equal(elements.imageCanvas.hasPointerCapture(1), true);
elements.imageCanvas.onpointermove(pointer({pointerId:2, clientX:200}));
assert.equal(frames.size, 0, 'other touches cannot replace the active pointer');
elements.imageCanvas.onpointermove(pointer({clientX:200}));
elements.imageCanvas.onpointermove(pointer({clientX:250}));
assert.equal(frames.size, 1, 'drag frames are coalesced');
flushFrames();
assert.equal(canvas.draws.at(-1).sx, 0, 'CSS-scaled drag is converted to canvas units and clamped');
elements.imageCanvas.onpointermove(pointer({clientX:-1000})); flushFrames();
assert.equal(canvas.draws.at(-1).sx, 600);
elements.imageCanvas.onpointercancel(pointer());
assert.equal(elements.imageCanvas.pointerCaptures.size, 0);
elements.imageCenter.onclick(); flushFrames();
assert.equal(canvas.draws.at(-1).sx, 300);
elements.imageCanvas.onpointerdown(pointer());
elements.imageFit.value = 'contain'; elements.imageFit.onchange(); flushFrames();
assert.equal(elements.imageCanvas.pointerCaptures.size, 0, 'mode change releases an active drag');
elements.imageCanvas.onpointerdown(pointer());
assert.equal(elements.imageCanvas.pointerCaptures.size, 0, 'contain is not draggable');

// Compare exact PBM conversion with the pre-existing threshold/dither math.
for (const dither of [false, true]) for (const threshold of [1, 128, 254]) {
  elements.threshold.value = String(threshold); elements.dither.checked = dither;
  elements.threshold.oninput(); flushFrames();
  const gray = new Float32Array(vm.runInContext('imageGray', context)), expected = new Uint8Array(15000);
  expected.fill(255, 250*50);
  for (let y=0; y<250; y++) for (let x=0; x<400; x++) {
    const i=y*400+x, next=gray[i]>=threshold?255:0, error=gray[i]-next;
    if (!next) expected[i>>3] |= 1 << (7-(i&7));
    if (dither) {
      if (x+1<400) gray[i+1] += error*7/16;
      if (y+1<250) {
        if (x>0) gray[i+399] += error*3/16;
        gray[i+400] += error*5/16;
        if (x+1<400) gray[i+401] += error/16;
      }
    }
  }
  assert.deepEqual((await currentPbm()).slice(11), expected);
}
assert.equal(writes.length, imageWritesBefore);
assert.equal(getRequests.length, imageReadsBefore);
assert.equal(request, null, 'selection, crop and dither do not upload anything');
confirmed = false; elements.imageUpload.onclick();
assert.equal(request, null, 'a cancelled confirmation sends nothing');
confirmed = true; elements.imageUpload.onclick();
assert.equal(request.path, '/api/images/upload');
assert.equal(request.headers['Content-Type'], 'application/octet-stream');
assert.equal(request.headers['X-RLCD-Token'], state.token);
assert.equal(request.body.size, 15011, 'only the final PBM, never the source file, is sent');
assert.equal(request.body, vm.runInContext('imagePbm', context));
assert.equal(elements.imageFile.disabled, true, 'explicit upload locks file changes');
request.status = 409; request.responseText = '写入失败'; request.onload();
assert.equal(elements.imageFile.disabled, false);
assert.equal(firstBitmap.closed, 0, 'a failed upload preserves the local crop for inspection');
request = null;

// Rapid file changes and page teardown must close stale decoded resources,
// without allowing an older result or error to replace the newest selection.
let finishOld, finishNew;
const staleBitmap = bitmap(800,600), newBitmap = bitmap(300,900);
const oldPromise = selectImage({...fileFor(staleBitmap,'old.png'), promise:new Promise(resolve => { finishOld=resolve; })});
assert.equal(firstBitmap.closed, 1);
assert.equal(elements.imageFile.disabled, false, 'another selection is allowed while decoding');
assert.equal(elements.imageUpload.disabled, true);
const newPromise = selectImage({...fileFor(newBitmap,'new.png'), promise:new Promise(resolve => { finishNew=resolve; })});
finishNew(newBitmap); await newPromise;
const newPreview = await currentPbm();
finishOld(staleBitmap); await oldPromise;
assert.equal(staleBitmap.closed, 1);
assert.equal(newBitmap.closed, 0);
assert.deepEqual(await currentPbm(), newPreview);
assert.equal(elements.imageFile.value, 'new.png');
let finishInvalid;
const invalidBitmap = bitmap(40000001,1);
const invalidPromise = selectImage({...fileFor(invalidBitmap,'invalid.png'), promise:new Promise(resolve => { finishInvalid=resolve; })});
const newestBitmap = bitmap(400,250);
await selectImage(fileFor(newestBitmap,'newest.png'));
finishInvalid(invalidBitmap); await invalidPromise;
assert.equal(invalidBitmap.closed, 1);
assert.equal(elements.imageFile.value, 'newest.png', 'stale errors never clear the newest file');
assert.match(elements.imageMessage.textContent, /黑白预览已生成/);
canvas.fail = true;
const failedBitmap = bitmap(10,10);
await selectImage(fileFor(failedBitmap)); canvas.fail = false;
assert.equal(newestBitmap.closed, 1);
assert.equal(failedBitmap.closed, 1, 'raster failure frees its decoded source');
assert.equal(elements.imagePreview.hidden, true);
assert.equal(elements.imageUpload.disabled, true);
for (const file of [{...fileFor(bitmap(1,1)),size:0}, {...fileFor(bitmap(1,1)),size:32*1024*1024+1}, {name:'bad.gif',type:'image/gif',size:10}]) {
  await selectImage(file);
  assert.equal(elements.imagePreview.hidden, true);
  assert.equal(elements.imageUpload.disabled, true);
}
let finishClosed;
const closingBitmap = bitmap(800,500);
const closingPromise = selectImage({...fileFor(closingBitmap), promise:new Promise(resolve => { finishClosed=resolve; })});
imagePagehide(); finishClosed(closingBitmap); await closingPromise;
assert.equal(closingBitmap.closed, 1);
assert.equal(elements.imagePreview.hidden, true);
const visibleBitmap = bitmap(400,1000);
await selectImage(fileFor(visibleBitmap));
elements.imageFit.value = 'cover'; elements.imageFit.onchange(); flushFrames();
elements.imageCanvas.onpointerdown(pointer());
elements.imageCanvas.onpointermove(pointer({clientY:-1000})); flushFrames();
assert.equal(canvas.draws.at(-1).sy, 750, 'portrait drag clamps vertically');
elements.threshold.oninput(); imagePagehide();
assert.equal(visibleBitmap.closed, 1);
assert.equal(frames.size, 0);
assert.equal(elements.imageCanvas.pointerCaptures.size, 0);
imagePagehide(); assert.equal(visibleBitmap.closed, 1, 'cleanup is idempotent');

// The object-URL fallback follows the same ownership contract.
const revokedUrls = [], fallbackSources = [];
context.window.createImageBitmap = undefined;
context.URL = {createObjectURL() { return 'blob:local-' + fallbackSources.length; }, revokeObjectURL(url) { revokedUrls.push(url); }};
context.Image = class { constructor() { this.naturalWidth=400; this.naturalHeight=250; fallbackSources.push(this); } };
const fallbackPromise = selectImage(fileFor(null));
fallbackSources.at(-1).onload(); await fallbackPromise;
assert.equal(revokedUrls.length, 0);
await selectImage(null);
assert.deepEqual(revokedUrls, ['blob:local-0']);
assert.equal(fallbackSources[0].src, '');
const fallbackFailure = selectImage(fileFor(null));
fallbackSources.at(-1).onerror(); await fallbackFailure;
assert.deepEqual(revokedUrls, ['blob:local-0','blob:local-1']);
assert.equal(elements.imagePreview.hidden, true);
assert.equal(writes.length, imageWritesBefore);
assert.equal(request, null);

// An initial read failure leaves safe, non-submittable forms, then the same
// page can recover without a reload. Use separate elements and lexical state.
const freshElements = Object.fromEntries(ids.map(id => [id, new Element()]));
for (const [id, form] of Object.entries(forms)) freshElements[id].controls = Object.keys(form.names).map(name => freshElements[name]);
freshElements.alarmForm.controls.push(freshElements.alarmTime, freshElements.alarmPreview);
freshElements.timerForm.controls.push(freshElements.timerSave);
freshElements.pagesForm.controls.push(...pageIds.map(id => freshElements[id]), freshElements.pagesSave);
freshElements.wifiForm.hidden = true;
failedGets.set('/api/state', '暂时无法读取');
const freshContext = vm.createContext({...context, document: {...context.document,
  getElementById(id) { assert.ok(freshElements[id]); return freshElements[id]; },
}});
vm.runInContext(javascript, freshContext);
freshElements.musicSection.open = true;
const beforeInitialToggle = getRequests.length;
freshElements.musicSection.ontoggle();
assert.equal(getRequests.length, beforeInitialToggle, 'native initial open-toggle cannot start media before state loads');
await new Promise(resolve => setImmediate(resolve));
for (const id of ['volume','alarmVolume','timerMinutes','timerSave','updates','pagesSave','musicMode','conversationPreferencesSave','conversationSave','weatherSave','weatherKeySave','setTime','upload','wifiEdit']) assert.equal(freshElements[id].disabled, true, id + ' remains unavailable before initial state');
assert.equal(freshElements.loadNotice.hidden, false);
const beforeUnreadySave = writes.length;
await freshElements.settings.onsubmit({preventDefault() {}, target: freshElements.settings});
await freshElements.musicSettingsForm.onsubmit({preventDefault() {}, target: freshElements.musicSettingsForm});
await freshElements.timerForm.onsubmit({preventDefault() {}, target: freshElements.timerForm});
assert.equal(writes.length, beforeUnreadySave);
failedGets.delete('/api/state');
await freshElements.retryLoads.onclick();
assert.equal(freshElements.volume.disabled, false);
assert.equal(freshElements.musicMode.disabled, false);
assert.equal(freshElements.timerSave.disabled, false);
assert.equal(freshElements.conversationPreferencesSave.disabled, true, 'fresh unconfigured state still requires credentials');
assert.equal(freshElements.conversationSave.disabled, false, 'fresh AP session can configure credentials');
assert.equal(freshElements.loadNotice.hidden, true);

// OTA has its own upload deadline. An ambiguous disconnect is not success and
// cannot blindly resubmit a possibly committed image, even after other reads.
elements.file.files = [{name: 'test-ota.bin', size: 2200000}];
elements.upload.onclick();
assert.equal(request.path, '/update');
assert.equal(request.timeout, 630000);
const otaRequest = request;
request.upload.onprogress({lengthComputable: true, loaded: 2200000, total: 2200000});
assert.match(elements.updateMessage.textContent, /正在校验/);
request.ontimeout();
assert.match(elements.updateMessage.textContent, /升级结果尚未确认/);
assert.match(elements.updateMessage.textContent, /重新开启设置后核对版本/);
assert.equal(elements.upload.disabled, true);
await vm.runInContext("load('session')", context);
assert.equal(elements.upload.disabled, true, 'unrelated reads do not unlock ambiguous OTA');
elements.upload.onclick();
assert.equal(request, otaRequest);
assert.equal(timers.size, 0);
console.log('Settings portal: eight-page order, left-start environment charts, monthly archive consent/pagination/verified CSV/cancel, private ordinary-preference backup/restore, isolated drafts, LAN auth, media guards and OTA uncertainty passed.');
