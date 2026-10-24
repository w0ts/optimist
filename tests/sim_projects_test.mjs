// SPDX-License-Identifier: GPL-3.0-only
// One project moved from a device to another (editor command 110 PJ_OP, firmware/src/io/editor/ed_proj.c, and the
// backup's PJ01..PJ16 stream), through the page's own code (web/sim/bk.js moveProject: the Simulator tab's "Get a
// project from the FM-1" / "Send the project to the FM-1"), between two instances of the simulator's firmware: the
// project saved and named, moved into a project that held another (cleared first: the copy is byte for byte, a record
// the new stream lacks is gone), named and
// reopened there; back with the work saved first; an empty project refused; the transport playing refused.
// A build with PATTERNS (the synth profile; no live editing needed):
//   python3 tools/sim.py build --wasm --config config/profiles/synths_USBaudio_BTaudio_BTmidi.config --out build/sim/sim-pj.wasm && node tests/sim_projects_test.mjs build/sim/sim-pj.wasm
import { readFileSync } from "node:fs";
import * as BK from "../web/sim/bk.js";

const WASM = process.argv[2] || new URL("../build/sim/sim-pj.wasm", import.meta.url).pathname;
const module = new WebAssembly.Module(readFileSync(WASM));
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };

function make() {
  const x = new WebAssembly.Instance(module, {}).exports;
  if (x._initialize) x._initialize();
  const mem = () => x.memory.buffer;
  const s = {
    x,
    midiIn: (b) => { new Uint8Array(mem(), x.sim_midi_buf(), b.length).set(b); x.sim_midi_in(b.length); },
    midiOut: () => { const n = x.sim_midi_out(); return Array.from(new Uint8Array(mem(), x.sim_midi_out_buf(), n)); },
    run: (sec) => { let st = 0; const n = Math.round(sec * 44100); for (let f = 0; f < n && !st; f += 1024) { st = x.sim_run(Math.min(1024, n - f)); x.sim_audio_take(); } return st; },
  };
  x.sim_boot();
  s.run(1);
  return s;
}
/* bk.js's client over an instance: the instance runs until the reply came */
function client(s) {
  let buf = [];
  const c = BK.client((b) => s.midiIn(Array.from(b)), 30000);
  async function request(cmd, args, ms) {
    let done = false;
    const p = c.request(cmd, args, ms);
    p.then(() => { done = true; }, () => { done = true; });
    while (!done) {
      s.run(0.05);
      buf = buf.concat(s.midiOut());
      for (let a = buf.indexOf(0xF0), z = buf.indexOf(0xF7, a); a >= 0 && z > a; a = buf.indexOf(0xF0), z = buf.indexOf(0xF7, a)) {
        c.feed(new Uint8Array(buf.slice(a, z + 1)));
        buf = buf.slice(z + 1);
      }
      await new Promise((r) => setImmediate(r));
    }
    return p;
  }
  return { request, feed: c.feed };
}
const BTN = (s, name) => s.x.sim_button(names(s).indexOf(name), 1);
const names = (s) => { const u = new Uint8Array(s.x.memory.buffer, s.x.sim_button_names()); let t = ""; for (let i = 0; u[i]; i++) t += String.fromCharCode(u[i]); return t.split("\n"); };
const ids = (d) => { const o = []; for (let i = 0; i + 3 <= d.length; i += 3 + (d[i + 1] | (d[i + 2] << 8))) o.push(d[i]); return o; };
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

const A = make(), B = make();
const ca = client(A), cb = client(B);
check(await BK.hasProjects(ca), "INFO names opti_proj_1");
check(BK.pjName("my song é!/2  ") === "MY SONG /2", `pjName: "${BK.pjName("my song é!/2  ")}"`);

