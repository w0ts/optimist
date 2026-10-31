// SPDX-License-Identifier: GPL-3.0-only
// FM6 user presets through the web editor's library (issue #133): an FM6 sound whose voice came in as DX7 SysEx is
// stored in a user slot (UP_STORE: UP_FM6 keeps the voice beside the record, upreset.c OBJ_UPFM6), copied to the
// library (UP_GET) and back into another slot (UP_PUT); loaded, it must play the voice, not INIT VOICE's sine. And a
// DX7 voice from a .syx file goes straight into a slot the same way. The editor's own request builders and parsers
// (web/editor.html PROTO: bank.get, bank.put, dx7Message) against the simulator's firmware. A build with UP_FM6:
//   python3 tools/sim.py build --wasm --config CONFIG --out build/sim/sim-fm6.wasm && node tests/sim_fm6lib_test.mjs build/sim/sim-fm6.wasm
import { readFileSync } from "node:fs";
import vm from "node:vm";

const HTML = new URL("../web/editor.html", import.meta.url).pathname;
const WASM = process.argv[2] || new URL("../build/sim/sim-fm6.wasm", import.meta.url).pathname;
const html = readFileSync(HTML, "utf8");
const proto = html.slice(html.indexOf("/*wire-section:begin*/"), html.indexOf("/*wire-section:end*/"));
const E = vm.runInNewContext(proto + `;({ parse, req, CMD, bank, dx7Message, dx7Init, unpackDX7, packDX7: typeof packDX7 === "function" ? packDX7 : null })`,
  { setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder });
const module = new WebAssembly.Module(readFileSync(WASM));
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };

function make(flashImage) {
  const x = new WebAssembly.Instance(module, {}).exports;
  if (x._initialize) x._initialize();
  const mem = () => x.memory.buffer;
  if (flashImage) new Uint8Array(mem(), x.sim_flash(), x.sim_flash_size()).set(flashImage);
  const sim = {
    x, pcm: [], flash: () => new Uint8Array(mem(), x.sim_flash(), x.sim_flash_size()).slice(),
    take: () => { const n = x.sim_audio_take(); sim.pcm.push(new Int16Array(mem(), x.sim_audio(), 2 * n).slice()); },
    audio: () => { sim.take(); const a = Int16Array.from(sim.pcm.flatMap((c) => Array.from(c))); sim.pcm = []; return a; },
    midiIn: (b) => { new Uint8Array(mem(), x.sim_midi_buf(), b.length).set(b); x.sim_midi_in(b.length); },
    midiOut: () => { const n = x.sim_midi_out(); return Array.from(new Uint8Array(mem(), x.sim_midi_out_buf(), n)); },
    run: (sec) => { let st = 0; const n = Math.round(sec * 44100); for (let f = 0; f < n && !st; f += 1024) { st = x.sim_run(Math.min(1024, n - f)); sim.take(); } return st; },
  };
  return sim;
}
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
function ask(sim, cmd, args, sec = 3) {
  sim.midiOut();
  sim.midiIn([...HDR, cmd, ...args, 0xF7]);
  let got = [];
  for (let t = 0; t < sec * 20; t++) {
    sim.run(0.05);
    got = got.concat(sim.midiOut());
    const r = frames(got).find((f) => f[4] === cmd);
    if (r) return r.slice(5, -1);
  }
  return null;
}
let sim = make(null);
const rq = async ([cmd, args]) => { const a = ask(sim, cmd, args); if (!a) throw new Error(`timeout ${cmd}`); return a; };
const C = E.CMD;
const rms = (a) => Math.sqrt(a.reduce((s, v) => s + v * v, 0) / Math.max(1, a.length));
/* how far from a sine: the mean |second difference| over the mean |first difference| (a sine at 262 Hz: ~0.04) */
const rough = (a) => {
  let d1 = 0, d2 = 0;
  for (let i = 4; i < a.length; i += 2) { d1 += Math.abs(a[i] - a[i - 2]); d2 += Math.abs(a[i] - 2 * a[i - 2] + a[i - 4]); }
  return d2 / Math.max(1, d1);
};
/* the selected track's note (key 12) for 0.4 s, after the notes before it ended */
function play() {
  sim.run(0.6); sim.audio();
  sim.x.sim_key(12, 1); sim.run(0.4);
  const a = sim.audio();
  sim.x.sim_key(12, 0); sim.run(0.6); sim.audio();
  return a;
}
/* a = b within 2 % of b's level (the same voice, the same note from silence) */
const same = (a, b) => {
  const n = Math.min(a.length, b.length), d = new Int16Array(n);
  for (let i = 0; i < n; i++) d[i] = a[i] - b[i];
  return rms(d) <= 0.02 * rms(b);
};

/* a voice far from a sine: algorithm 1, OP2 (ratio 3) modulating OP1 at full depth, OP2's feedback */
function brassy(name) {
  const v = E.dx7Init();
  const op2 = 4 * 21;
  v[op2 + 16] = 92;                                 /* OP2 output level */
  v[op2 + 18] = 3;                                  /* OP2 coarse 3 */
  v[126 + 8] = 0;                                   /* algorithm 1 */
  v[126 + 9] = 6;                                   /* feedback */
  Array.from(name.padEnd(10, " ").slice(0, 10), (c, i) => { v[145 + i] = c.charCodeAt(0); });
  return v;
}

