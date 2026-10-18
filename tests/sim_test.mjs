// SPDX-License-Identifier: GPL-3.0-only
// The browser simulator's WebAssembly (tools/sim.py build --wasm -> build/sim/sim.wasm), under node, no browser: it
// boots, draws, sounds after a key, answers the web editor's SysEx (INFO, GET / SET / DESC, PROJECT, BK_LIST) and
// keeps what it saved in its flash across a restart (a fresh instance on the same flash bytes). Prints the wall time a
// simulated second takes. docs/SIMULATOR.md.
//   node tests/sim_test.mjs [build/sim/sim.wasm]
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';

const WASM = process.argv[2] || new URL('../build/sim/sim.wasm', import.meta.url).pathname;
const bytes = readFileSync(WASM);
const module = new WebAssembly.Module(bytes);
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; };

function make(flashImage) {
  const inst = new WebAssembly.Instance(module, {});
  const x = inst.exports;
  if (x._initialize) x._initialize();
  const mem = () => x.memory.buffer;
  if (flashImage) new Uint8Array(mem(), x.sim_flash(), x.sim_flash_size()).set(flashImage);
  const sim = {
    x,
    flash: () => new Uint8Array(mem(), x.sim_flash(), x.sim_flash_size()).slice(),
    pixels: () => new Uint16Array(mem(), x.sim_pixels(), 240 * 240),
    pcm: [],
    take: () => { const n = x.sim_audio_take(); sim.pcm.push(new Int16Array(mem(), x.sim_audio(), 2 * n).slice()); },
    audio: () => { sim.take(); const a = Int16Array.from(sim.pcm.flatMap((c) => Array.from(c))); sim.pcm = []; return a; },
    midiIn: (bytes) => { new Uint8Array(mem(), x.sim_midi_buf(), bytes.length).set(bytes); x.sim_midi_in(bytes.length); },
    midiOut: () => { const n = x.sim_midi_out(); return Array.from(new Uint8Array(mem(), x.sim_midi_out_buf(), n)); },
    run: (sec) => { let st = 0; const frames = Math.round(sec * 44100); for (let f = 0; f < frames && !st; f += 1024) { st = x.sim_run(Math.min(1024, frames - f)); sim.take(); } return st; },
    cstr: (p) => { const u = new Uint8Array(mem(), p); let s = ''; for (let i = 0; u[i]; i++) s += String.fromCharCode(u[i]); return s; },
  };
  return sim;
}

// the editor's frames: F0 7D 46 4C cmd args F7; the replies collected from the device's MIDI out
const HDR = [0xF0, 0x7D, 0x46, 0x4C];
function frames(bytes) {
  const out = [];
  let cur = null;
  for (const b of bytes) {
    if (b === 0xF0) cur = [b];
    else if (cur) { cur.push(b); if (b === 0xF7) { out.push(cur); cur = null; } }
  }
  return out.filter((f) => HDR.every((h, i) => f[i] === h));
}
function ask(sim, cmd, args = [], sec = 0.3) {
  sim.midiOut();
  sim.midiIn([...HDR, cmd, ...args, 0xF7]);
  let got = [];
  for (let t = 0; t < sec * 10; t++) {
    sim.run(0.1);
    got = got.concat(sim.midiOut());
    const r = frames(got).find((f) => f[4] === cmd);
    if (r) return r.slice(5, -1);
  }
  return null;
}
const v14 = (v) => [(v + 8192) & 0x7F, ((v + 8192) >> 7) & 0x7F];
const fromV14 = (lo, hi) => (lo | (hi << 7)) - 8192;
const rms = (a) => Math.sqrt(a.reduce((s, v) => s + v * v, 0) / Math.max(1, a.length));

