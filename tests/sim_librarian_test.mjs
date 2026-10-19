// SPDX-License-Identifier: GPL-3.0-only
// The web editor's librarian end to end (builder LIBRARIAN with ED_LIVE off, firmware/src/io/editor/ed_lite.c): the
// simulator's firmware answers the editor's own request builders and parsers (web/editor.html PROTO): INFO, the
// sections, the user presets, the user kits, the patterns, a section's file moved onto another section, kept across a
// restart. A build with live editing off (the synth profile):
//   python3 tools/sim.py build --wasm --config CONFIG --out build/sim/sim-lib.wasm && node tests/sim_librarian_test.mjs build/sim/sim-lib.wasm
import { readFileSync } from "node:fs";
import vm from "node:vm";

const HTML = new URL("../web/editor.html", import.meta.url).pathname;
const WASM = process.argv[2] || new URL("../build/sim/sim-lib.wasm", import.meta.url).pathname;
const html = readFileSync(HTML, "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `;({ parse, req, CMD, bank, bkListAll, backupFile, readBackupFile, patReadAll, patWriteAll, crc32, BK, PATREC, patRecBuild, patRawEmpty })`,
  { setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder });
const module = new WebAssembly.Module(readFileSync(WASM));
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };

function make(flashImage) {
  const x = new WebAssembly.Instance(module, {}).exports;
  if (x._initialize) x._initialize();
  const mem = () => x.memory.buffer;
  if (flashImage) new Uint8Array(mem(), x.sim_flash(), x.sim_flash_size()).set(flashImage);
  return {
    x,
    flash: () => new Uint8Array(mem(), x.sim_flash(), x.sim_flash_size()).slice(),
    midiIn: (b) => { new Uint8Array(mem(), x.sim_midi_buf(), b.length).set(b); x.sim_midi_in(b.length); },
    midiOut: () => { const n = x.sim_midi_out(); return Array.from(new Uint8Array(mem(), x.sim_midi_out_buf(), n)); },
    run: (sec) => { let st = 0; const n = Math.round(sec * 44100); for (let f = 0; f < n && !st; f += 1024) { st = x.sim_run(Math.min(1024, n - f)); x.sim_audio_take(); } return st; },
  };
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
function ask(sim, cmd, args, sec = 1) {
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
let sim;
const rq = async ([cmd, args]) => { const a = ask(sim, cmd, args, 3); if (!a) throw new Error(`timeout ${cmd}`); return a; };
const C = E.CMD;

sim = make(null);
sim.x.sim_boot();
sim.run(2);
const info = E.parse[C.INFO](await rq(E.req.info()), null);
check(info.lite && info.lib && info.nengines > 0 && info.pcount > 0 && info.ntrk === 4 && info.engines.length === info.nengines,
  `INFO: lite + librarian, ${info.nengines} engines (${info.engines.join(" ")}), P_COUNT ${info.pcount}, ${info.ntrk} tracks, patterns ${JSON.stringify(info.pat)}`);
check(ask(sim, C.GET, [0, 0, 0], 0.5) === null && ask(sim, C.DUMP, [], 0.5) === null && ask(sim, C.DRUM_LANES, [], 0.5) === null,
  "nothing live: GET, DUMP, DRUM_LANES unanswered");

// sections
const used = [];
for (let k = 0; k < 16; k++) used.push(E.parse[C.PROJECT](await rq(E.req.project(2, k))).used);
check(used.every((u) => u === 0), `PROJECT query A..P: ${used.join("")}`);
const sv = E.parse[C.PROJECT](await rq(E.req.project(1, 0)));
check(sv.used === 1, `PROJECT save A -> used ${sv.used}`);
const ld = E.parse[C.PROJECT](await rq(E.req.project(0, 0)));
check(ld.used === 1, `PROJECT load A -> used ${ld.used}`);

// user presets
const b0 = await E.bank.list(rq);
check(b0.total === 32 && b0.slots.length === 32 && b0.slots.every((s) => !s.used), `UP_LIST: ${b0.total} slots, none used`);
check(await E.bank.store(rq, 3, "LIBTEST") === 0, "UP_STORE slot 4 (the selected track's sound)");
const g = await E.bank.get(rq, info, 3);
check(g.used && g.name === "LIBTEST" && g.p.length === info.pcount, `UP_GET slot 4: ${g.name}, engine ${info.engines[g.engine]}, ${g.p.length} values`);
check(await E.bank.put(rq, 7, { ...g, name: "COPY" }) === 0, "UP_PUT the record into slot 8 (a copy)");
const g8 = await E.bank.get(rq, info, 7);
check(g8.used && g8.name === "COPY" && JSON.stringify(g8.p) === JSON.stringify(g.p), "UP_GET slot 8: the same values");
check(await E.bank.erase(rq, 3) === 0 && !(await E.bank.list(rq)).slots[3].used, "UP_ERASE slot 4");
check(await E.bank.load(rq, 7) === 0, "UP_LOAD slot 8 onto the track");

// user kits
const k0 = E.parse[C.UKIT_LIST](await rq(E.req.ukitList()));
check(k0.kits.length === 16 && k0.kits.every((u) => !u.used), `UKIT_LIST: ${k0.kits.length} kits, none used`);
const st = E.parse[C.UKIT_OP](await rq(E.req.ukitOp(2, 2)));
const k1 = E.parse[C.UKIT_LIST](await rq(E.req.ukitList()));
check(st.rc === 0 && k1.kits[2].used, `UKIT_OP store the project's lanes into KIT 3: rc ${st.rc}`);
const kg = E.parse[C.UKIT_GET](await rq(E.req.ukitGet(2, true)));
check(kg.ok && kg.kit, "UKIT_GET v2 KIT 3 (with its sends)");
const kp = E.parse[C.UKIT_PUT](await rq(E.req.ukitPut(5, kg.kit, true)));
check(kp.rc === 0 && E.parse[C.UKIT_LIST](await rq(E.req.ukitList())).kits[5].used, `UKIT_PUT v2 KIT 3's into KIT 6 (an import): rc ${kp.rc}`);
const ke = E.parse[C.UKIT_OP](await rq(E.req.ukitOp(2, 1)));
check(ke.rc === 0 && !E.parse[C.UKIT_LIST](await rq(E.req.ukitList())).kits[2].used, "UKIT_OP erase KIT 3");
check(E.parse[C.UKIT_OP](await rq(E.req.ukitOp(5, 0))).rc === 0, "UKIT_OP load KIT 6 into the project");

// patterns
if (info.pat) {
  const o = { flags: E.PATREC.V1, hdr: [16, 1, 0, 50], mot: null, sx: null, raw: [] };   /* (the page's mock's record: a rising line) */
  for (let i = 0; i < 64; i++) o.raw.push(i < 16 && i % 2 === 0 ? [48 + i, 0, 0, 0, 1, 0, 0, 100, 0, 0] : E.patRawEmpty(false));
  await E.patWriteAll(rq, 0, 0, E.patRecBuild(o, false));
  let L = E.parse[C.PAT_LIST](await rq(E.req.patList(0)));
  check(L.len[0][0] === 16, `PAT_WRITE a 16-step record into track 1 / pattern 1: LEN ${L.len[0][0]}`);
  const bytes = await E.patReadAll(rq, 0, 0);
  check(bytes.length > 0, `PAT_READ pattern 1: ${bytes.length} B (the export)`);
  await E.patWriteAll(rq, 0, 5, bytes);
  L = E.parse[C.PAT_LIST](await rq(E.req.patList(0)));
  const back = await E.patReadAll(rq, 0, 5);
  check(L.len[0][5] === L.len[0][0] && back.length === bytes.length, `PAT_WRITE into pattern 6 (the import): LEN ${L.len[0][5]}, ${back.length} B`);
  let bad = null;
  try { await E.patWriteAll(rq, 3, 0, bytes); } catch (e) { bad = e; }
  check(bad && bad.rc === 2, `a synth track's pattern onto the drum track refused: ${bad && bad.message}`);
  const cp = E.parse[C.PAT_OP](await rq(E.req.patOp(1, 0, 0, 1, 2)));
  L = E.parse[C.PAT_LIST](await rq(E.req.patList(0)));
  check(cp.rc === 0 && L.len[1][2] > 0, `PAT_OP copy track 1 / 1 -> track 2 / 3: rc ${cp.rc}`);
  const cl = E.parse[C.PAT_OP](await rq(E.req.patOp(2, 0, 5)));
  L = E.parse[C.PAT_LIST](await rq(E.req.patList(0)));
  check(cl.rc === 0 && L.len[0][5] === 0, `PAT_OP clear track 1 / 6: rc ${cl.rc}`);
}

// a section's file: its backup object out of A, into C (the page's lbrSecExport / lbrSecImport)
let BL = await E.bkListAll(rq);
const oa = BL.objs.find((o) => o.tag === "S01 "), oc = BL.objs.find((o) => o.tag === "S03 ");
check(oa && oa.hasData && oc, `BK_LIST: S01 has data (${oa && oa.len} B), S03 listed`);
const data = new Uint8Array(oa.len);
for (let off = 0; off < oa.len;) {
  const r = E.parse[C.BK_READ](await rq(E.req.bkRead(oa.i, off)));
  const c = Uint8Array.from(r.data).subarray(0, oa.len - off);
  data.set(c, off);
  off += c.length;
}
check(E.crc32(data) === oa.crc, "BK_READ S01: CRC");
const file = E.backupFile({ version: info.version }, [{ tag: oa.tag, kind: oa.kind, fm6: oa.fm6, data }]);
const x = E.readBackupFile(file).objects.find((o) => o.kind === "sec");
check(x && x.data.length === data.length, `the section file: ${file.length} B, one sec object`);
let r = E.parse[C.BK_BEGIN](await rq(E.req.bkBegin(oc.i, x.data.length, E.crc32(x.data))));
for (let off = 0; !r.rc && off < x.data.length; off += E.BK.CHUNK) r = E.parse[C.BK_DATA](await rq(E.req.bkData(oc.i, off, x.data.subarray(off, off + E.BK.CHUNK))));
if (!r.rc) r = E.parse[C.BK_COMMIT](await rq(E.req.bkCommit(oc.i)));
check(!r.rc, `BK_BEGIN / DATA / COMMIT into S03: rc ${r.rc}`);
await rq(E.req.bkEnd(true));
sim.run(1);
const fl = sim.flash();
sim = make(fl);
sim.x.sim_boot();
sim.run(2);
const after = [];
for (let k = 0; k < 4; k++) after.push(E.parse[C.PROJECT](await rq(E.req.project(2, k))).used);
check(after[0] === 1 && after[2] === 1, `after the restart: sections A..D ${after.join("")} (C imported from A's file)`);
const b1 = await E.bank.list(rq);
check(b1.slots[7].used && b1.slots[7].name === "COPY", "after the restart: user preset 8 kept");
console.log(fails ? `${fails} FAILED` : "all ok");
process.exit(fails ? 1 : 0);