sim.x.sim_boot();
sim.run(2);
const info = E.parse[C.INFO](await rq(E.req.info()), null);
const fm6 = info.engines.indexOf("FM6");
const upf6 = !!(info.opti && info.opti.opti_upfm6 === 1);
check(fm6 >= 0 && info.pcount > 0, `INFO: FM6 is engine ${fm6 + 1}, P_COUNT ${info.pcount}; opti ${JSON.stringify(info.opti)}`);
await rq(E.req.set(1, 20, fm6));                    /* G_ENGSEL: the selected track plays FM6 */
let d = E.parse[C.DUMP](await rq(E.req.dump()), info);
check(d.engine === fm6, `track 1 plays FM6 (DUMP engine ${d.engine}, VOICE ${d.p[info.pe0]})`);
const sine = play();
check(rms(sine) > 50 && rough(sine) < 0.1, `INIT VOICE: rms ${rms(sine).toFixed(0)}, roughness ${rough(sine).toFixed(3)} (a sine)`);

/* the voice as a website sends it: DX7 SysEx into the FM6 track */
const voice = brassy("ISSUE 133");
sim.midiIn(E.dx7Message(voice)); sim.run(0.5);
const ref = play();
check(rms(ref) > 50 && rough(ref) > 2 * rough(sine), `DX7 voice received: rms ${rms(ref).toFixed(0)}, roughness ${rough(ref).toFixed(3)}`);

/* SAVE AS slot 1, then the library's round trip: slot 1 -> library -> slot 2 */
check(await E.bank.store(rq, 0, "FM6 BRASS") === 0, "UP_STORE the FM6 sound into U01");
check(await E.bank.load(rq, 0) === 0 && same(play(), ref), "UP_LOAD U01 plays the voice (UP_FM6 kept it)");
const g = await E.bank.get(rq, info, 0);
check(g.used && g.engine === fm6, `UP_GET U01: ${g.name}, engine ${g.engine}, voice ${g.dx7 ? g.dx7.length + " values" : "none"}`);
check(upf6 && g.dx7 && g.dx7.length === 155 && g.dx7.every((x, i) => x === voice[i]), "UP_GET U01 carries the slot's own voice (opti_upfm6_1)");
const libEntry = JSON.parse(JSON.stringify({ name: "FM6 BRASS", engine: g.engine, p: g.p, pattern: g.pattern, dx7: g.dx7 }));   /* (as IndexedDB keeps it) */
let rc;
try { rc = await E.bank.put(rq, 1, { ...libEntry, name: "FM6 COPY" }, info); } catch (e) { rc = e.message; }
check(rc === 0, `UP_PUT the library entry into U02: rc ${rc}`);
const g2 = await E.bank.get(rq, info, 1);
check(g2.dx7 && g2.dx7.every((x, i) => x === voice[i]), "UP_GET U02: the voice went with it");
await rq(E.req.set(1, 20, 0)); sim.run(0.3);        /* (another engine in between: nothing left over in the part) */
await rq(E.req.set(1, 20, fm6)); sim.run(0.3);
check(await E.bank.load(rq, 1) === 0, "UP_LOAD U02");
const back = play();
check(same(back, ref), `U02 plays the voice, not a sine: rms ${rms(back).toFixed(0)}, roughness ${rough(back).toFixed(3)} (voice ${rough(ref).toFixed(3)}, sine ${rough(sine).toFixed(3)})`);

/* a DX7 voice from a .syx file (no values of its own) straight into a slot */
const voice2 = brassy("SYX IMPORT");
voice2[4 * 21 + 18] = 5;                            /* (another ratio: another sound) */
await rq(E.req.set(1, 20, fm6)); sim.run(0.3);
sim.midiIn(E.dx7Message(voice2)); sim.run(0.5);
const ref2 = play();
try { rc = await E.bank.put(rq, 2, { name: "SYX IMPORT", engine: fm6, p: g.p, pattern: null, dx7: voice2 }, info); } catch (e) { rc = e.message; }
check(rc === 0, `UP_PUT a .syx voice into U03: rc ${rc}`);
sim.midiIn(E.dx7Message(voice)); sim.run(0.5);      /* (the track back on the first voice) */
check(await E.bank.load(rq, 2) === 0 && same(play(), ref2), "UP_LOAD U03 plays the .syx voice");

/* a record written without a voice drops the slot's old one (the record no longer matches it) */
try { rc = await E.bank.put(rq, 1, { ...libEntry, name: "NO VOICE", dx7: undefined }, info); } catch (e) { rc = e.message; }
const g3 = await E.bank.get(rq, info, 1);
check(rc === 0 && !g3.dx7, `UP_PUT U02 without a voice: rc ${rc}, UP_GET has none`);
/* the issue as reported: an entry without its voice (the editor before the fix; a library entry copied before it) plays
   the record's VOICE, INIT VOICE: a sine */
const lost = await E.bank.load(rq, 1) === 0 ? play() : null;
check(lost && rms(lost) > 50 && rough(lost) < 0.1,`U02 without its voice plays INIT VOICE (the reported sine): roughness ${lost ? rough(lost).toFixed(3) : "-"}`);

/* kept across a restart: the voice is in flash (OBJ_UPFM6), not only in RAM */
sim.run(1);
sim = make(sim.flash());
sim.x.sim_boot();
sim.run(2);
const g4 = await E.bank.get(rq, info, 2);
check(g4.used && g4.dx7 && g4.dx7.every((x, i) => x === voice2[i]), `after a restart: U03 ${g4.name} still has its voice`);

console.log(fails ? `${fails} FAILED` : "all ok");
process.exit(fails ? 1 : 0);
