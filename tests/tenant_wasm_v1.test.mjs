// Real engine integration: Node's WebAssembly validator and per-instance memory.
// Does NOT prove general Atto actors, socket policy or native Forge SFI.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import test from 'node:test';
import { env, pid } from 'node:process';
const dir = env.ATTO_GUEST_DIR;
if (!dir) throw new Error('Expected generated guest module directory');
const bytes = n => readFileSync(join(dir, 'tenant' + n + '.wasm'));
async function start(n) {
  const binary = bytes(n);
  assert.ok(WebAssembly.validate(binary));
  const compiled = await WebAssembly.compile(binary);
  assert.deepEqual(WebAssembly.Module.imports(compiled), []);
  assert.deepEqual(WebAssembly.Module.exports(compiled).map(x => x.name), ['main','memory','peek','poke']);
  const guest = await WebAssembly.instantiate(compiled, {});
  assert.equal(guest.exports.memory.buffer.byteLength, 65536);
  assert.equal(guest.exports.memory.buffer instanceof SharedArrayBuffer, false);
  return guest.exports;
}
test('compiled Atto scalar programs are executable as real WebAssembly', async () => {
  const expected = [55n, 9223372036854775807n, -9223372036854775808n, 5n, -1n, -2n, -9223372036854775808n];
  for (let n = 0; n < expected.length; n++) {
    const guest = await start(n);
    assert.equal(guest.main(), expected[n]);
  }
});
test('two guests in one PID have distinct, bounded linear memories', async () => {
  const before = pid;
  const [a,b] = await Promise.all([start(0),start(1)]);
  assert.equal(pid, before);
  assert.notEqual(a.memory.buffer, b.memory.buffer);
  a.poke(0, 171); a.poke(65535, 52);
  assert.equal(a.peek(0), 171);
  assert.equal(a.peek(65535), 52);
  assert.equal(b.peek(0), 0);
  assert.equal(b.peek(65535), 0);
  b.poke(0, 12);
  assert.equal(a.peek(0), 171);
  assert.equal(b.peek(0), 12);
  assert.throws(() => a.peek(65536), WebAssembly.RuntimeError);
  assert.throws(() => a.poke(65536, 255), WebAssembly.RuntimeError);
  assert.throws(() => b.peek(-1), WebAssembly.RuntimeError);
  assert.throws(() => a.memory.grow(1), RangeError);
  assert.equal(b.main(), 9223372036854775807n);
  assert.equal(b.peek(0), 12);
});
test('Wasm validator rejects malformed or tampered artifacts', async () => {
  for (const mutation of [
    raw => {raw[0] = 0x01;},
    raw => {raw[raw.length-1] = 0xff;},
  ]) {
    const bad = Buffer.from(bytes(0));
    mutation(bad);
    assert.equal(WebAssembly.validate(bad), false);
    await assert.rejects(WebAssembly.compile(bad), WebAssembly.CompileError);
  }
});
test('fresh tenant instances never inherit private memory', async () => {
  for (let n = 0; n < 40; n++) {
    const old = await start(0);
    old.poke(4096, 77);
    const fresh = await start(0);
    assert.equal(fresh.peek(4096), 0);
    assert.equal(old.peek(4096), 77);
  }
});
