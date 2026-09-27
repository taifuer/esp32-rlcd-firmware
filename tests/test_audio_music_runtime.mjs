import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

// Compile the actual contiguous music-control functions, not a second copy of
// them. The surrounding hardware/RTOS and streaming boundary is stubbed; the
// separate music_stream tests cover real file/decoder lifetime on the host.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = fs.readFileSync(path.join(root, 'src/audio/audio_diagnostics.c'), 'utf8');
const start = source.indexOf('static void music_stop_locked(void)\n{');
const end = source.indexOf('static void audio_worker_task(void *argument)\n{', start);
assert(start >= 0 && end > start, 'music-control test extraction markers changed');
for (const name of ['request_alert', 'audio_voice_start', 'audio_conversation_start', 'audio_diagnostics_start']) {
  const body = source.slice(source.indexOf(`${name}(`));
  assert(body.slice(0, body.indexOf('\n}\n')).includes('music_stop_locked();'), `${name} must cancel music and timer`);
}
const worker = source.slice(end, source.indexOf('\nesp_err_t audio_diagnostics_init(', end));
assert(worker.includes('music_expire_timer_locked();'), 'queued playback must honor an expired timer');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'rlcd-audio-music-'));
try {
  fs.writeFileSync(path.join(temporary, 'audio_music_under_test.inc'), source.slice(start, end));
  const binary = path.join(temporary, 'test_audio_music_runtime');
  const compile = spawnSync('cc', ['-std=c17', '-Wall', '-Wextra', '-Werror', '-pedantic',
    '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I' + temporary,
    '-Itests/music_stubs', '-Isrc/audio/include', '-Isrc/music/include',
    'src/audio/audio_music_policy.c', 'tests/test_audio_music_runtime.c', '-o', binary],
    {cwd: root, encoding: 'utf8'});
  assert.equal(compile.status, 0, compile.stderr || compile.stdout);
  const test = spawnSync(binary, [], {encoding: 'utf8'});
  assert.equal(test.status, 0, test.stderr || test.stdout);
  process.stdout.write(test.stdout);
} finally {
  fs.rmSync(temporary, {recursive: true, force: true});
}