// ---- boot, 3 s
let sim = make(null);
const x = sim.x;
check(x.sim_info(0) === 27 && x.sim_info(1) === 14 && x.sim_info(2) === 7, `info: ${x.sim_info(0)} keys, ${x.sim_info(1)} buttons, ${x.sim_info(2)} knobs, half ${x.sim_info(3)}, ${x.sim_info(4)} Hz`);
check(sim.cstr(x.sim_button_names()).split('\n').length === 14, `buttons: ${sim.cstr(x.sim_button_names()).replace(/\n/g, ' ')}`);
let t0 = performance.now();
x.sim_boot();
const tBoot = performance.now() - t0;
check(x.sim_status() === 0, `boot (status ${x.sim_status()}, ${tBoot.toFixed(0)} ms wall)`);
sim.audio();
t0 = performance.now();
let st = sim.run(3);
const t3 = performance.now() - t0;
check(st === 0, `3 s run (status ${st})`);
console.log(`     ${(t3 / 3).toFixed(0)} ms wall per simulated second (idle)`);
const px = sim.pixels();
const colours = new Set(px).size;
check(colours > 2, `screen: ${colours} colours, frame rev ${x.sim_frame_rev()}, dirty ${x.sim_dirty().toString(16)}`);
const quiet = sim.audio();
check(quiet.length >= 2 * 44100 * 2.9, `audio: ${quiet.length / 2} frames for 3 s`);

// ---- a key: sound
x.sim_key(12, 1);
t0 = performance.now();
sim.run(0.5);
const tKey = performance.now() - t0;
const loud = sim.audio();
x.sim_key(12, 0);
sim.run(0.5);
check(rms(loud) > 50, `key 12: rms ${rms(loud).toFixed(0)} (silence before: ${rms(quiet).toFixed(0)}); ${(tKey / 0.5).toFixed(0)} ms wall per simulated second playing`);
const leds = new Uint8Array(x.memory.buffer, x.sim_leds(), x.sim_led_count());
check(leds.some((v) => v), `leds: ${Array.from(leds).join('')}`);

// ---- the editor
const info = ask(sim, 1);
check(info && info.length > 10, `INFO: ${info ? String.fromCharCode(...info.slice(0, info.indexOf(0))) : 'no reply'}`);
const bk = ask(sim, 43, [4, 0]);
check(bk && bk.length > 10, `BK_LIST: ${bk ? bk.length + ' bytes' : 'no reply'}`);
// a track parameter changed, the project saved to slot 0
const PID = 0;
const desc = ask(sim, 5, [0, PID]);
const get = ask(sim, 2, [0, PID]);
check(desc && get, `DESC / GET track parameter ${PID}`);
const lo = fromV14(desc[3], desc[4]), hi = fromV14(desc[5], desc[6]), was = fromV14(get[2], get[3]);
const want = was + 3 <= hi ? was + 3 : Math.max(lo, was - 3);
const set = ask(sim, 3, [0, PID, ...v14(want)]);
check(set && fromV14(set[2], set[3]) === want, `SET parameter ${PID}: ${was} -> ${want}`);
const rev0 = x.sim_flash_rev();
const save = ask(sim, 9, [1, 0], 3);
check(save && save[0] === 1, `PROJECT save slot 0 (flash rev ${rev0} -> ${x.sim_flash_rev()})`);
sim.run(1);
const image = sim.flash();

// ---- a restart on the same flash
sim = make(image);
sim.x.sim_boot();
sim.run(1);
const q = ask(sim, 9, [0, 0], 3);
const after = ask(sim, 2, [0, PID]);
check(q && after && fromV14(after[2], after[3]) === want, `after a restart and PROJECT load: parameter ${PID} = ${after ? fromV14(after[2], after[3]) : '?'} (want ${want})`);

