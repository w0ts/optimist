// SPDX-License-Identifier: GPL-3.0-only
// DRUMS=0 (the builder's DRUMS off) in the browser simulator: four synth tracks (MIDI channels 1..4), a project of a build
// with drums (FUNB) read and converted (track 4 a new part, not the fallback to the selected track), the build's own project
// (FUNC) kept across a restart.
//   node tests/sim_nodrums_test.mjs <no-drums sim.wasm> <drums sim.wasm, the same profile>
import { readFileSync } from 'node:fs';

let fails = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; };
const rms = (a) => Math.sqrt(a.reduce((s, v) => s + v * v, 0) / Math.max(1, a.length));

function make(wasm, flashImage) {
  const inst = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(wasm)), {});
  const x = inst.exports;
  if (x._initialize) x._initialize();
  const mem = () => x.memory.buffer;
  if (flashImage) new Uint8Array(mem(), x.sim_flash(), x.sim_flash_size()).set(flashImage);
  const sim = {
    x,
    flash: () => new Uint8Array(mem(), x.sim_flash(), x.sim_flash_size()).slice(),
    pcm: [],
    take: () => { const n = x.sim_audio_take(); sim.pcm.push(new Int16Array(mem(), x.sim_audio(), 2 * n).slice()); },
    audio: () => { sim.take(); const a = Int16Array.from(sim.pcm.flatMap((c) => Array.from(c))); sim.pcm = []; return a; },
    midiIn: (bytes) => { new Uint8Array(mem(), x.sim_midi_buf(), bytes.length).set(bytes); x.sim_midi_in(bytes.length); },
    run: (sec) => { let st = 0; const frames = Math.round(sec * 44100); for (let f = 0; f < frames && !st; f += 1024) { st = x.sim_run(Math.min(1024, frames - f)); sim.take(); } return st; },
  };
  x.sim_boot();
  return sim;
}
// a note on MIDI channel ch (1..16) held 0.4 s: the output's rms while it plays
function noteRms(sim, ch, note = 60) {
  sim.run(0.3);
  sim.audio();
  sim.midiIn([0x90 | (ch - 1), note, 110]);
  sim.run(0.4);
  const a = sim.audio();
  sim.midiIn([0x80 | (ch - 1), note, 0]);
  sim.run(0.6);
  sim.audio();
  return rms(a);
}

const [ND, DR] = process.argv.slice(2);
// ---- no drums: four parts, each its channel
let s = make(ND);
check(s.x.sim_status() === 0, 'no drums: boots');
s.run(2);
for (let ch = 1; ch <= 4; ch++) {
  const r = noteRms(s, ch);
  check(r > 50, `no drums: channel ${ch} plays track ${ch} (rms ${r.toFixed(0)})`);
}

// ---- a project of the build with drums, autosaved, then booted by the build without
let d = make(DR);
check(d.x.sim_status() === 0, 'drums: boots');
d.run(2);
const dr4 = noteRms(d, 1);
check(dr4 > 50, `drums: channel 1 plays (rms ${dr4.toFixed(0)})`);
const blank = d.flash();
d.run(20);                                            // (the autosave: stopped and quiet)
const fDrums = d.flash();
check(fDrums.some((v, i) => v !== blank[i]), 'drums: the autosave wrote the flash');
s = make(ND, fDrums);
check(s.x.sim_status() === 0, 'no drums, the drums build\'s flash: boots');
s.run(2);
for (let ch = 1; ch <= 4; ch++) {
  const r = noteRms(s, ch);
  check(r > 50, `no drums, its project converted: channel ${ch} plays (rms ${r.toFixed(0)})`);
  if (ch === 4) check(Math.abs(r - 1900) > 200, 'no drums, converted: channel 4 is track 4 (its pad), not the fallback to track 1');
}
// ---- its own format kept
s.run(20);
const fNd = s.flash();
check(fNd.some((v, i) => v !== fDrums[i]), 'no drums: the autosave wrote its own project');
const has = (f, m) => { const b = Buffer.from(f); return b.indexOf(Buffer.from(m, 'latin1')) >= 0; };
check(has(fDrums, 'BNUF') && !has(fDrums, 'CNUF'), 'drums: its projects are FUNB');
check(has(fNd, 'CNUF'), 'no drums: its project is FUNC');
s = make(ND, fNd);
check(s.x.sim_status() === 0, 'no drums, restarted on its own flash: boots');
s.run(2);
const r4 = noteRms(s, 4);
check(r4 > 50, `no drums, restarted: track 4 plays (rms ${r4.toFixed(0)})`);
console.log(fails ? `ND SIM FAILED (${fails})` : 'ND SIM PASSED');
process.exit(fails ? 1 : 0);
