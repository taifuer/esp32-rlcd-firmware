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
  let request;
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
    fetch: async () => ({ok: true, json: async () => state}),
    TextEncoder, confirm: () => true, XMLHttpRequest: FakeRequest,
  });
  vm.runInContext(javascript, context);
  await new Promise(resolve => setImmediate(resolve));
  elements.upload.onclick();
  assert.equal(request.method, 'POST');
  assert.equal(request.path, '/update');
  assert.equal(request.headers['X-RLCD-Token'], 'test-token');
  assert.equal(elements.wifiName.textContent, state.wifi_configured
    ? 'Lab Wi-Fi'
    : state.wifi_readable ? '尚未配置' : '无法读取，可更换网络或升级');
}
console.log('Recovery portal: configured, unconfigured and unreadable Wi-Fi passed.');
