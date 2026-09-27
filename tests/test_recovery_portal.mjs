import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

// Optional browser-script check: node tests/test_recovery_portal.mjs
// Uses the exact embedded page, without third-party browser dependencies.
const source = fs.readFileSync(
  new URL('../src/update/firmware_update.c', import.meta.url), 'utf8');
const block = source.split('static const char RECOVERY_SETTINGS_PAGE[] =')[1]
  .split('static const char SETTINGS_PAGE[] =')[0];
const html = [...block.matchAll(/^\s*("(?:[^"\\]|\\.)*")/gm)]
  .map(match => JSON.parse(match[1])).join('');
const javascript = html.match(/<script>([\s\S]*)<\/script>/)[1];
new vm.Script(javascript);
const ids = [...html.matchAll(/id="([^"]+)"/g)].map(match => match[1]);

for (const state of [
  {token: 'test-token', wifi_configured: true, wifi_readable: true, wifi_ssid: 'Lab Wi-Fi'},
  {token: 'test-token', wifi_configured: false, wifi_readable: true, wifi_ssid: ''},
  {token: 'test-token', wifi_configured: false, wifi_readable: false, wifi_ssid: ''},
]) {
  const elements = Object.fromEntries(ids.map(id => [id, {
    value: '', checked: false, disabled: false,
    files: [{name: 'firmware-ota.bin'}], focus() {},
  }]));
  elements.wifiForm.hidden = true;
  let request, requests = 0, posts = 0, failRead = true, hangRead = false, hangPost = false;
  const timers = new Map();
  let nextTimer = 0;
  const waitForAbort = signal => new Promise((resolve, reject) => {
    signal.addEventListener('abort', () => reject(Object.assign(new Error('aborted'), {name: 'AbortError'})), {once: true});
  });
  class FakeRequest {
    constructor() { request = this; this.headers = {}; this.upload = {}; }
    open(method, path) { this.method = method; this.path = path; }
    setRequestHeader(key, value) { this.headers[key] = value; }
    send(body) { this.body = body; }
  }
  const context = vm.createContext({
    document: {getElementById(id) {
      assert.ok(elements[id], `missing element ${id}`);
      return elements[id];
    }},
    fetch: async (path, options = {}) => {
      requests++;
      if (options.method === 'POST') {
        posts++;
        if (hangPost) { hangPost = false; return waitForAbort(options.signal); }
        return {ok: true, text: async () => '已处理'};
      }
      if (hangRead) { hangRead = false; return {ok: true, json: () => waitForAbort(options.signal)}; }
      return failRead ? {ok: false, text: async () => '暂不可用'} : {ok: true, json: async () => state};
    },
    TextEncoder, confirm: () => true, XMLHttpRequest: FakeRequest, AbortController,
    setTimeout(callback, ms) { const id = ++nextTimer; timers.set(id, {callback, ms}); return id; },
    clearTimeout(id) { timers.delete(id); },
  });
  vm.runInContext(javascript, context);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(elements.retryLoad.hidden, false);
  assert.equal(elements.upload.disabled, true, 'initial state failure never enables an unauthenticated upload');
  assert.equal(elements.wifiSave.disabled, true);
  elements.upload.onclick();
  assert.equal(request, undefined);
  failRead = false;
  await elements.retryLoad.onclick();
  assert.equal(elements.retryLoad.hidden, true);
  assert.equal(elements.upload.disabled, false, 'explicit read retry recovers local OTA even if saved Wi-Fi cannot be parsed');
  assert.equal(elements.wifiSsid.value, state.wifi_ssid);
  elements.wifiForm.hidden = false;
  elements.wifiSsid.value = 'unsaved network';
  await elements.retryLoad.onclick();
  assert.equal(elements.wifiSsid.value, 'unsaved network', 'read retries preserve visible Wi-Fi drafts');
  const expire = expectedMs => {
    assert.equal(timers.size, 1);
    const [id, timer] = timers.entries().next().value;
    assert.equal(timer.ms, expectedMs);
    timers.delete(id);
    timer.callback();
  };
  hangRead = true;
  let pending = elements.retryLoad.onclick();
  await new Promise(resolve => setImmediate(resolve));
  expire(15000);
  await pending;
  assert.equal(elements.retryLoad.hidden, false);
  assert.match(elements.loadMessage.textContent, /确认门户仍在开启后重试/);
  await elements.retryLoad.onclick();
  elements.wifiPassword.value = 'password123';
  hangPost = true;
  pending = elements.wifiForm.onsubmit({preventDefault() {}});
  expire(45000);
  await pending;
  assert.equal(posts, 1, 'Wi-Fi write timeout is never automatically replayed');
  assert.match(elements.wifiMessage.textContent, /操作结果尚未确认/);
  assert.equal(elements.wifiSave.disabled, false);
  elements.upload.onclick();
  assert.equal(request.method, 'POST');
  assert.equal(request.path, '/update');
  assert.equal(request.headers['X-RLCD-Token'], 'test-token');
  assert.equal(request.timeout, 630000);
  assert.equal(elements.wifiName.textContent, state.wifi_configured
    ? 'Lab Wi-Fi'
    : state.wifi_readable ? '尚未配置' : '无法读取，可更换网络或升级');
  request.upload.onprogress({lengthComputable: true, loaded: 100, total: 100});
  assert.match(elements.updateMessage.textContent, /正在校验/);
  request.onerror();
  assert.match(elements.updateMessage.textContent, /升级结果尚未确认/);
  assert.match(elements.updateMessage.textContent, /重新开启设置后核对版本/);
  assert.equal(elements.upload.disabled, true);
  assert.equal(elements.wifiSave.disabled, true);
  const before = requests, previousRequest = request;
  await elements.retryLoad.onclick();
  elements.upload.onclick();
  assert.equal(requests, before, 'recovery read does not race an ambiguous in-flight OTA');
  assert.equal(request, previousRequest, 'ambiguous OTA cannot be blindly resubmitted');
  assert.equal(timers.size, 0);
}
console.log('Recovery portal: initial failures/retry, unreadable Wi-Fi, bounded reads/writes and OTA uncertainty passed.');