// A: its work as project 3, named
A.x.sim_enc(3, 5); A.run(0.5);                    // (KNOB 1: a sound value)
await BK.projectOp(ca, 1, 2);
await BK.projectOp(ca, 4, 2, "Sim song");
let pa = await BK.projects(ca);
check(pa.open === 2 && pa.used[2] && pa.names[2] === "SIM SONG", `save + name: open ${pa.open + 1}, used ${pa.used.map(Number).join("")}, "${pa.names[2]}"`);
let bad = "";
try { await BK.projectOp(ca, 4, 2, ""); await ca.request(110, [4, 2, 0x61]).then((r) => { if (r[2]) throw new Error(`rc ${r[2]}`); }); } catch (e) { bad = e.message; }
check(bad === "rc 1", `a name outside the NAME page's characters refused (${bad || "accepted"})`);
await BK.projectOp(ca, 4, 2, "Sim song");

// B: project 6 with a state of its own
B.x.sim_enc(3, -7); B.run(0.5);
await BK.projectOp(cb, 1, 5);

// A's 3 -> B's 6: cleared, written, named, reopened (B's open project)
try {
  const n = await BK.moveProject(ca, cb, 2, 5, { name: "from sim" });
  const got = await BK.readProject(cb, 5), src = await BK.readProject(ca, 2);
  const pb = await BK.projects(cb);
  check(same(got, src), `project 3 -> 6 (${n} bytes): byte for byte (B's ids ${ids(got).join(" ")})`);
  check(pb.names[5] === "FROM SIM" && pb.open === 5 && pb.used[5], `named "${pb.names[5]}", open ${pb.open + 1}`);

  // a stream without one of the records B's project 6 holds: written, the record is gone (cleared first)
  const rec = (d) => { const o = []; for (let i = 0; i + 3 <= d.length;) { const n = 3 + (d[i + 1] | (d[i + 2] << 8)); o.push(d.slice(i, i + n)); i += n; } return o; };
  const parts = rec(got), drop = parts[0][0], fewer = parts.slice(1).flat();
  await BK.writeProject(cb, 5, fewer, "from sim");
  const got1 = await BK.readProject(cb, 5);
  check(same(got1, fewer) && !ids(got1).includes(drop), `written again without record ${drop}: it is gone (ids ${ids(got1).join(" ")})`);
  await BK.moveProject(ca, cb, 2, 5, { name: "from sim" });

  // B's work changed, not saved: sent with save into A's 7, opened there
  B.x.sim_enc(3, 3); B.run(0.5);
  const unsaved = await BK.readProject(cb, 5);
  await BK.moveProject(cb, ca, 5, 6, { save: true, name: "back", open: true });
  const saved = await BK.readProject(cb, 5), got2 = await BK.readProject(ca, 6);
  pa = await BK.projects(ca);
  check(!same(saved, unsaved) && same(got2, saved), `back with the work saved first (B's project ${same(saved, unsaved) ? "unchanged" : "saved"}, A's 7 its copy)`);
  check(pa.open === 6 && pa.names[6] === "BACK" && pa.names[2] === "SIM SONG", `A: open ${pa.open + 1}, "${pa.names[6]}", 3 still "${pa.names[2]}"`);

  let refused = "";
  try { await BK.moveProject(ca, cb, 11, 0); } catch (e) { refused = e.message; }
  check(/empty/.test(refused), `an empty project refused (${refused})`);
  BTN(B, "PLAY"); B.run(0.2); B.x.sim_button(names(B).indexOf("PLAY"), 0); B.run(0.3);
  refused = "";
  try { await BK.moveProject(ca, cb, 2, 9); } catch (e) { refused = e.message; }
  const pb2 = await BK.projects(cb);
  check(/transport/.test(refused) && !pb2.used[9], `the transport playing: refused, nothing written (${refused})`);
} catch (e) { check(false, `moving a project: ${e.message}`); }

console.log(fails ? `sim_projects_test: ${fails} failed` : "sim_projects_test: all passed");
process.exit(fails ? 1 : 0);
