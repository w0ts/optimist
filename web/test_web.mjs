// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html: the protocol section (between PROTO-BEGIN/END) against its mock device (v1 commands,
//   the user preset bank / librarian, library files, live pushes, older-firmware fallback, the v3 tracks
//   and the mixer), its tab layout and ja/en strings,
//   and the user-sample pipeline byte for byte against tools/sampleio.py
// - fm1pkg.js: productOf and logicalImage on build/felucca.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { STOCK_V15_SHA256, logicalImage, productOf, validateStockPackage } from "./fm1pkg.js";
import { OFFICIAL_LOADER, OUR_LOADER, Updater, pack7, unpack7 } from "./fm1ota.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const PYTHON = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");
const py = (code, ...args) => execFileSync(PYTHON, ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = fileURLToPath(new URL(".", import.meta.url));

/* ------------------------------------------------------------ editor protocol --- */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `
;({ frame, unframe, parse, req, Link, parseWav, resample, normalize, rootFromName, buildSlot, makeMockDevice, CMD, SMP,
   CHOP, chopNovelty, chopHits, chopSnap, chopGrid, chopEqual, chopList, chopZones, wavFile, zipStore, crc32,
   UP, bank, capturePatch, auditionPatch, startWatch, libraryFile, readLibraryFile, paramKeys, patternFromSteps, stepsFromPattern, upName,
   mixer, GM_DRUM, drumName, parseNotes, fmtValue, F, DRUM_LANES, LV_NAMES, emptyDrum,
   readDX7File, dx7Message, cleanPatch, packDX7, dx7ForDevice, dx7Init, dx7Name, FM6, fm6Bank,
   DL, refBytes, refFrom, laneFrom, laneBytes, lanesFrom, lanesBytes, kitFrom, kitBytes, kitFile, readKitFile, kitSlots, emptyLane,
   emptySnd, sndBytes, sndFrom, BK, backupFile, readBackupFile, bkPlan, bkReport, bkSlotParts, crc32,
   DRUM_KIT_NAMES })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder });

async function editorMock() {
  const m = E.makeMockDevice();
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 300 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (r, o) => link.request(r, o);
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  ok(info.nengines === 10 && info.engines[9] === "FM6" && info.engines[5] === "VOICE" && info.engines[6] === "TRIO" && info.engines[7] === "WHEEL" && info.engines[8] === "GRAIN" && !info.engines.includes("SUPER") && info.pcount === 72 && info.pe0 === 64 && info.engines[4] === "SAMPLE",
    "editor: INFO");
  let descs = 0;
  for (let i = 0; i < info.pcount; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label) descs++;
  ok(descs === info.pcount, "editor: DESC for every parameter");
  {
    /* the SLICER (core.h P_SLCR..P_SLDEPTH = 45..48, just before P_E0): the mock as params.c has it,
       and a factory preset turns it off as ui.c apply_preset_to does */
    const pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8");
    const sd = [];
    for (let i = 45; i < 49; i++) sd.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(sd.map((d) => d.label).join() === "SLCR,PAT,RATE,DEPTH" && sd[0].names.join() === "OFF,GATE,STUT"
      && sd[2].names.join() === "1/8,1/16,1/32,8T,16T,32T" && sd[2].def === 1 && sd[1].min === 1 && sd[1].max === 16 && sd[3].def === 127
      && /\[P_SLCR\] = PE\("SLCR", N_SLCR, 0\)/.test(pc) && /\[P_SLPAT\] = PD\("PAT", F_INT, 1, 16, 1\)/.test(pc)
      && /\[P_SLRATE\] = PE\("RATE", N_SLDIV, 1\)/.test(pc) && /\[P_SLDEPTH\] = PD\("DEPTH", F_PCT, 0, 127, 127\)/.test(pc)
      && /N_SLDIV\[\] = \{"1\/8", "1\/16", "1\/32", "8T", "16T", "32T"\}/.test(pc),
      "editor: SLICER parameters 45..48 (mock == params.c)");
    await rq(E.req.set(0, 45, 2));
    await rq(E.req.set(0, 46, 7));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 1));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[45] === 2 && on.p[46] === 7 && off.p[45] === 0 && off.p[46] === 1, "editor: a factory preset turns the SLICER off");
  }
  const scale = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 26)));
  const scaleNames = ["CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM", "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"];
  ok(scale.label === "SCL" && scale.max === 15 && eq(scale.names, scaleNames), "editor: all 16 scale names exposed");
  const scaleSet = E.parse[E.CMD.SET](await rq(E.req.set(0, scale.id, 15)));
  ok(scaleSet.value === 15, "editor: new scale selection is not clamped to the old range");
  const dump = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
  ok(dump.p.length === info.pcount && dump.g.length === info.gcount, "editor: DUMP");
  const set = E.parse[E.CMD.SET](await rq(E.req.set(0, 3, 500)));
  ok(set.value === 127, "editor: SET clamps to the range");
  const st = E.parse[E.CMD.STEP_SET](await rq(E.req.stepSet(5, { n: 2, notes: [60, 64], time: 0, flags: 1, vel: 100 })));
  ok(st.n === 2 && st.notes[1] === 64 && st.vel === 100, "editor: STEP_SET");
  const pj = E.parse[E.CMD.PROJECT](await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 }));
  ok(pj.used === 1, "editor: PROJECT save");
  /* sample upload as smpUpload() does it */
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  let rc = E.parse[E.CMD.SMP_BEGIN](await rq(E.req.smpBegin(1), { timeout: 1000, retries: 0 })).rc;
  for (let off = 0; off < data.length && !rc; off += 256) {
    rc = E.parse[E.CMD.SMP_WRITE](await rq(E.req.smpWrite(1, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 })).rc;
  }
  rc = rc || E.parse[E.CMD.SMP_END](await rq(E.req.smpEnd(1, hdr), { timeout: 2000, retries: 0 })).rc;
  const si = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(rc === 0 && si.slots[1].zones === 1 && si.slots[1].name === "TEST", "editor: sample upload (CRC checked by the mock)");
  /* a device that never answers */
  const dead = new E.Link(() => {}, { timeout: 30 });
  const err = await dead.request(E.req.info(), { retries: 1 }).then(() => null, (e) => e.message);
  ok(/^timeout/.test(err || ""), "editor: no reply -> timeout after the retries");
  link.close();
  m.stop();
}

/* ------------------------------------- editor protocol v2: librarian + live --- */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (x) => JSON.stringify(x);
/* a mock + Link pair; counts frames sent per cmd and timeouts reported */
function attachMock(opt, linkOpt = {}) {
  const m = E.makeMockDevice({ auto: false, ...opt });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const sent = {}, ev = { timeouts: 0, unknown: [], pushes: [] };
  const link = new E.Link((d) => { sent[d[4]] = (sent[d[4]] || 0) + 1; out.send(d); }, {
    timeout: 300, onTimeout: () => ev.timeouts++, onUnknown: (f) => ev.unknown.push(f),
    onPush: (f) => ev.pushes.push({ ...f, pending: link.cur ? link.cur.cmd : 0 }), ...linkOpt });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  return { m, link, rq, sent, ev, done: () => { link.close(); m.stop(); } };
}
const emptyStep = { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 };

async function editorLibrarian() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  ok(keys[6] === "PIT" && keys[13] === "PIT#2" && keys[info.pe0] === "E0" && new Set(keys).size === keys.length, "librarian: parameter keys unique (label#n, E0..E7)");

  const b = await E.bank.list(rq);
  ok(b.total === 32 && b.slots.length === 32 && b.slots[1].used && b.slots[1].name === "GLASS BELL" && !b.slots[3].used
    && b.slots[31].slot === 31, "librarian: UP_LIST, 32 slots in 2 frames");

  const cap = (await E.capturePatch(rq, info, "acid test")).patch;
  ok(cap.engine === 0 && cap.p.length === info.pcount && cap.pattern && cap.pattern[0][0] === 45 && cap.pattern[0][1] === 1,
    "librarian: capture = DUMP + first 16 steps");
  let rc = await E.bank.put(rq, 10, cap);
  const g = await E.bank.get(rq, info, 10);
  ok(rc === 0 && g.used && g.name === "acid test" && g.engine === 0 && eq(g.p, cap.p) && js(g.pattern) === js(cap.pattern),
    "librarian: UP_PUT -> UP_GET round trip");
  const bad = E.parse[C.UP_PUT](await rq(E.req.upPut(60, cap), { timeout: 2500, retries: 0 }));
  ok(bad.rc === 1, "librarian: UP_PUT to a slot past the bank -> rc 1");
  ok(E.upName("") === "PATCH" && E.upName("abcdefghijklmnop") === "abcdefghijkl" && E.upName("Bäss") === "Bss", "librarian: device names (ASCII, 1..12)");

  await rq(E.req.set(0, 1, 33));
  rc = await E.bank.store(rq, 11, "STORED");
  const g2 = await E.bank.get(rq, info, 11);
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && g2.name === "STORED" && g2.engine === d1.engine && eq(g2.p, d1.p) && g2.p[1] === 33, "librarian: UP_STORE keeps the current sound");

  /* another engine, empty sequencer, then UP_LOAD brings the sound back, not the pattern (LIVE) */
  await rq(E.req.preset(2, 0));
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  rc = await E.bank.load(rq, 10);
  const d2 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  ok(rc === 0 && d2.engine === 0 && eq(d2.p, cap.p) && st.every((x) => !x.n), "librarian: UP_LOAD applies the sound, the sequencer stays empty (LIVE)");

  rc = await E.bank.erase(rq, 11);
  const b2 = await E.bank.list(rq);
  const rcEmpty = await E.bank.load(rq, 11);
  ok(rc === 0 && !b2.slots[11].used && b2.slots[10].used && rcEmpty === 1, "librarian: UP_ERASE, UP_LOAD of an empty slot -> rc 1");

  /* audition: a PHASE patch with a pattern into an empty sequencer: the sound only (as UP_LOAD in SLOOP), never
     the pattern, and the mix / pattern / key parameters stay (ui.c param_kept) */
  const bass = await E.bank.get(rq, info, 2);   /* PHASE RESO, with the ACID pattern */
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  const KEPT = [0, 25, 26, 27, 29, 30, 31, 32, 39, 40, 49];
  for (const [id, v] of [[0, 77], [25, 5], [29, 12], [39, -10], [49, 2]]) await rq(E.req.set(0, id, v));
  const d2b = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const flashBefore = js(m.state.bank);
  let a = await E.auditionPatch(rq, info, bass, { gEng: 20 });
  const d3 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st3 = [];
  for (let i = 0; i < 16; i++) st3.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const soundOk = (d, pt) => d.p.every((v, i) => (KEPT.includes(i) ? v === d2b.p[i] : v === pt.p[i]));
  ok(!a.wrote && d3.engine === bass.engine && soundOk(d3, bass) && st3.every((s) => !s.n) && js(m.state.bank) === flashBefore
    && d3.p[0] === 77 && d3.p[25] === 5 && d3.p[29] === 12 && d3.p[49] === 2,
    "librarian: audition = G_ENGSEL + SETs of the sound; no pattern, mix, LEN or key; no flash write");
  /* notes in the sequencer stay too */
  await rq(E.req.stepSet(20, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  const bell = await E.bank.get(rq, info, 1);
  a = await E.auditionPatch(rq, info, { ...bell, pattern: [[72, 0], [74, 0]] }, { gEng: 20 });
  const s0 = E.parse[C.STEP_GET](await rq(E.req.stepGet(0))), s20 = E.parse[C.STEP_GET](await rq(E.req.stepGet(20)));
  const d4 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(!a.wrote && !s0.n && s20.notes[0] === 50 && d4.engine === 1 && soundOk(d4, bell), "librarian: audition keeps the sequencer as it is");

  /* ties as the firmware keeps them: no note; a rest has no flags */
  const tied = [[60, 1], [61, 4], [0, 4], [0, 3], [64, 2]];
  const norm = E.patternFromSteps(E.stepsFromPattern(tied));
  rc = await E.bank.put(rq, 12, { ...cap, name: "TIES", pattern: tied });
  const g3 = await E.bank.get(rq, info, 12);
  ok(rc === 0 && js(norm.slice(0, 5)) === js([[60, 1], [0, 4], [0, 4], [0, 0], [64, 2]]) && js(g3.pattern) === js(norm),
    "librarian: pattern ties / rests normalised like the firmware");
  rc = await E.bank.store(rq, 13, "");
  ok(rc === 0 && (await E.bank.get(rq, info, 13)).name === `${info.engines[d4.engine]} 14`, "librarian: UP_STORE with no name -> automatic name");

  /* library files */
  const ctx = { keys, engines: info.engines, firmware: info.version, pe0: info.pe0 };
  const pts = [cap, { ...bass, engineName: info.engines[bass.engine], tags: ["bass", "device"] }];
  const file = JSON.parse(JSON.stringify(E.libraryFile("library", pts, ctx)));
  ok(file.format === "felucca-library" && file.version === 1 && file.pCount === 72 && file.paramLabels.length === 72 && file.engines.length === 10,
    "library file: versioned, with P_COUNT, labels and engines");
  const back = E.readLibraryFile(file, ctx);
  ok(back.patches.length === 2 && !back.skipped && eq(back.patches[0].p, cap.p) && eq(back.patches[1].p, bass.p)
    && js(back.patches[1].pattern) === js(bass.pattern) && back.patches[1].tags.join() === "bass,device" && back.patches[1].engineName === "PHASE",
    "library file: write -> read round trip");
  /* a future firmware: one more parameter at id 5, engines in another order and one of them gone */
  const keys2 = [...keys.slice(0, 5), "NEW", ...keys.slice(5)];
  const eng2 = ["PHASE", "ANALOG", "SAMPLE"];
  const fut = E.readLibraryFile(file, { keys: keys2, engines: eng2 });
  const p0 = fut.patches[0].p;
  ok(fut.patches.length === 2 && p0.length === info.pcount + 1 && p0[5] === null && p0[6] === cap.p[5] && p0[info.pcount] === cap.p[info.pcount - 1]
    && fut.patches[0].engine === 1 && fut.patches[1].engine === 0, "library file: other ids / engine order mapped by label and name");
  const lost = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engineName: "WAVETABLE" }] }, ctx);
  ok(lost.patches.length === 0 && lost.skipped === 1, "library file: a patch for an unknown engine is skipped");
  const bankFile = E.libraryFile("bank", [{ ...g, engineName: "ANALOG", slot: 10 }], ctx);
  ok(bankFile.kind === "bank" && bankFile.patches[0].slot === 10 && E.readLibraryFile(bankFile, ctx).patches[0].slot === 10, "library file: bank export keeps slot numbers");
  const old = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 0, preset: 4, engineName: "ANALOG", presetName: "ACID", p: d2.p, steps: E.stepsFromPattern(cap.pattern) }, ctx);
  ok(old.patches.length === 1 && eq(old.patches[0].p, d2.p) && js(old.patches[0].pattern) === js(cap.pattern), "library file: reads the old \"Save to file\" format");
  let threw = false;
  try { E.readLibraryFile({ format: "something" }, ctx); } catch (e) { threw = true; }
  ok(threw, "library file: unknown format -> error");
  done();
}

/* ------------------------------------------- FM6 engine, DX7 voices / bank --- */
/* FM6 bank / DX7 import tests: ported from Melodee (Kerem Kilic, github.com/keremimo/melodee), GPL-3.0-only;
   SLOOP: engine 10, the bank in a USR sample slot */
async function editorFM6Engine() {
  /* the mock's FM6 engine == firmware/src/eng_fm6.c ENG_FM6 (DESC, titles, presets) */
  const src = readFileSync(join(HERE, "../firmware/src/eng_fm6.c"), "utf8");
  const strs = (name) => [...((new RegExp(`${name}\\[\\] = \\{([^}]*)\\}`).exec(src) || [])[1] || "").matchAll(/"([^"]*)"/g)].map((x) => x[1]);
  const fm6v = strs("N_FM6V"), fm6eng = strs("N_FM6ENG");
  const eb = src.slice(src.indexOf("static const engine_t ENG_FM6"), src.indexOf("};", src.indexOf("static const engine_t ENG_FM6")));
  const edit = [...eb.matchAll(/\{"([^"]*)", F_(\w+), (-?\d+), ([^,]+), (-?\d+), (\w+), 0\}/g)].map((x) => ({ label: x[1], fmt: E.F[x[2]],
    min: +x[3], max: x[4].trim() === "FM6_NVOICE - 1" ? fm6v.length - 1 : +x[4], def: +x[5], names: x[6] === "N_FM6V" ? fm6v : x[6] === "N_FM6ENG" ? fm6eng : null }));
  const titles = [...(/"FM6", \{([^}]*)\}/.exec(eb) || [])[1].matchAll(/"([^"]*)"/g)].map((x) => x[1]);
  const pb = src.slice(src.indexOf("FM6_PRESETS[] = {"), src.indexOf("};", src.indexOf("FM6_PRESETS[] = {")));
  const presets = [...pb.matchAll(/\{"([^"]+)", \{([^}]*)\}, \{([^}]*)\}, (-?\d+), (\d), FX/g)].map((x) => ({ name: x[1],
    e: x[2].split(",").map(Number), env: x[3].split(",").map(Number), mono: +x[5] }));
  ok(fm6v.length === 48 && fm6v[0] === "R01" && fm6v[16] === "U01" && fm6v[47] === "U32" && edit.length === 8 && presets.length === 16,
    "FM6: eng_fm6.c parsed (48 VOICE names, 8 parameters, 16 presets)");

  const { rq, done } = attachMock({});
  const C = E.CMD, info = E.parse[C.INFO](await rq(E.req.info()));
  const fm6 = info.engines.indexOf("FM6");
  await rq(E.req.set(1, 20, fm6));
  const md = [];
  for (let i = 0; i < 8; i++) md.push(E.parse[C.DESC](await rq(E.req.desc(0, info.pe0 + i))));
  ok(fm6 === 9 && !info.engines.includes("DX7") && md.every((d, i) => d.label === edit[i].label && d.fmt === edit[i].fmt && d.min === edit[i].min
    && d.max === edit[i].max && d.def === edit[i].def && (edit[i].names ? eq(d.names, edit[i].names) : true)),
    "FM6: engine 10, the mock's DESC == ENG_FM6 (VOICE R01..U32, ENGINE MARK I)");
  const nm = E.parse[C.NAMES](await rq(E.req.names(fm6)));
  ok(eq(nm.names, presets.map((p) => p.name)) && eq(nm.titles, titles) && eq(titles, ["PATCH", "ENGINE"]), "FM6: preset names and page titles == eng_fm6.c");
  let same = true;
  for (let i = 0; i < presets.length; i++) {
    await rq(E.req.preset(fm6, i));
    const d = E.parse[C.DUMP](await rq(E.req.dump()), info), pr = presets[i];
    same &&= eq(d.p.slice(info.pe0, info.pe0 + 8), pr.e) && eq(d.p.slice(1, 5), pr.env) && (d.p[37] === 2) === !!pr.mono;
  }
  ok(same, "FM6: every preset's VOICE / ENGINE / ADSR / MONO == FM6_PRESETS");
  done();
}

async function editorDX7() {
  const voice = Array(155).fill(0);
  for (let op = 0; op < 6; op++) {
    const o = op * 21;
    for (let i = 0; i < 11; i++) voice[o + i] = (op * 13 + i) % 100;
    voice.splice(o + 11, 10, op % 4, (op + 1) % 4, op + 1, op % 4, 7 - op, 90 - op, op % 2, 31 - op, 42 + op, 14 - op);
  }
  voice.splice(126, 19, 91, 82, 73, 64, 55, 46, 37, 28, 31, 7, 1, 66, 55, 44, 33, 1, 5, 7, 24);
  voice.splice(145, 10, ...Array.from("TEST VOICE", (c) => c.charCodeAt(0)));
  const wrap = (data, bank = false, ch = 0) => [240, 67, ch, ...(bank ? [9, 32, 0] : [0, 1, 27]), ...data,
    (128 - data.reduce((a, b) => a + b, 0) % 128) % 128, 247];
  const single = wrap(voice, false, 15);
  const p = E.readDX7File(single).patches[0];
  ok(p.name === "TEST VOICE" && p.engineName === "FM6" && p.engine === -1 && eq(p.dx7, voice), "DX7: single voice, channel 16, offline import");
  ok(eq(E.dx7Message(p.dx7), wrap(voice)), "DX7: outgoing single-voice data and checksum");
  /* independently pack VCED parameters into Yamaha VMEM bit fields */
  const packed = Array(128).fill(0);
  for (let op = 0; op < 6; op++) {
    const v = voice.slice(op * 21, op * 21 + 21), o = op * 17;
    packed.splice(o, 17, ...v.slice(0, 11), v[11] | v[12] << 2, v[13] | v[20] << 3,
      v[14] | v[15] << 2, v[16], v[17] | v[18] << 1, v[19]);
  }
  packed.splice(102, 26, ...voice.slice(126, 135), voice[135] | voice[136] << 3,
    ...voice.slice(137, 141), voice[141] | voice[142] << 1 | voice[143] << 4, ...voice.slice(144));
  const data = Array.from({ length: 32 }, (_, i) => {
    const v = packed.slice(); v[127] = 65 + i % 26; return v;
  }).flat();
  const ps = E.readDX7File(wrap(data, true), { engines: ["ANALOG", "FM6"] }).patches;
  ok(ps.length === 32 && ps.every((x, i) => x.engine === 1 && eq(x.dx7.slice(0, 154), voice.slice(0, 154))
    && x.dx7[154] === 65 + i % 26), "DX7: all 32 voices unpack every operator and global field");
  ok(E.readDX7File([...single, ...wrap(data, true)]).patches.length === 33, "DX7: concatenated voice and bank dumps");
  const badCheck = single.slice(); badCheck[50] ^= 1;
  const highBit = single.slice(); highBit[50] |= 128;
  const other = single.slice(); other[1] = 66;
  const parameter = single.slice(); parameter[2] = 16;
  ok([[], single.slice(0, -1), badCheck, highBit, other, parameter, [...single, 0], [...single, ...badCheck]]
    .every((bytes) => { try { E.readDX7File(bytes); return false; } catch { return true; } }),
    "DX7: reject empty, truncated, corrupt, foreign and parameter dumps atomically");
  const ctx = { engines: ["ANALOG"], keys: null };
  const file = JSON.parse(JSON.stringify(E.libraryFile("library", [p, ...ps], ctx)));
  const restored = E.readLibraryFile(file, ctx);
  ok(restored.patches.length === 33 && restored.skipped === 0 && eq(restored.patches[0].dx7, voice)
    && eq(E.cleanPatch(p).dx7, voice), "DX7: library export / import keeps dx7 (offline, firmware without FM6)");
  file.patches[0].dx7[0] = 128;
  let badJSON = false;
  try { E.readLibraryFile(file, ctx); } catch { badJSON = true; }
  ok(badJSON, "DX7: invalid voice payload in JSON rejected");
  let writes = 0;
  const err = await E.bank.put(() => { writes++; }, 0, p).then(() => null, (e) => e.message);
  ok(err && !writes, "DX7: regular preset slot cannot silently discard voice data");

  /* audition against the mock: FM6 (engine 10) selected first, then the voice, confirmed by its echo */
  const { m, link, rq, sent, done } = attachMock({});
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  const fm6 = info.engines.indexOf("FM6");
  const flash = js(m.state.bank), pattern = js(m.state.step);
  let outgoing;
  await E.auditionPatch(rq, info, p, { sendDX7: (message) => {
    ok(m.state.engine === fm6, "DX7: FM6 selected before sending the voice"); outgoing = message;
    return link.transferDX7(message);
  } });
  ok(eq(outgoing, wrap(voice)) && js(m.state.bank) === flash && js(m.state.step) === pattern
    && eq(m.state.tracks[m.state.sel].fm6ed, E.dx7ForDevice(voice)),
    "DX7: audition loads one voice into the FM6 part, keeps the bank and the sequence");
  await rq(E.req.track(3));
  const sets = sent[E.CMD.SET];
  const drum = await E.auditionPatch(rq, info, p, { sendDX7: () => { writes++; } }).then(() => null, (e) => e.message);
  ok(drum && sent[E.CMD.SET] === sets && !writes, "DX7: drum track rejected before any changes or voice send");
  const old = { ...info, engines: info.engines.map((e) => (e === "FM6" ? "DX7" : e)) };
  const missing = await E.auditionPatch(rq, old, p, { sendDX7: () => { writes++; } }).then(() => null, (e) => e.message);
  ok(missing && !writes, "DX7: firmware without FM6 cannot audition voices");
  done();

  /* the real import handler with browser File-shaped inputs, a mixed selection */
  let onChange, added = [], status;
  const input = { files: [
    { name: "voice.SYX", arrayBuffer: async () => Uint8Array.from(single).buffer },
    { name: "bank.syx", arrayBuffer: async () => Uint8Array.from(wrap(data, true)).buffer },
    { name: "library.json", text: async () => JSON.stringify(E.libraryFile("library", [p], ctx)) },
  ], addEventListener: (_event, fn) => { onChange = fn; } };
  const handler = html.slice(html.indexOf('$("libfile").addEventListener("change"'), html.indexOf("/* device bank */"));
  vm.runInNewContext(handler, {
    $: () => input, libCtx: () => ctx, readDX7File: E.readDX7File, readLibraryFile: E.readLibraryFile,
    libAdd: async (patches) => { added.push(...patches.map(E.cleanPatch)); },
    sayK: (...args) => { status = args; }, t: () => " patches", console,
  });
  await onChange();
  ok(added.length === 34 && added.every((x) => x.dx7.length === 155) && status[0] === "imported",
    "DX7: Import handler reads mixed .SYX / bank / JSON file selection");
  added = [];
  input.files = [{ name: "broken.syx", arrayBuffer: async () => Uint8Array.from(badCheck).buffer }];
  await onChange();
  ok(!added.length && status[0] === "badfile" && status[1].includes("broken.syx"),
    "DX7: Import handler reports filename and leaves library unchanged for corrupt files");
}

async function editorDX7Transfer() {
  const voice = Array(155).fill(0);
  voice.splice(145, 10, ...Array.from("TEST VOICE", (c) => c.charCodeAt(0)));
  const message = E.dx7Message(voice), oldVoice = voice.slice();
  oldVoice[145] = 79;
  let uploads = 0, reads = 0, confirmed = false, pingEarly = false, device = E.dx7Message(oldVoice);
  const link = new E.Link((data) => {
    if (data[1] === 67 && data.length === 163) {
      uploads++;
      if (uploads > 1) device = data;                // the first upload lost by a busy receiver
    } else if (data[1] === 67 && data[2] === 32) {
      reads++;
      setTimeout(() => { confirmed = uploads > 1; link.receive(device); }, 1);
    } else {
      pingEarly ||= !confirmed;
      setTimeout(() => link.receive(E.frame(E.CMD.PING, [0])), 1);
    }
  });
  const transfer = link.transferDX7(message, { settle: 5, timeout: 15 });
  const ping = link.request(E.req.ping());
  const loaded = await transfer;
  await ping;
  ok(uploads === 2 && reads === 2 && eq(loaded, voice) && !pingEarly && link.idle,
    "DX7 transfer: stale readback retries; editor traffic waits for verified voice");
  link.close();

  let bounded;
  const clampLink = new E.Link((data) => {
    if (data.length === 163) bounded = data;
    else setTimeout(() => clampLink.receive(bounded), 1);
  });
  const normalized = await clampLink.transferDX7(E.dx7Message(Array(155).fill(127)), { settle: 5 });
  ok(normalized[0] === 99 && normalized[11] === 3 && normalized[20] === 14 && normalized[134] === 31
    && normalized[142] === 5 && normalized[144] === 48 && normalized[145] === 126,
    "DX7 transfer: device parameter bounds applied before readback comparison");
  clampLink.close();

  for (const mode of ["missing", "mismatch", "corrupt"]) {
    let count = 0;
    const broken = new E.Link((data) => {
      if (data.length === 163) count++;
      else if (mode !== "missing") {
        const reply = mode === "mismatch" ? E.dx7Message(oldVoice) : message.slice();
        if (mode === "corrupt") reply[161] ^= 1;
        setTimeout(() => broken.receive(reply), 1);
      }
    });
    const err = await broken.transferDX7(message, { settle: 2, timeout: 8, retries: 1 }).then(() => "", (e) => e.message);
    ok(count === 2 && /not confirmed/.test(err) && broken.idle, `DX7 transfer: ${mode} readback reports failure after bounded retries`);
    broken.close();
  }
  let count = 0;
  const closed = new E.Link(() => count++);
  const pending = closed.transferDX7(message, { settle: 10 }).catch((e) => e.message);
  closed.close();
  await sleep(20);
  ok(await pending === "closed" && count === 1, "DX7 transfer: disconnect cancels delayed dump request");
  /* Yamaha frames do not count as WATCH keep-alive traffic (only editor requests keep it on the device) */
  const quiet = new E.Link((data) => { if (data.length === 5) setTimeout(() => quiet.receive(message), 1); });
  quiet.lastSent = 1;
  await quiet.yamaha(null, [0xF0, 0x43, 0x20, 0, 0xF7], (d) => d, "no voice");
  ok(quiet.lastSent === 1, "DX7 transfer: Yamaha frames leave the WATCH keep-alive clock alone");
  quiet.close();
}

async function editorFM6Bank() {
  /* VMEM packing: the inverse of the bank unpacker, for every field at its limits and in between */
  let seed = 7;
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF) % 128;
  const voices = Array.from({ length: 32 }, (_, i) => E.dx7ForDevice(Array.from({ length: 155 }, () => (i === 0 ? 127 : i === 1 ? 0 : rnd()))));
  const bank = [].concat(...voices.map(E.packDX7));
  const back = E.readDX7File(E.fm6Bank.message(bank)).patches.map((p) => p.dx7);
  ok(bank.length === 4096 && bank.every((v) => v < 128) && back.every((v, i) => eq(v, voices[i])),
    "FM6 bank: VMEM pack round-trips every field (32 voices, limits)");
  const init = E.dx7Init();
  ok(init.length === 155 && E.dx7Name(init) === E.FM6.INIT && eq(E.dx7ForDevice(init), init), "FM6 bank: init voice in range, named INIT VOICE");
  let b = E.fm6Bank.init();
  ok(E.fm6Bank.isEmpty(b) && b.length === 4096, "FM6 bank: the device's bank without one in flash = 32 INIT VOICE");
  b = E.fm6Bank.withVoice(b, 0, voices[2]);
  b = E.fm6Bank.withVoice(b, 3, voices[3]);
  const free = E.fm6Bank.free(b), from30 = E.fm6Bank.free(b, 30);
  ok(free[0] === 1 && free[1] === 2 && !free.includes(3) && free.length === 30 && from30[0] === 30 && from30[2] === 1 && !E.fm6Bank.isEmpty(b)
    && eq(E.fm6Bank.voice(b, 3), voices[3]) && E.fm6Bank.read(E.fm6Bank.message(b)).every((v, i) => v === b[i]),
    "FM6 bank: free slots (INIT VOICE, wrapping), slot voices, dump parse");
  ok(E.fm6Bank.read(E.fm6Bank.message(b).map((v, i) => (i === 4102 ? v ^ 1 : v))) === undefined
    && E.fm6Bank.read(E.dx7Message(init)) === undefined, "FM6 bank: corrupt or foreign dumps rejected");

  /* against the mock device: read, write one slot, then that slot on an FM6 track */
  const { m, link, rq, done } = attachMock({});
  const C = E.CMD, info = E.parse[C.INFO](await rq(E.req.info()));
  const dev = await link.readFM6Bank();
  const names = E.fm6Bank.names(dev);
  ok(names[0] === "MOCK BRASS" && names[4] === "MOCK PIANO" && E.fm6Bank.free(dev)[0] === 2, "FM6 bank: mock bank read as one 32-voice dump");
  const smp = E.parse[C.SMP_INFO](await rq(E.req.smpInfo()));
  const magic = (k) => String.fromCharCode(...m.state.smp[k].flash.subarray(0, 4));
  const bankMagic = () => String.fromCharCode(...m.state.fm6area.subarray(0, 4));
  const noSlotBank = () => [0, 1, 2].every((k) => magic(k) !== "FM6B");
  ok(bankMagic() === "FM6B" && noSlotBank() && smp.slots.every((s) => s.zones === 0),
    "FM6 bank: kept in the banks area (0xD8000, magic FM6B), no USR slot used");
  const voice = E.dx7ForDevice([...voices[5].slice(0, 145), ...Array.from("KEPT VOICE", (c) => c.charCodeAt(0))]);
  const wrote = await link.writeFM6Bank(E.fm6Bank.withVoice(dev, 2, voice));
  ok(E.fm6Bank.names(wrote)[2] === "KEPT VOICE" && E.fm6Bank.names(await link.readFM6Bank())[1] === "MOCK BELLS"
    && bankMagic() === "FM6B" && noSlotBank(), "FM6 bank: one slot written in place, the rest kept, confirmed by readback");
  const fm6 = info.engines.indexOf("FM6");
  await rq(E.req.set(1, 20, fm6));
  const vd = E.parse[C.DESC](await rq(E.req.desc(0, info.pe0)));
  await rq(E.req.set(0, info.pe0, vd.max + 1 - E.FM6.SLOTS + 2));      /* U03: the bank comes after the 16 factory voices */
  const playing = await link.yamaha(null, [0xF0, 0x43, 0x20, 0, 0xF7], (d) => (d.length === 163 ? d.slice(6, 161) : undefined), "no voice");
  ok(fm6 >= 0 && vd.max + 1 - E.FM6.SLOTS === E.FM6.NROM && eq(Array.from(playing), voice), "FM6 bank: VOICE U03 plays the stored voice");

  /* the bank has its own place: a sample upload into any slot keeps it */
  const kept = await link.readFM6Bank();
  E.parse[C.SMP_BEGIN](await rq(E.req.smpBegin(0)));
  ok(eq(await link.readFM6Bank(), kept) && !E.fm6Bank.isEmpty(kept), "FM6 bank: an upload into a USR slot keeps the bank");
  /* three samples: the bank is still stored, the samples kept */
  m.state.smp.forEach((u, k) => { u.flash.set([0x46, 0x53, 0x4D, 0x50], 0); u.zones = 1; u.name = "S" + k; u.len = 1024; });
  const again = await link.writeFM6Bank(b);
  ok(eq(again, b) && m.state.msg === "FM6 BANK SAVED" && m.state.smp.every((u, k) => u.zones === 1 && u.name === "S" + k),
    "FM6 bank: three samples in the USR slots, the bank still stored (FM6 BANK SAVED), samples kept");
  const fs = readFileSync(join(HERE, "../firmware/src/fm6_store.c"), "utf8"), es = readFileSync(join(HERE, "../firmware/src/eng_sample.c"), "utf8");
  ok(/0x42364D46u\s+\/\* "FM6B" \*\//.test(fs) && /FM6_BANK_OFF 0x1000u/.test(fs) && /#define FM6_HDR SMP_BANKS/.test(fs) &&
    /#define SMP_BANKS 0xD8000u/.test(es), "FM6 bank: the magic, offset and place (0xD8000) == fm6_store.c / eng_sample.c");
  done();

  /* the device busy with the flash write: its first readback request is lost, the retry gets it */
  let sends = 0, asks = 0, stored = null;
  const busyLink = new E.Link((d) => {
    if (d.length === 4104) { sends++; stored = d.slice(6, 4102); }
    else if (d[2] === 0x20 && ++asks > 1) setTimeout(() => busyLink.receive(E.fm6Bank.message(stored)), 1);
  });
  const res = await busyLink.writeFM6Bank(b, { settle: 2, timeout: 10 });
  ok(sends === 2 && asks === 2 && eq(res, b) && busyLink.idle, "FM6 bank: a lost readback request retries the write");
  const wrong = new E.Link((d) => { if (d.length === 5) setTimeout(() => wrong.receive(E.fm6Bank.message(dev)), 1); });
  const err = await wrong.writeFM6Bank(b, { settle: 2, timeout: 10, retries: 1 }).then(() => "", (e) => e.message);
  ok(/not confirmed/.test(err) && wrong.idle, "FM6 bank: another bank read back reports failure");
  busyLink.close(); wrong.close();
}

async function editorLive() {
  const C = E.CMD;
  const { m, link, rq, sent, ev, done } = attachMock({ watchMs: 250 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(await E.startWatch(rq), "live: WATCH on");

  /* a push between a request and its reply */
  const pend = rq(E.req.dump());
  const kn = m.sim.knob(9, 4);
  const dump = E.parse[C.DUMP](await pend, info);
  const ch = ev.pushes.find((f) => f.cmd === C.CHANGED);
  const cv = ch && E.parse[C.CHANGED](ch.a);
  ok(dump.p.length === 72 && ch && ch.pending === C.DUMP && cv.scope === 0 && cv.id === 9 && cv.value === kn.value && !ev.unknown.length,
    "live: CHANGED while DUMP waits -> push handler, reply still matched");
  const rl = m.sim.reload();
  m.sim.step(3);
  const pend2 = rq(E.req.stepGet(7));
  const s7 = E.parse[C.STEP_GET](await pend2);
  await sleep(10);
  const r = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(s7.index === 7 && r && E.parse[C.RELOAD](r.a).preset === rl.preset && sc && E.parse[C.STEP_CHANGED](sc.a).index === 3,
    "live: RELOAD and STEP_CHANGED routed");
  const nr = ev.pushes.filter((f) => f.cmd === C.RELOAD).length;
  await rq(E.req.preset(1, 2));
  await rq(E.req.stepSet(9, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await sleep(10);
  ok(ev.pushes.filter((f) => f.cmd === C.RELOAD).length === nr + 1 && !ev.pushes.some((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9),
    "live: RELOAD after an editor PRESET too, nothing after its STEP_SET");

  /* PING keeps the watch on; without requests it ends */
  for (let i = 0; i < 4; i++) { await sleep(120); await rq(E.req.ping()); }
  let n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0 + 1, "live: PING keeps WATCH on");
  await sleep(320);
  n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0, "live: WATCH ends by itself without requests");

  /* a slider drag: 40 values at once -> one SET in flight + one coalesced, the last value wins */
  const before = sent[C.SET] || 0;
  const all = [];
  for (let v = 0; v < 40; v++) all.push(rq(E.req.set(0, 9, v * 3), { key: "0:9" }));
  await Promise.all(all);
  ok((sent[C.SET] || 0) - before === 2 && m.state.p[9] === 117 && link.idle, "live: drag SETs coalesce (2 frames for 40 values, latest kept)");
  ok(ev.timeouts === 0, "live: no timeouts");
  done();

  /* older firmware: no reply to WATCH -> false without a "no reply" message; no bank */
  const o = attachMock({ legacy: true });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const w = await o.rq(E.req.watch(1), { timeout: 60, retries: 0, quiet: true }).then(() => true, () => false);
  const sw = await E.startWatch(o.rq);
  const bl = await E.bank.list((rr, oo) => o.rq(rr, { ...oo, timeout: 60, quiet: true })).then(() => "listed", (e) => e.message);
  ok(!w && !sw && /^timeout/.test(bl) && o.ev.timeouts === 0, "live: older firmware -> WATCH unanswered (fall back to polling), no bank");
  o.done();
}

/* ------------------------------------------------------ editor protocol v3: tracks --- */
async function editorTracks() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(info.ntrk === 4 && tr.sel === 0 && tr.ntrk === 4 && tr.tracks[0].engine === 0 && tr.tracks[1].engine === 1
    && tr.tracks[3].engine === info.nengines, "tracks: INFO NTRK, TRACK lists 4 (track 4 = drums, engine NENGINES)");
  /* the v1 commands follow the selected track */
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const t1 = E.parse[C.TRACK](await rq(E.req.track(1)));
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(0, 1, 77));
  const td0 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(0)), info);
  const td1 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(1)), info);
  ok(t1.sel === 1 && d1.engine === 1 && d0.engine === 0 && td1.p[1] === 77 && td0.p[1] === d0.p[1] && td0.p[1] !== 77,
    "tracks: TRACK selects; DUMP / SET act on it, TRACK_DUMP reads any track");
  /* steps of a track that is not selected */
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 1, vel: 99 })));
  const g2 = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const s1 = E.parse[C.STEP_GET](await rq(E.req.stepGet(5)));
  ok(w.track === 2 && g2.n === 2 && g2.notes[1] === 67 && g2.vel === 99 && s1.n === 0, "tracks: TRACK_STEP set / get on another track");
  /* level / mute; the drum level is GLO > DRUMS LEVEL */
  const mx = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0, 90, 1)));
  const mxd = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(3, 64, 0)));
  const mx2 = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0)));
  const tr2 = E.parse[C.TRACK](await rq(E.req.track()));
  ok(mx.level === 90 && mx.mute === 1 && mx2.level === 90 && mxd.level === 64 && m.state.g[25] === 64
    && tr2.tracks[0].level === 90 && tr2.tracks[0].mute === 1, "tracks: TRACK_MIX level / mute (drums: G_DRLVL)");
  /* the drum track: no sound to store or load */
  await rq(E.req.track(3));
  const dd = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const us = E.parse[C.UP_STORE](await rq(E.req.upStore(20, "X"), { timeout: 2500, retries: 0 }));
  const ul = E.parse[C.UP_LOAD](await rq(E.req.upLoad(1), { timeout: 2500, retries: 0 }));
  ok(dd.engine === info.nengines && us.rc === 1 && ul.rc === 1, "tracks: drum track selected -> DUMP engine NENGINES, UP_STORE / UP_LOAD rc 1");
  /* pushes carry the selected track */
  await rq(E.req.track(0));
  ok(await E.startWatch(rq), "tracks: WATCH on");
  m.sim.track(2);
  m.sim.step(4);
  await sleep(10);
  const rl = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(rl && E.parse[C.RELOAD](rl.a).track === 2 && sc && E.parse[C.STEP_CHANGED](sc.a).track === 2 && E.parse[C.STEP_CHANGED](sc.a).index === 4,
    "tracks: RELOAD / STEP_CHANGED carry the selected track");
  /* projects keep all four tracks */
  await rq(E.req.project(1, 3), { timeout: 4000, retries: 0 });
  await rq(E.req.trackStep(2, 5, { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 }));
  await rq(E.req.track(0));
  await rq(E.req.project(0, 3), { timeout: 4000, retries: 0 });
  const back = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track())).sel;
  ok(back.n === 2 && back.notes[0] === 60 && sel === 2, "tracks: PROJECT save / load keeps every track and the selection");
  /* older firmware: no NTRK in INFO, no RELOAD track byte */
  const o = attachMock({ legacy: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  ok(oi.ntrk === 0 && E.parse[C.RELOAD]([0, 4]).track === 0 && E.parse[C.STEP_CHANGED]([7]).track === 0, "tracks: older firmware parses (no tracks)");
  o.done();
  done();
}

/* ------------------------------------------------ editor v3: the mixer (Tracks tab) --- */
async function editorMixer() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const PAN = 39, MUTE = 40;
  const m0 = await E.mixer.read(rq, info, { pan: PAN });
  ok(m0.ntrk === 4 && m0.tracks.length === 4 && m0.tracks[1].pan === -24 && m0.tracks[2].pan === 20 && m0.tracks[3].engine === info.nengines
    && m0.tracks.every((x) => Number.isInteger(x.level) && (x.mute === 0 || x.mute === 1)), "mixer: read = TRACK + pan of every track (TRACK_DUMP)");
  /* level / mute of a track that is not selected, and of the drum track (G_DRLVL) */
  const a = await E.mixer.setMix(rq, 2, 70, 1);
  const b = await E.mixer.setMix(rq, 3, 200, 0);
  const m1 = await E.mixer.read(rq, info, { pan: PAN });
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(a.level === 70 && a.mute === 1 && b.level === 127 && m.state.g[25] === 127 && m1.tracks[2].level === 70 && m1.tracks[2].mute === 1
    && td2.p[0] === 70 && td2.p[MUTE] === 1 && m1.sel === 0, "mixer: TRACK_MIX level / mute round trip (drums: G_DRLVL, clamped)");
  /* pan of another track: selected for the SET, the selection put back, no RELOAD pushed */
  ok(await E.startWatch(rq), "mixer: WATCH on");
  const pushes = ev.pushes.length;
  /* (the v3 path: firmware 0.8 has no TRACK_PARAM) */
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40);
  const p0 = await E.mixer.setPan(rq, 0, 0, PAN, 99);
  await sleep(10);
  const m2 = await E.mixer.read(rq, info, { pan: PAN });
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(p2 === -40 && p0 === 63 && m2.sel === 0 && m2.tracks[2].pan === -40 && m2.tracks[0].pan === 63 && d0.p[PAN] === 63 && m2.tracks[1].pan === -24
    && ev.pushes.length === pushes, "mixer: pan of any track via SET (other track selected for a moment, then back; no push)");
  /* the device's TRACKS page: level of the selected track pushes CHANGED; REC arm shows in TRACK */
  m.sim.level(33);
  m.sim.arm(1);
  await sleep(10);
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  const m3 = await E.mixer.read(rq, info, { pan: PAN });
  ok(ch && ch.scope === 0 && ch.id === 0 && ch.value === 33 && m3.tracks[0].level === 33 && m3.tracks[1].armed === 1 && m3.tracks[0].armed === 0,
    "mixer: device-side level (CHANGED push) and REC arm read back");
  await rq(E.req.track(3));
  m.sim.level(90);
  await sleep(10);
  const chd = E.parse[C.CHANGED](ev.pushes.filter((f) => f.cmd === C.CHANGED).pop().a);
  ok(chd.scope === 1 && chd.id === 25 && chd.value === 90, "mixer: drum level on the device pushes G_DRLVL");
  /* the drum track's steps: GM notes, shown and typed by name */
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, i))));
  const names = st[0].notes.slice(0, st[0].n).map(E.drumName).join(" ");
  ok(names === "KICK CHH" && st[4].notes.slice(0, 2).map(E.drumName).join(" ") === "SNARE CHH" && E.drumName(20) === "20",
    "mixer: the mock drum track holds a GM pattern (KICK CHH ...)");
  ok(JSON.stringify(E.parseNotes("kick CHH 49")) === "[36,42,49]" && JSON.stringify(E.parseNotes("C4 SNARE")) === "[60,38]" && E.parseNotes("KICKS") === null
    && Object.keys(E.GM_DRUM).length === 47 && new Set(Object.values(E.GM_DRUM)).size === 47, "mixer: GM drum names parse (unique, 35..81)");
  done();
}

/* ------------------------------------- editor v4: TRACK_PARAM and TRACK_CHANGED --- */
async function editorTrackParam() {
  const C = E.CMD, PAN = 39, MUTE = 40;
  const { m, rq, sent, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const w1 = E.parse[C.WATCH](await rq(E.req.watch(1)));
  m.sim.param(2, PAN, 11);
  await sleep(10);
  ok(w1.on === 1 && !ev.pushes.some((f) => f.cmd === C.TRACK_CHANGED), "v4: WATCH 1 answers 1 as before (no TRACK_CHANGED pushes)");
  ok(await E.startWatch(rq) === 3, "v4: WATCH 3 -> 3 (TRACK_PARAM / TRACK_CHANGED known)");
  const tracks0 = sent[C.TRACK] || 0, pushes = ev.pushes.length;
  const g = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, PAN)));
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40, true);
  const p1 = await E.mixer.setPan(rq, 1, 0, PAN, 99, true);
  const alg = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, info.pe0, 50)));   /* track 2 is DIGITAL: ALG 0..7 */
  const lv = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(3, 0, -5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track()));
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  await sleep(10);
  ok(g.track === 1 && g.id === PAN && g.value === -24 && p2 === -40 && p1 === 63 && alg.value === 7 && lv.value === 0 && td2.p[PAN] === -40
    && sel.sel === 0 && (sent[C.TRACK] || 0) === tracks0 + 1 && ev.pushes.length === pushes,
    "v4: TRACK_PARAM get / set on other tracks (clamped as SET, selection kept, no push)");
  const bad = await rq(E.req.trackParam(4, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(bad === "none", "v4: TRACK_PARAM of track 5: no reply");
  /* device-side changes: CHANGED for the selected track, TRACK_CHANGED for the others */
  m.sim.param(2, PAN, 30);
  m.sim.param(3, MUTE, 1);
  m.sim.param(0, PAN, -7);
  await sleep(10);
  const tc = ev.pushes.filter((f) => f.cmd === C.TRACK_CHANGED).map((f) => E.parse[C.TRACK_CHANGED](f.a));
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  ok(tc.length === 2 && tc[0].track === 2 && tc[0].id === PAN && tc[0].value === 30 && tc[1].track === 3 && tc[1].id === MUTE && tc[1].value === 1
    && ch && ch.scope === 0 && ch.id === PAN && ch.value === -7 && !ev.unknown.length, "v4: TRACK_CHANGED pushes for the other tracks, CHANGED for the selected one");
  done();
  /* firmware 0.8 (v3): WATCH 3 answers 1, TRACK_PARAM unanswered: the editor keeps the select / restore path */
  const o = attachMock({ v3: true, watchMs: 1000 });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const on = await E.startWatch(o.rq);
  const tp = await o.rq(E.req.trackParam(1, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const pv = await E.mixer.setPan(o.rq, 2, 0, PAN, 5, false);
  ok(on === 1 && tp === "none" && pv === 5 && o.ev.timeouts === 0 && !o.ev.unknown.length, "v4: v3 firmware -> WATCH 1, no TRACK_PARAM (pan by select / restore)");
  o.done();
}

/* ------------------------------------- editor v5 (SLOOP 2.0): lanes, levels, ratchets --- */
async function editorV5() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 6 && info.uids.length === info.nengines && /SLOOP/.test(info.version) && info.pcount === 72 && info.gcount === 32 && info.pe0 === 64, "v5/v6: INFO ends with the protocol version (6) and the engine UIDs");
  /* the firmware says the same: ED_DRUM_STEP is command 33, INFO sends 5, P_CHORD / the master globals as the mock has them */
  const ec = readFileSync(join(HERE, "../firmware/src/editor.c"), "utf8"), pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8");
  const en = (/enum \{ ED_INFO = 1,([^}]*)\}/.exec(ec) || [])[1] || "";
  const names = ["ED_INFO", ...en.replace(/\/\*[^*]*\*\//g, "").split(",").map((x) => x.trim()).filter(Boolean)];
  ok(names.indexOf("ED_DRUM_STEP") + 1 === C.DRUM_STEP && names.indexOf("ED_TRACK_CHANGED") + 1 === C.TRACK_CHANGED
    && /ed_b\(6\);\s*\/\* v5: the protocol version/.test(ec) && /ED_BUILD = 49/.test(ec) && C.BUILD === 49, "v5: command numbers and INFO == editor.c");
  const enumNames = (id) => (new RegExp(`${id}\\[\\] = \\{([^}]*)\\}`).exec(pc) || [])[1].split(",").map((x) => x.trim().replace(/"/g, ""));
  const chord = E.parse[C.DESC](await rq(E.req.desc(0, 49)));
  const gd = [];
  for (let i = 27; i < 32; i++) gd.push(E.parse[C.DESC](await rq(E.req.desc(1, i))));
  ok(chord.label === "CHORD" && chord.names.join() === enumNames("N_CHORD").join() && /\[P_CHORD\] = PE\("CHORD", N_CHORD, 0\)/.test(pc)
    && gd.map((d) => d.label).join() === "DUST,DUCK,FILT,ROLL,NEW" && gd[3].names.join() === enumNames("N_ROLL").join() && gd[3].def === 1
    && /\[G_DUST\] = PD\("DUST", F_PCT, 0, 127, 0\)/.test(pc) && /\[G_FILT\] = PD\("FILT", F_FILT, -64, 63, 0\)/.test(pc)
    && /\[G_ROLL\] = PE\("ROLL", N_ROLL, 1\)/.test(pc) && /\[G_NEWPRJ\] = PE\("NEW", N_GO, 0\)/.test(pc),
    "v5: CHORD, DUST, DUCK, FILT, ROLL, NEW (mock == params.c)");
  /* the value formats (params.c fmt_value) */
  const fv = (fmt, v, max = 127, min = 0) => E.fmtValue({ fmt, min, max, names: [] }, v).join("");
  ok(fv(E.F.SWING, 0, 100) === "50%" && fv(E.F.SWING, 50, 100) === "63%" && fv(E.F.SWING, 100, 100) === "75%"
    && fv(E.F.FILT, 0, 63, -64) === "OFF" && fv(E.F.FILT, -64, 63, -64) === "LP100%" && fv(E.F.FILT, -32, 63, -64) === "LP50%" && fv(E.F.FILT, 63, 63, -64) === "HP100%"
    && fv(E.F.PCT, 127) === "100%" && fv(E.F.PCT, 64) === "50%" && fv(E.F.PCT, 60, 120) === "50%", "v5: SWING 50..75 %, FILT LP / OFF / HP, PCT of the range");
  /* a synth step keeps its levels and ratchets; an old-style write clears them (as the firmware) */
  const w = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 0, vel: 100, lvl: 0b11000110, rat: 0b10000011 })));
  const g = E.parse[C.STEP_GET](await rq(E.req.stepGet(3)));
  const w2 = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 })));
  ok(w.lvl === 0b11000110 && g.lvl === 0b11000110 && g.rat === 0b10000011 && w2.lvl === 0 && w2.rat === 0 && E.req.stepSet(1, { n: 0, notes: [], time: 2, flags: 0, vel: 0 })[1].length === 9,
    "v5: STEP_SET / STEP_GET level and ratchet bytes (8 bits each), none for an old step");
  /* the drum track: 16 lanes with their level and ratchet; lane 15 uses the top bits */
  const d = E.emptyDrum();
  d.on = (1 << 0) | (1 << 4) | (1 << 15); d.lvl[0] = 3; d.lvl[4] = 1; d.rat[4] = 3; d.lvl[15] = 2; d.rat[15] = 1;
  const ds = E.parse[C.DRUM_STEP](await rq(E.req.drumStep(5, d)));
  const dg = E.parse[C.DRUM_STEP](await rq(E.req.drumStep(5)));
  ok(ds.index === 5 && dg.on === d.on && dg.lvl.join() === d.lvl.join() && dg.rat.join() === d.rat.join()
    && E.req.drumStep(5, d)[1].every((b) => b >= 0 && b < 128) && E.req.drumStep(5, d)[1].length === 14, "v5: DRUM_STEP round trip (16 lanes, levels, ratchets)");
  /* old commands see the lanes as GM notes (the first four, ACC when one is hard); an old write lands on lanes */
  const old = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, 5)));
  await rq(E.req.trackStep(3, 6, { n: 2, notes: [38, 46, 0, 0], time: 0, flags: 1, vel: 100 }));
  const d6 = E.parse[C.DRUM_STEP](await rq(E.req.drumStep(6)));
  ok(old.n === 3 && old.notes.slice(0, 3).join() === "36,42,56" && old.flags === 1 && d6.on === ((1 << 2) | (1 << 5)) && d6.lvl[2] === 3 && d6.lvl[5] === 3,
    "v5: the drum track through TRACK_STEP: lanes <-> GM notes");
  ok(E.DRUM_LANES.length === 16 && E.DRUM_LANES[0][0] === 36 && E.DRUM_LANES[15][0] === 56 && E.LV_NAMES.join() === "NORM,GHOST,SOFT,HARD",
    "v5: the 16 lanes (kick .. cowbell), 4 levels");
  const dc = readFileSync(join(HERE, "../firmware/src/drums.c"), "utf8");
  const lanes = ((/LANE_NOTE\[DRUM_LANES\] = \{([^}]*)\}/.exec(dc) || [])[1] || "").split(",").map((x) => +x);
  ok(lanes.join() === E.DRUM_LANES.map((x) => x[0]).join(), "v5: lane notes == drums.c LANE_NOTE");
  /* the kit: the drum track's P_E0 */
  await rq(E.req.track(3));
  const kit = E.parse[C.DESC](await rq(E.req.desc(0, info.pe0)));
  const dk = (/DRUM_KIT_NAMES\[\] = \{([^}]*)\}/.exec(dc) || [])[1] || "";
  const gen = join(HERE, "../build/gen/felucca_drumkits.h");
  const dsList = existsSync(gen) ? ((/#define DS_KIT_NAME_LIST (.*)/.exec(readFileSync(gen, "utf8")) || [])[1] || "") : null;
  const fwKits = dsList == null ? null : dk.replace("DS_KIT_NAME_LIST", dsList).split(",").map((x) => x.trim().replace(/"/g, ""));
  ok(kit.label === "KIT" && kit.names[5] === "808" && kit.names.length === kit.max + 1 && (!fwKits || fwKits.join() === kit.names.join()),
    `v5: the drum track's KIT (${kit.names.length} kits${fwKits ? ", == drums.c" : ""})`);
  /* TRACK ends with the solo mask */
  m.state.solo = 0b0101;
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(tr.solo === 5 && tr.sel === 3 && tr.tracks.length === 4, "v5: TRACK reports the soloed tracks");
  ok(!ev.unknown.length, "v5: no unmatched replies");
  done();
  /* firmware 0.8 (v3): no DRUM_STEP, no protocol byte, steps without the extra bytes */
  const o = attachMock({ v3: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  const os = E.parse[C.STEP_GET](await o.rq(E.req.stepGet(0)));
  const nd = await o.rq(E.req.drumStep(0), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(oi.proto === 0 && os.lvl === undefined && nd === "none", "v5: older firmware -> proto 0, no DRUM_STEP (the editor keeps GM notes)");
  o.done();
}

/* ------------------------------------------- drum lanes and user kits (36..42) --- */
async function editorDrums() {
  const C = E.CMD;
  const ed = readFileSync(join(HERE, "../firmware/src/ed_drums.c"), "utf8"), de = readFileSync(join(HERE, "../firmware/src/drum_edit.c"), "utf8");
  ok(/ED_DRUM_LANES = 36, ED_DRUM_LANE, ED_UKIT_LIST, ED_UKIT_GET, ED_UKIT_PUT, ED_UKIT_OP, ED_SMP_READ/.test(ed)
    && C.DRUM_LANES === 36 && C.DRUM_LANE === 37 && C.UKIT_LIST === 38 && C.UKIT_GET === 39 && C.UKIT_PUT === 40 && C.UKIT_OP === 41 && C.SMP_READ === 42,
  "drums: command numbers 36..42 == ed_drums.c");
  const arr = (name) => ((new RegExp(`${name}\\[DE_N\\] = \\{([^}]*)\\}`).exec(de) || [])[1] || "").split(",").map((x) => +x);
  ok(arr("DE_MIN").join() === E.DL.MIN.join() && arr("DE_MAX").join() === E.DL.MAX.join() && /sizeof\(dlanes_t\) == 204u/.test(de)
    && /DE_TUNE, DE_DECAY, DE_SNAP, DE_CLICK, DE_BEND, DE_CUT, DE_DRIVE, DE_LEVEL/.test(de),
  "drums: the 8 offsets' order and ranges == drum_edit.c, 204-byte lanes");
  /* the byte layouts (drum_edit.c dl_set_ref, dlanes_t; drum_kits.c ukit_t) */
  const rb = E.refBytes({ hit: 9, start: 1000, len: 1023 }), rf = E.refFrom(...rb);
  ok(rf.hit === 9 && rf.start === 1000 && rf.len === 1023 && E.refFrom(...E.refBytes({ hit: 2, start: 0, len: 1024 })).len === 1024
    && js(E.refBytes({ hit: 2, start: 0, len: 1024 })) === js([0x20, 0, 0]), "drums: a hit / start / length in 3 bytes (1024 = 0)");
  const TRK = { rev: -1, dly: 0, cho: 0 };          /* (a lane's sends read from version 1 bytes) */
  const lanes = Array.from({ length: 16 }, (_, l) => ({ ofs: [l - 8, 63, -64, 5, -24, 0, 1, 6], src: [0, 1, 2, 3, 16, 52][l % 6], hit: l, start: l * 60,
    len: 1024 - l, snd: TRK }));
  const blk = { lanes, ukit: 7, name: "MY KIT" };
  const lb = E.lanesBytes(blk), back = E.lanesFrom(Uint8Array.from(lb));
  ok(lb.length === 204 && js(back) === js(blk) && lb[128 + 4] === 16 && lb[192] === 7, "drums: the 204 lane bytes round trip (dlanes_t layout)");
  const kit = { used: true, base: 6, lanes };
  const kb = E.kitBytes(kit), kback = E.kitFrom(Uint8Array.from(kb));
  ok(kb.length === 196 && kb[0] === 0xA5 && kb[1] === 6 && js(kback) === js(kit), "drums: a kit's 196 bytes round trip (ukit_t layout, no name)");
  const old = Uint8Array.from([...kb.slice(0, 2), ...Array.from("OLDNAME\0", (c) => c.charCodeAt(0)), ...kb.slice(2)]);
  ok(old.length === 204 && js(E.kitFrom(old)) === js(kit), "drums: an older kit's 204 bytes (with a name) read, the name dropped");
  const dk = readFileSync(join(HERE, "../firmware/src/drum_kits.c"), "utf8");
  ok(/sizeof\(ukit_t\) == 196u/.test(dk) && /0x33424B44u\s+\/\* "DKB3" \*\//.test(dk), "drums: 196-byte kits, bank DKB3 == drum_kits.c");
  /* version 2: each lane's sends after the lanes / the kit (REV -1 = TRK, 0..31; DLY, CHO 0..31) */
  const lanes2 = lanes.map((l, i) => ({ ...l, snd: { rev: i % 3 ? i : -1, dly: (i * 5) % 32, cho: 31 - i } }));
  const lb2 = E.lanesBytes({ ...blk, lanes: lanes2 }, true), kb2 = E.kitBytes({ ...kit, lanes: lanes2 }, true);
  ok(lb2.length === 252 && js(E.lanesFrom(Uint8Array.from(lb2))) === js({ ...blk, lanes: lanes2 }) && lb2[204] === 0xFF && lb2[204 + 3] === 1 &&
    kb2.length === 244 && js(E.kitFrom(Uint8Array.from(kb2))) === js({ ...kit, lanes: lanes2 }) && js(lb2.slice(0, 204)) === js(lb),
    "drums v2: lanes (252) / kit (244): + 48 bytes of sends round trip, the first 204 as version 1");
  ok(js(E.sndFrom(E.sndBytes({ rev: 99, dly: -4, cho: 40 }), 0)) === js({ rev: 31, dly: 0, cho: 31 }) && js(E.sndBytes(E.emptySnd())) === js([0xFF, 0, 0]),
    "drums v2: sends clamped (REV -1..31), TRK = 0xFF");
  ok(E.req.drumLanes(null, true)[1].length === 1 && E.req.drumLanes({ ...blk, lanes: lanes2 }, true)[1].length === 289 &&
    E.req.drumLanes({ ...blk, lanes: lanes2 })[1].length === 234 && E.req.drumLane(3, null, true)[1][0] === 0x43 && E.req.ukitGet(4, true)[1][0] === 0x44,
    "drums v2: requests (36: 2 / 2 + 288 bytes; 37 / 39 / 40: 0x40 + lane / slot)");
  ok(E.DRUM_KIT_NAMES.length === 37 && E.DRUM_KIT_NAMES[5] === "808", "drums: the kit names (the SRC list)");
  /* the mock device */
  const { m, rq, ev, done } = attachMock({});
  const sm = E.parse[C.SMP_INFO](await rq(E.req.smpInfo()));
  ok(js(sm.caps) === js([80, 80, 64]), "drums: SMP_INFO ends with each slot's KiB (USR3 64: the banks take 16)");
  const big = new Uint8Array(256);
  const w = E.parse[C.SMP_WRITE](await rq(E.req.smpWrite(2, 64 * 1024, big))), w1 = E.parse[C.SMP_WRITE](await rq(E.req.smpWrite(1, 64 * 1024, big)));
  ok(w.rc === 1 && w1.rc !== 1, "drums: USR3 refuses data past 64 KiB (USR2 takes it)");
  let L = E.parse[C.DRUM_LANES](await rq(E.req.drumLanes()));
  ok(L.lanes.length === 16 && L.lanes.every((l) => l.src === 0 && l.ofs.every((v) => !v) && l.len === 1024) && L.ukit === 0, "drums: DRUM_LANES: 16 lanes, all as the kit");
  const one = { ofs: [3, -10, 0, 0, 0, -20, 0, -3], src: 2, hit: 4, start: 512, len: 256, snd: TRK };
  const r1 = E.parse[C.DRUM_LANE](await rq(E.req.drumLane(5, one)));
  ok(r1.lane === 5 && js({ ...r1, lane: undefined }) === js({ ...one, lane: undefined }), "drums: DRUM_LANE set, read back");
  L = E.parse[C.DRUM_LANES](await rq(E.req.drumLanes({ ...L, lanes: L.lanes.map((l, i) => (i === 1 ? { ...l, ofs: [99, 0, 0, 0, 0, 0, 0, 0] } : l)) })));
  ok(L.lanes[1].ofs[0] === 24 && L.lanes[5].hit === 0, "drums: DRUM_LANES set: clamped, the whole block replaced");
  let K = E.parse[C.UKIT_LIST](await rq(E.req.ukitList()));
  ok(K.kits.length === 16 && K.kits[0].used && K.kits[0].name === "KIT 1" && !K.kits[1].used && K.kits[4].name === "KIT 5",
    "drums: UKIT_LIST (the mock's kit in slot 1; names are the slot numbers)");
  await rq(E.req.drumLane(2, { ...E.emptyLane(), src: 1, hit: 3 }));
  let o = E.parse[C.UKIT_OP](await rq(E.req.ukitOp(4, 2)));
  const g = E.parse[C.UKIT_GET](await rq(E.req.ukitGet(4)));
  ok(o.rc === 0 && g.ok && g.kit.lanes[2].src === 1 && g.kit.lanes[2].hit === 3 && g.kit.lanes[0].src === E.DL.KIT0 + 5,
    "drums: UKIT_OP store: the lanes, KIT written as the kit (808)");
  o = E.parse[C.UKIT_OP](await rq([C.UKIT_OP, [4, 3, 0x52, 0]]));
  ok(o.rc === 1, "drums: UKIT_OP 3 (rename) is gone: refused");
  o = E.parse[C.UKIT_OP](await rq(E.req.ukitOp(0, 0)));
  L = E.parse[C.DRUM_LANES](await rq(E.req.drumLanes()));
  ok(o.rc === 0 && L.ukit === 1 && L.lanes[2].src === E.DL.KIT0 + 6 && L.lanes[0].ofs[0] === -2, "drums: UKIT_OP load: the project's lanes");
  const p = E.parse[C.UKIT_PUT](await rq(E.req.ukitPut(9, kit))), g9 = E.parse[C.UKIT_GET](await rq(E.req.ukitGet(9)));
  ok(p.rc === 0 && js(g9.kit) === js(kit), "drums: UKIT_PUT / UKIT_GET round trip (an import)");
  o = E.parse[C.UKIT_OP](await rq(E.req.ukitOp(9, 1)));
  K = E.parse[C.UKIT_LIST](await rq(E.req.ukitList()));
  ok(o.rc === 0 && !K.kits[9].used && E.parse[C.UKIT_OP](await rq(E.req.ukitOp(9, 1))).rc === 1, "drums: UKIT_OP erase (an empty slot: rc 1)");
  m.state.smp[1].flash.set([1, 2, 3, 4, 5], 600);
  const sr = E.parse[C.SMP_READ](await rq(E.req.smpRead(1, 600, 5))), se = E.parse[C.SMP_READ](await rq(E.req.smpRead(2, 64 * 1024 - 10, 256)));
  ok(js([...sr.data]) === js([1, 2, 3, 4, 5]) && sr.offset === 600 && se.data.length === 10, "drums: SMP_READ (not past USR3's end)");
  /* version 2: the sends */
  let L2 = E.parse[C.DRUM_LANES](await rq(E.req.drumLanes(null, true)));
  ok(L2.v2 && L2.lanes.length === 16 && L2.lanes.every((l) => l.snd.rev === -1 && !l.snd.dly && !l.snd.cho), "drums v2: DRUM_LANES 2: every lane TRK / 0");
  const r2 = E.parse[C.DRUM_LANE](await rq(E.req.drumLane(2, { ...L2.lanes[2], snd: { rev: 31, dly: 0, cho: 0 } }, true)));
  ok(r2.lane === 2 && r2.snd.rev === 31 && r2.src === L2.lanes[2].src, "drums v2: DRUM_LANE 0x40 + 2: reverb on the snare only");
  L2 = E.parse[C.DRUM_LANES](await rq(E.req.drumLanes()));
  L2 = E.parse[C.DRUM_LANES](await rq(E.req.drumLanes(L2)));     /* a version 1 set keeps the sends */
  L2 = E.parse[C.DRUM_LANES](await rq(E.req.drumLanes(null, true)));
  ok(L2.lanes[2].snd.rev === 31 && L2.lanes[3].snd.rev === -1, "drums v2: a version 1 set leaves the sends");
  o = E.parse[C.UKIT_OP](await rq(E.req.ukitOp(6, 2, "SENDS")));
  const g6 = E.parse[C.UKIT_GET](await rq(E.req.ukitGet(6, true)));
  ok(o.rc === 0 && g6.slot === 6 && g6.kit.lanes[2].snd.rev === 31, "drums v2: a kit stored with its sends, UKIT_GET 0x40 + slot");
  const kit2 = { ...kit, lanes: kit.lanes.map((l, i) => ({ ...l, snd: i === 4 ? { rev: -1, dly: 20, cho: 0 } : TRK })) };
  const p2 = E.parse[C.UKIT_PUT](await rq(E.req.ukitPut(10, kit2, true))), g10 = E.parse[C.UKIT_GET](await rq(E.req.ukitGet(10, true)));
  ok(p2.rc === 0 && p2.slot === 10 && js(g10.kit) === js(kit2), "drums v2: UKIT_PUT 0x40 + slot / UKIT_GET round trip with sends");
  ok(!ev.unknown.length && !ev.timeouts, "drums: no unmatched replies, no timeouts");
  done();
  /* a kit file: the kit and its samples */
  const slots = { 1: { hdr: Uint8Array.from({ length: 480 }, (_, i) => i & 255), data: Uint8Array.from({ length: 1001 }, (_, i) => (i * 7) & 255) } };
  const f = E.readKitFile(E.kitFile({ ...kit, lanes: kit.lanes.map((l, i) => (i === 3 ? { ...l, src: 2, snd: { rev: 5, dly: 6, cho: 7 } } : l)) }, slots));
  const oldFile = E.readKitFile(JSON.stringify({ ...JSON.parse(E.kitFile(kit)), kit: { ...JSON.parse(E.kitFile(kit)).kit, name: "BOOM" } }));
  ok(js(oldFile.kit) === js(kit) && !("name" in JSON.parse(E.kitFile(kit)).kit), "drums: kit files without a name; an older one's name ignored");
  ok(f.kit.lanes[3].src === 2 && js(f.kit.lanes[0]) === js(kit.lanes[0]) && js([...f.slots[1].data]) === js([...slots[1].data])
    && js([...f.slots[1].hdr]) === js([...slots[1].hdr]) && js(E.kitSlots(f.kit)) === js([0, 1, 2]) && f.kit.lanes[3].snd.cho === 7,
    "drums: a kit file round trip (kit + slot bytes, version 2 with the sends)");
  const v1f = JSON.parse(E.kitFile(kit, {}));
  v1f.version = 1;
  v1f.kit.lanes.forEach((l) => { delete l.snd; });
  ok(E.readKitFile(JSON.stringify(v1f)).kit.lanes.every((l) => l.snd.rev === -1 && !l.snd.dly), "drums: a version 1 kit file: every send TRK / 0");
  let bad = false;
  try { E.readKitFile(JSON.stringify({ format: "other" })); } catch (e) { bad = true; }
  ok(bad, "drums: another file refused");
  /* a firmware without the drum switches: no reply */
  const x = attachMock({ drums: false });
  const nr = await x.rq(E.req.drumLanes(), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const xs = E.parse[C.SMP_INFO](await x.rq(E.req.smpInfo()));
  ok(nr === "none" && js(xs.caps) === js([80, 80, 80]), "drums: older firmware: no DRUM_LANES, every slot 80 KiB");
  x.done();
  /* a firmware with the lanes but without the sends: no reply to version 2, version 1 as before */
  const y = attachMock({ sends: false });
  const n2 = await y.rq(E.req.drumLanes(null, true), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const y1 = E.parse[C.DRUM_LANES](await y.rq(E.req.drumLanes()));
  const n37 = await y.rq(E.req.drumLane(1, null, true), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(n2 === "none" && n37 === "none" && !y1.v2 && y1.lanes.length === 16, "drums: firmware without the sends: no reply to v2, v1 answered");
  y.done();
}

/* ------------------------------------------------- backup / restore (cmds 43..48) --- */
async function editorBackup() {
  const C = E.CMD, js = JSON.stringify;
  const ed = readFileSync(join(HERE, "../firmware/src/ed_backup.c"), "utf8");
  ok(/ED_BK_LIST = 43, ED_BK_READ, ED_BK_BEGIN, ED_BK_DATA, ED_BK_COMMIT, ED_BK_END/.test(ed) && C.BK_LIST === 43 && C.BK_END === 48,
    "backup: cmds 43..48 == ed_backup.c");
  const fwTags = [...ed.matchAll(/^ {4}\{\{'(\w)', '(\w)', '(\w)', '(\w)'\}, BK_(ST|USR)/gm)].map((m) => m.slice(1, 5).join(""));
  ok(fwTags.join() === "SETT,DLNS,PRJ1,PRJ2,PRJ3,PRJ4,AUTO,UPR1,UPR2,UKIT,USR1,USR2,USR3" && fwTags.every((x) => E.BK.NAMES[x]),
    "backup: the firmware's objects (order: drum records before the projects), each named in the editor");
  /* the file */
  const objs = [{ tag: "PRJ1", kind: "st", data: Uint8Array.from({ length: 3840 }, (_, i) => i & 255) },
    { tag: "USR2", kind: "usr", fm6: true, data: new Uint8Array(8192).fill(7) }];
  const fb = E.backupFile({ version: "x", magic: "FUNA", switches: 63, bk: 1 }, objs, "2026-10-06");
  const back = E.readBackupFile(fb);
  ok(back.objects.length === 2 && js([...back.objects[0].data]) === js([...objs[0].data]) && back.objects[1].fm6 && back.device.magic === "FUNA"
    && back.created === "2026-10-06", "backup file: round trip (header, objects, flags)");
  let bad = 0;
  for (const cut of [fb.slice(0, fb.length - 10), Uint8Array.from(fb, (x, i) => (i === 3000 ? x ^ 1 : x)), new Uint8Array(20)]) {
    try { E.readBackupFile(cut); } catch (e) { bad++; }
  }
  ok(bad === 3, "backup file: cut short, a flipped bit, another file: refused");
  /* the mock device: back up, wipe, restore, compare */
  const { m, rq, ev, done } = attachMock({});
  const st = m.state;
  st.bk.objs.PRJ1 = Uint8Array.from({ length: 3640 }, (_, i) => (i * 3) & 255);
  st.bk.objs.UKIT = Uint8Array.from({ length: 3784 }, (_, i) => (i * 5) & 255);
  st.bk.objs.SETT = Uint8Array.from({ length: 120 }, (_, i) => i);
  let L = E.parse[C.BK_LIST](await rq(E.req.bkList()));
  ok(L.version === 2 && L.objs.length === 14 && L.magic === "FUNA" && L.objs[2].tag === "PRJ1" && L.objs[2].hasData && L.objs[2].len === 3640 &&
    L.objs[2].crc === E.crc32(st.bk.objs.PRJ1) && L.objs[10].tag === "FM6B" && L.objs[10].kind === "fm6" &&
    L.objs.slice(11).every((o) => o.kind === "usr") && L.caps && L.caps.usrCap[2] === 65536 && L.caps.trk === 780,
    "backup: BK_LIST v2 (14 objects, the FM6 bank its own, lengths, CRCs, what the build holds)");
  const saved = {};
  let chunks = 0, crcOk = true, wrOk = true;
  for (const o of L.objs.filter((x) => x.hasData && x.kind === "st")) {   /* (USR1: the mock's FM6 bank, another path) */
    const d = new Uint8Array(o.len);
    for (let off = 0; off < o.len;) {
      const r = E.parse[C.BK_READ](await rq(E.req.bkRead(o.i, off)));
      crcOk = crcOk && r.crc === E.crc32(Uint8Array.from(r.data));
      d.set(r.data, off);
      off += r.data.length;
      chunks++;
    }
    saved[o.tag] = d;
  }
  ok(Object.keys(saved).join() === "SETT,PRJ1,UKIT" && E.crc32(saved.UKIT) === L.objs[9].crc && chunks === 1 + 15 + 15 && crcOk, "backup: BK_READ in 256-byte chunks, CRCs right");
  st.bk.objs = {};
  for (const [tag, d] of Object.entries(saved)) {
    const i = L.objs.find((o) => o.tag === tag).i;
    let r = E.parse[C.BK_BEGIN](await rq(E.req.bkBegin(i, d.length, E.crc32(d))));
    for (let off = 0; off < d.length && !r.rc; off += 256) r = E.parse[C.BK_DATA](await rq(E.req.bkData(i, off, d.subarray(off, off + 256))));
    r = r.rc ? r : E.parse[C.BK_COMMIT](await rq(E.req.bkCommit(i)));
    wrOk = wrOk && r.rc === 0;
  }
  const end = E.parse[C.BK_END](await rq(E.req.bkEnd(true)));
  ok(wrOk && end.rc === 0 && st.bk.reboots === 1 && Object.entries(saved).every(([k, d]) => js([...st.bk.objs[k]]) === js([...d])),
    "restore: BEGIN / DATA / COMMIT, END: every object byte for byte, the device restarts");
  /* torn: a transfer stopped before COMMIT writes nothing; a bad chunk CRC is refused */
  const i1 = L.objs.find((o) => o.tag === "SETT").i, other = new Uint8Array(120).fill(9);
  await rq(E.req.bkBegin(i1, 120, E.crc32(other)));
  const bd = E.req.bkData(i1, 0, other);
  bd[1][4] ^= 1;
  ok(E.parse[C.BK_DATA](await rq(bd)).rc === 2, "restore: a chunk with a wrong CRC: rc 2");
  await rq(E.req.bkEnd(false));
  ok(js([...st.bk.objs.SETT]) === js([...saved.SETT]) && st.bk.reboots === 1, "restore: stopped before COMMIT (abort): nothing written, no restart");
  st.playing = true;
  ok(E.parse[C.BK_BEGIN](await rq(E.req.bkBegin(i1, 120, 0))).rc === 3, "restore: refused while playing (rc 3)");
  st.playing = false;
  done();
  /* a build without the kit bank: listed, not written; the plan leaves it out */
  const y = attachMock({ bkOff: ["UKIT"] });
  L = E.parse[C.BK_LIST](await y.rq(E.req.bkList()));
  const iK = L.objs.find((o) => o.tag === "UKIT").i;
  const plan = E.bkPlan({ objects: [{ tag: "UKIT", kind: "st", data: saved.UKIT }, { tag: "PRJ1", kind: "st", data: saved.PRJ1 },
    { tag: "SLOG", kind: "st", data: new Uint8Array(4) }] }, L);
  ok(!L.objs[iK].inBuild && E.parse[C.BK_BEGIN](await y.rq(E.req.bkBegin(iK, 10, 0))).rc === 5 &&
    js(plan.map((x) => x.why)) === js(["bkNotInBuild", "", "bkUnknown"]), "restore: an object the build has not: rc 5, the editor skips it (and unknown tags)");
  y.done();
  /* the FM6 bank's slot (always backed up) and a sample slot's parts */
  const z = attachMock({});
  z.m.state.smp[1].flash.set([0x46, 0x4D, 0x36, 0x42], 0);
  L = E.parse[C.BK_LIST](await z.rq(E.req.bkList()));
  ok(L.objs[12].fm6 && L.objs[12].len === 8192 && !L.objs[13].hasData && !L.objs[13].fm6, "backup: an older slot holding the FM6 bank listed as such (8 KiB)");
  const sp = E.bkSlotParts(new Uint8Array(512 + 100));
  ok(sp.hdr.length === E.SMP.HDR_LEN && sp.data.length === 100, "restore: a sample slot object -> header + data (the sample upload)");
  z.done();
  /* a backup of a full build restored onto a reduced one: the report (skipped objects, stand-ins) before the confirm */
  {
    const prj = new Uint8Array(3640);
    prj.set([0x41, 0x4E, 0x55, 0x46], 0);                /* "FUNA" (LE) */
    prj[214] = 0; prj[214 + 780] = 11; prj[214 + 1560] = 4;   /* ANALOG, PHYS, SAMPLE */
    new DataView(prj.buffer).setInt16(198 + 1560, 6, true);   /* track 3: the SCRCH set */
    new DataView(prj.buffer).setInt16(2538, 1, true);         /* the drum kit DEEP */
    const full = { objects: [{ tag: "PRJ1", kind: "st", data: prj }, { tag: "USR3", kind: "usr", data: new Uint8Array(73728) },
      { tag: "UKIT", kind: "st", data: new Uint8Array(10) }, { tag: "SLOG", kind: "st", data: new Uint8Array(4) }] };
    const r = attachMock({ bkOff: ["UKIT"], caps: { eng: 0x3FF & ~(1 << 5), kits: 2 ** 37 - 1 - 2, sets: 0xBF } });
    const LR = E.parse[C.BK_LIST](await r.rq(E.req.bkList()));
    const rep = E.bkReport(full, LR), pl = E.bkPlan(full, LR);
    ok(rep.some((x) => /USR3: .*larger.*72 > 64 KiB/.test(x)) && rep.some((x) => /UKIT: not in this build/.test(x)) &&
      rep.some((x) => /SLOG: not a kind/.test(x)) && rep.some((x) => /PRJ1 track 2 uses PHYS: plays ANALOG, settings kept/.test(x)) &&
      rep.some((x) => /PRJ1 track 3 uses the SCRCH set/.test(x)) && rep.some((x) => /PRJ1 drums use kit DEEP/.test(x)) &&
      js(pl.map((x) => x.why)) === js(["", "bkTooBig", "bkNotInBuild", "bkUnknown"]),
      "restore onto a reduced build: the report names skipped objects (too big, not in the build, unknown) and the stand-ins");
    const LF = E.parse[C.BK_LIST](await attachMock({}).rq(E.req.bkList()));
    ok(E.bkReport({ objects: [{ tag: "PRJ1", kind: "st", data: prj }] }, LF).filter((x) => /track 2/.test(x)).length === 0,
      "... the same file on the full build: nothing to report for PHYS");
    r.done();
  }
  /* older firmware: no reply to BK_LIST */
  const x = attachMock({ backup: false });
  ok(await x.rq(E.req.bkList(), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none") === "none",
    "backup: older firmware: no reply to BK_LIST (the editor shows no backup)");
  x.done();
  ok(!ev.unknown.length && !ev.timeouts, "backup: no unmatched replies, no timeouts");
  /* the UI and the installer */
  ok(/id="backup"/.test(html) && /id="bkprog"/.test(html) && /function bkRestore\(/.test(html) && /function backupDo\(/.test(html) &&
    /if \(h === "backup"\) return "projects"/.test(html), "editor: Backup section (projects tab, #backup), progress, restore");
  const inst = readFileSync(join(HERE, "index_pkg.html"), "utf8");
  ok(/confirm\(t\("backupFirst"\)\)/.test(inst) && (inst.match(/backupFirst:/g) || []).length === 2 && /\.\.\/editor\/#backup/.test(inst),
    "installer: asks to back up first (ja / en), opens the editor's Backup");
}

/* ------------------------------------------------- editor tabs and strings --- */
function editorTabs() {
  const tabs = [...html.matchAll(/<button role="tab" data-tab="(\w+)"/g)].map((x) => x[1]);
  const panels = [...html.matchAll(/<section class="panel" id="p-(\w+)" data-tab="(\w+)"/g)].map((x) => [x[1], x[2]]);
  const TABS = JSON.parse((/const TABS = (\[[^\]]*\]);/.exec(html) || [])[1] || "[]");
  ok(tabs.length === 8 && js(tabs) === js(TABS) && js(panels.map((x) => x[1])) === js(TABS) && panels.every(([a, b]) => a === b),
    `editor: ${tabs.length} tabs, one panel each (${tabs.join(" ")})`);
  ok(/localStorage\.setItem\(TAB_KEY/.test(html) && /try \{ localStorage/.test(html) && /history\.replaceState\([^)]*"#" \+ name\)/.test(html)
    && /addEventListener\("hashchange"/.test(html), "editor: last tab in localStorage (try/catch) and in the URL hash");
  /* every string key in both languages */
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const ja = new Set(Object.keys(TEXT.ja)), en = new Set(Object.keys(TEXT.en));
  const used = new Set([...html.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"|hint = "(\w+)"/g)].map((x) => x[1] || x[2] || x[3] || x[4]));
  for (const k of ["needDevice", "smpNone", "bankConnect", "bankNone", "selectedTrack", "selectTrack", "drumHelp", "notesHelp", "live", "polling"]) used.add(k);
  const miss = [...used].filter((k) => !ja.has(k) || !en.has(k));
  const odd = [...ja].filter((k) => !en.has(k)).concat([...en].filter((k) => !ja.has(k)));
  ok(!miss.length && !odd.length, `editor: every string in ja and en (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", one language only " + odd : ""})`);
  /* the page script parses (the browser's view of it) */
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "editor: page script compiles" + (err ? ` (${err})` : ""));
  ok(!/#[0-9a-f]{3,6}\b/i.test(html.slice(html.indexOf("[hidden]") - 6000, html.indexOf("[hidden]")).replace(/:root[^}]*\}/g, "")),
    "editor: no colours beyond the black / white tokens in the new styles");
}

/* ------------------------------------------------- editor icons (Fukiai) --- */
function editorIcons() {
  const blk = html.slice(html.indexOf("const GLYPH = {"), html.indexOf("};", html.indexOf("const GLYPH = {")));
  const names = new Set([...blk.matchAll(/(\w+): 0x[0-9A-F]{4}/g)].map((m) => m[1]));
  const used = new Set([...html.matchAll(/data-ic="(\w+)"|ic: "(\w+)"|: "((?:waveform|function|symbol|control|port|ui|note)_\w+)"/g)].map((m) => m[1] || m[2] || m[3]));
  const missing = [...used].filter((n) => !names.has(n));
  ok(names.size > 0 && !missing.length, `editor: every icon name is in GLYPH (${used.size} used${missing.length ? ", missing " + missing : ""})`);
  const ttf = existsSync(join(HERE, "fukiai.ttf")) && readFileSync(join(HERE, "fukiai.ttf"));
  ok(ttf && ttf.readUInt32BE(0) === 0x00010000 && existsSync(join(HERE, "FUKIAI-LICENSE.txt")) && html.includes('href="FUKIAI-LICENSE.txt"'),
    "editor: fukiai.ttf and FUKIAI-LICENSE.txt next to editor.html");
  ok(/html:not\(\.fk\) \.ic \{ display: none; \}/.test(html) && html.includes('classList.add("fk")'), "editor: icons hidden until the font has loaded");
}

/* ------------------------------------------- user samples: JS == sampleio.py --- */
function wav(sr, ch, bits, float, frames, f) {
  const bps = bits / 8, data = Buffer.alloc(frames * ch * bps);
  for (let i = 0; i < frames; i++) for (let c = 0; c < ch; c++) {
    const v = f(i, c), o = (i * ch + c) * bps;
    if (float) data.writeFloatLE(v, o);
    else if (bits === 8) data[o] = Math.max(0, Math.min(255, Math.round(v * 127 + 128)));
    else if (bits === 16) data.writeInt16LE(Math.round(v * 32000), o);
    else if (bits === 24) data.writeIntLE(Math.round(v * 8000000), o, 3);
  }
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(float ? 3 : 1, 0); fmt.writeUInt16LE(ch, 2); fmt.writeUInt32LE(sr, 4);
  fmt.writeUInt32LE(sr * ch * bps, 8); fmt.writeUInt16LE(ch * bps, 12); fmt.writeUInt16LE(bits, 14);
  const chunk = (id, b) => Buffer.concat([Buffer.from(id), Buffer.from(Uint32Array.of(b.length).buffer), b]);
  const body = Buffer.concat([Buffer.from("WAVE"), chunk("fmt ", fmt), chunk("data", data)]);
  return Buffer.concat([Buffer.from("RIFF"), Buffer.from(Uint32Array.of(body.length).buffer), body]);
}

function samplesMatch() {
  const dir = mkdtempSync(join(tmpdir(), "felucca-web-"));
  const files = [
    ["tone_A4.wav", wav(44100, 1, 16, false, 9000, (i) => Math.sin(i * 0.0627) * Math.exp(-i / 4000))],
    ["pad C3.wav", wav(48000, 2, 24, false, 7000, (i, c) => Math.sin(i * (c ? 0.031 : 0.0313)) * 0.7)],
    ["Bb2 float.wav", wav(22050, 1, 32, true, 5000, (i) => ((i % 97) / 48 - 1) * 0.5)],
    ["BD1 lofi.wav", wav(96000, 1, 8, false, 12000, (i) => Math.sin(i * 0.01) * Math.exp(-i / 3000))],
  ].map(([n, b]) => { const p = join(dir, n); writeFileSync(p, b); return p; });
  const zones = files.map((p) => {
    const w = E.parseWav(readFileSync(p));
    const s = E.normalize(E.resample(w.x, w.sr, E.SMP.RATE));
    return { s, root: E.rootFromName(p.split("/").pop().replace(/\.[^.]*$/, "")) };
  });
  const js = E.buildSlot("Mix ä 12345", zones);
  execFileSync(PYTHON, [join(HERE, "../tools/fm1_sample_upload.py"), "build", "Mix ä 12345", join(dir, "slot"), ...files]);
  const pyHdr = readFileSync(join(dir, "slot.hdr")), pyData = readFileSync(join(dir, "slot.bin"));
  ok(eq(js.hdr, pyHdr) && eq(js.data, pyData), `samples: editor == sampleio.py (${files.length} WAV formats, ${js.data.length} B)`);
}

/* ------------------------------------------------------- CHOP (pure helpers) --- */
function chopTests() {
  const R = E.SMP.RATE, N = R * 4;
  /* a break: 8 hits (kick-like and snare-like, loud and ghost) on a quiet noise floor */
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32) * 2 - 1;
  const x = new Float64Array(N);
  for (let i = 0; i < N; i++) x[i] = rnd() * 0.002;
  const HITS = [0.10, 0.52, 0.93, 1.31, 1.80, 2.26, 2.70, 3.33].map((t) => Math.round(t * R));
  const AMP = [1, 0.8, 0.25, 0.9, 1, 0.3, 0.85, 0.7];
  HITS.forEach((h, k) => {
    for (let i = 0; i < R * 0.3 && h + i < N; i++) {
      const env = Math.exp(-i / (k % 2 ? 1500 : 3000)) * AMP[k];
      x[h + i] += env * (k % 2 ? rnd() * 0.8 : Math.sin(2 * Math.PI * 60 * i / R) * 0.9 + rnd() * 0.1);
    }
  });
  const nov = E.chopNovelty(x), hits = E.chopHits(x, nov, 5);
  const near = (a, b, ms) => Math.abs(a - b) <= ms * R / 1000;
  ok(hits.length === HITS.length && hits.every((h, i) => near(h, HITS[i] - E.CHOP.PRE, 3)),
    `chop: hits found (${hits.length} of ${HITS.length}, each within 3 ms of its attack)`);
  ok(E.chopHits(x, nov, 1).length < HITS.length && E.chopHits(x, nov, 1).length >= 4, "chop: low sensitivity keeps the hard hits only");
  const late = E.chopSnap(x, nov, HITS[3] + 0.035 * R), early = E.chopSnap(x, nov, HITS[4] - 0.03 * R), none = E.chopSnap(x, nov, 1.1 * R);
  ok(near(late, HITS[3] - E.CHOP.PRE, 3) && near(early, HITS[4] - E.CHOP.PRE, 3) && none === Math.round(1.1 * R),
    "chop: a TAP 35 ms late / 30 ms early lands on its hit; away from hits it stays");
  const grid = E.chopGrid(0, Math.round(R * 60 / 90 * 16), 90, 1);
  ok(grid.length === 16 && grid[1] === Math.round(R * 60 / 90) && E.chopEqual(100, 900, 4).join() === "100,300,500,700",
    "chop: grid (16 beats at 90 BPM) and equal parts");
  const list = E.chopList([10, 50, 400], 1000, 100);
  ok(list.map((c) => `${c.start}-${c.end}`).join() === "10-50,50-150,400-500", "chop: chops end at the next marker or the max length");
  const chops = E.chopList(hits, N), zones = E.chopZones(x, chops, 60, 0);
  const peak = (s) => s.reduce((a, v) => Math.max(a, Math.abs(v)), 0);
  ok(zones.length === 8 && zones.every((z, i) => z.root === 60 + i && z.lo === z.root && z.hi === z.root && z.s[0] === 0)
    && Math.abs(peak(zones[2].s) / peak(zones[0].s) - 0.25) < 0.05 && zones[0].fname === "CHOP01_C4.wav" && zones[1].fname === "CHOP02_C#4.wav",
    "chop: one key each from C4, levels kept (a ghost stays quiet), faded in");
  const one = E.chopZones(x, chops, 60, 1, 3);
  ok(one.length === 1 && one[0].root === 60 && one[0].lo === 0 && one[0].hi === 127 && one[0].fname === "CHOP04_C4.wav",
    "chop: one chop over the whole keyboard");
  const slot = E.buildSlot("BREAK", zones), v = new DataView(slot.hdr.buffer);
  ok(slot.hdr[6] === 8 && slot.hdr[32 + 25] === 60 && slot.hdr[32 + 26] === 60 && slot.hdr[32 + 7 * 28 + 25] === 67 && v.getInt16(32 + 20, true) === 60 * 16
    && slot.data.length <= E.SMP.MAX_DATA, "chop: slot header (8 zones, one key each)");
  const w = E.parseWav(E.wavFile(zones[1].s));
  ok(w.sr === R && w.x.length === zones[1].s.length && Math.abs(w.x[100] * 32768 - zones[1].s[100]) < 2, "chop: WAV writer round trip");
  const dir = mkdtempSync(join(tmpdir(), "sloop-chop-")), zp = join(dir, "c.zip");
  writeFileSync(zp, E.zipStore(zones.slice(0, 3).map((z) => ({ name: "BREAK/" + z.fname, data: E.wavFile(z.s) }))));
  const r = py(`import sys, zipfile
z = zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None
print(",".join(i.filename + ":" + str(i.file_size) for i in z.infolist()))`, zp).toString().trim();
  ok(r === zones.slice(0, 3).map((z) => `BREAK/${z.fname}:${44 + z.s.length * 2}`).join(), "chop: ZIP of the WAVs (Python reads it)");
}

/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  const pkg = join(HERE, "../build/felucca.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_[79]\d\d$/.test(productOf(raw)), "fm1pkg.js productOf");
  ok(["ota-FM-1_700", "ota-FM-1_712", "ota-FM-1_900", "ota-FM-1_906"].every((text) => OUR_LOADER({ text })) &&
     !["ota-FM-1_015", "ota-FM-1_500", "ota-FM-1", "FM-1_700"].some((text) => OUR_LOADER({ text })),
     "fm1ota.js OUR_LOADER: Optimist (7XX) and SLOOP / Felucca (9XX) loaders, no other");
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, finalIdentity = "FM-1_900", identity = "FM-1_015",
                       loaderIdentity = "ota-FM-1_900" } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.finalIdentity = finalIdentity; this.loaderIdentity = loaderIdentity;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, "FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot(this.loaderIdentity, "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.finalIdentity, "Felucca"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_900", (k) => steps.push(k));
  ok(got === "FM-1_900" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Felucca (${dev.served} reads)`);

  const rescue = new FakeFM1(image);
  rescue.boot("FM-1_000", "Felucca");
  const recovered = await new Updater(rescue.access).install(image, "FM-1_900");
  ok(recovered === "FM-1_900" && rescue.bad === 0, "fm1ota.js: recovery mode -> loader -> normal firmware");
  const failedBoot = new FakeFM1(image, { finalIdentity: "FM-1_000" });
  const rescueSteps = [];
  const bootError = await new Updater(failedBoot.access).install(image, "FM-1_900", (k) => rescueSteps.push(k)).then(() => null, (e) => e);
  ok(bootError?.code === "mismatch" && !rescueSteps.includes("done"), "fm1ota.js: boot into recovery is not reported as successful installation");

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_900", "Felucca Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");
  const stock = new FakeFM1(image);
  stock.boot("ota-FM-1_015", "FM-1 Update");
  const e3 = await new Updater(stock.access).resume(image).then(() => null, (x) => x);
  ok(e3 && e3.code === "foreign" && e3.detail === "ota-FM-1_015" && stock.served === 0, "fm1ota.js: another firmware's loader is never resumed ('foreign')");

  /* return to the official V15 (installer page, after Felucca 1.0.1): our firmware stages the official
   * loader, which writes V15; an interrupted return is resumed with the official loader only on request */
  const back = new FakeFM1(image, { identity: "FM-1_700", loaderIdentity: "ota-FM-1_015", finalIdentity: "FM-1_015" });
  const v15 = await new Updater(back.access).install(image, "FM-1_015");
  ok(v15 === "FM-1_015" && back.bad === 0, "fm1ota.js: Optimist -> official loader -> V15 (return to stock)");
  const half = new FakeFM1(image);
  half.boot("ota-FM-1_015", "FM-1 Update");
  const resumed = await new Updater(half.access).resume(image, null, OFFICIAL_LOADER);
  ok(resumed === true && half.bad === 0, "fm1ota.js: resume(image, step, OFFICIAL_LOADER) finishes an interrupted return to V15");
  ok(OFFICIAL_LOADER({ text: "ota-FM-1_015" }) && !OFFICIAL_LOADER({ text: "ota-FM-1_700" }) && !OFFICIAL_LOADER({ text: "ota-FM-1_905" }),
     "fm1ota.js OFFICIAL_LOADER: the stock loader (ota-FM-1_0XX), not ours");
  const notStock = await validateStockPackage(new Uint8Array(699956)).then(() => null, (x) => x);
  ok(notStock instanceof Error, "fm1pkg.js validateStockPackage: refuses anything but the official V15 file");
  const v15path = process.env.FM1_V15;
  if (v15path && existsSync(v15path)) {
    const s = await validateStockPackage(new Uint8Array(readFileSync(v15path)));
    ok(s.product === "FM-1_015" && s.sha256 === STOCK_V15_SHA256 && s.image.length > 0x93000,
       "fm1pkg.js validateStockPackage: the official FM-1.fwsc (FM1_V15)");
  }
}

await editorMock();
await editorLibrarian();
await editorFM6Engine();
await editorDX7();
await editorDX7Transfer();
await editorFM6Bank();
await editorLive();
await editorTracks();
await editorMixer();
await editorTrackParam();
await editorV5();
await editorDrums();
await editorBackup();
editorTabs();
editorIcons();
samplesMatch();
chopTests();
await packages();
await updater();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