// ---- backup and restore (the editor's 43..48): the working project (AUTO) read from this instance, written into a
// blank one, which restarts (status 1); a fresh instance on its flash has the parameter
const u7n = (v, n) => Array.from({ length: n }, (_, k) => (v >>> (7 * k)) & 0x7F);
const fromU7 = (a, n) => a.slice(0, n).reduce((s, v, k) => s + v * 2 ** (7 * k), 0);
const pack7 = (d) => { const o = []; for (let i = 0; i < d.length; i += 7) { const g = d.slice(i, i + 7); o.push(g.reduce((t, v, j) => t | ((v >> 7) << j), 0), ...g.map((v) => v & 0x7F)); } return o; };
const unpack7 = (p) => { const o = []; for (let i = 0; i < p.length; i += 8) { const t = p[i]; for (let j = 1; j < 8 && i + j < p.length; j++) o.push(p[i + j] | (((t >> (j - 1)) & 1) << 7)); } return o; };
const CRCT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc32 = (d) => { let c = 0xFFFFFFFF; for (const v of d) c = CRCT[(c ^ v) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
function bkObjects(s) {
  const objs = [];
  for (let first = 0; ;) {
    const r = ask(s, 43, [4, first]);
    if (!r) return null;
    const n = r[1], f = r[10], count = r[11];
    for (let k = 0, o = 12; k < count; k++, o += 14)
      objs.push({ i: f + k, tag: String.fromCharCode(...r.slice(o, o + 4)), kind: r[o + 4], flags: r[o + 5], len: fromU7(r.slice(o + 6), 3), crc: fromU7(r.slice(o + 9), 5) });
    first = f + count;
    if (first >= n || !count) return objs;
  }
}
const want2 = want - 1 >= lo ? want - 1 : want + 1;
ask(sim, 3, [0, PID, ...v14(want2)]);
sim.run(12);                                      // (the autosave: 10 s after the last change)
const objs = bkObjects(sim);
const auto = objs && objs.find((o) => o.tag === 'AUTO');
check(auto && auto.len > 0, `BK_LIST: ${objs ? objs.length : 0} objects, AUTO ${auto ? auto.len + ' bytes' : 'missing'}`);
let data = [];
for (let off = 0; auto && off < auto.len;) {
  const r = ask(sim, 44, [auto.i, ...u7n(off, 3)]);
  if (!r) break;
  const chunk = unpack7(r.slice(9));
  if (!chunk.length || crc32(chunk) !== fromU7(r.slice(4), 5)) break;
  data = data.concat(chunk);
  off += chunk.length;
}
data = data.slice(0, auto ? auto.len : 0);
check(auto && data.length === auto.len, `BK_READ AUTO: ${data.length} bytes`);
const blank = make(null);
blank.x.sim_boot();
blank.run(1);
if (!auto) { console.log("sim_test: no AUTO"); process.exit(1); }
const begin = ask(blank, 45, [auto.i, ...u7n(data.length, 3), ...u7n(crc32(data), 5)]);
let rc = begin ? begin[1] : -1;
for (let off = 0; !rc && off < data.length; off += 256) {
  const c = data.slice(off, off + 256);
  const r = ask(blank, 46, [auto.i, ...u7n(off, 3), ...u7n(crc32(c), 5), ...pack7(c)]);
  rc = r ? r[4] : -1;
}
const commit = rc ? null : ask(blank, 47, [auto.i]);
const end = commit && !commit[1] ? ask(blank, 48, [1]) : null;
check(end && end[0] === 0, `restore AUTO into a blank flash: BEGIN / DATA rc ${rc}, COMMIT rc ${commit ? commit[1] : '?'}, END ${end ? end[0] : '?'}`);
st = blank.run(1);
check(st === 1, `the restore restarts (status ${st})`);
const restored = make(blank.flash());
restored.x.sim_boot();
restored.run(0.5);
const rget = ask(restored, 2, [0, PID]);
check(rget && fromV14(rget[2], rget[3]) === want2, `after the restore: parameter ${PID} = ${rget ? fromV14(rget[2], rget[3]) : '?'} (want ${want2})`);

// ---- a restore's restart / UBOOT end the run with their status
sim.midiIn([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7]);
st = sim.run(1);
check(st === 2, `UBOOT SysEx: status ${st}`);

console.log(fails ? `sim_test: ${fails} failed` : 'sim_test: all passed');
process.exit(fails ? 1 : 0);
