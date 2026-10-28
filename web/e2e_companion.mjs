// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Henri David
// The editor's protocol code (editor.html between PROTO-BEGIN and PROTO-END) against Optimist as an fm1-base app (the
// trial) in fm1-base's browser emulator, headless under Node: the emulator's app.wasm (fm1-base tools/build.py optimist
// --web) behind its "port" part's channel (fm1-base web/emu/parts/port.js: BroadcastChannel "fm1-emu", "here" / "to" /
// "from"), the page's side through fm1-base's own port_client.js emuAccess(), as the editor's "fm1-base emulator" device
// uses it. What runs: the capability query first, INFO (opti_cpfiles_1), the librarian over Optimist's own protocol
// (UP_LIST), a backup through the companion's files (cpBkList, every object read and checked), a parameter set through the
// companion (BPM), a restore of the backup (the check pass, the writes, BKCTL 1: the restart) and the parameter back as
// the backup had it after the restart. Not part of `make test` (it needs the trial's app.wasm):
//   node web/e2e_companion.mjs FM1BASE_TREE [EMU_DIR]
//     FM1BASE_TREE  an fm1-base checkout with the trial (its web/emu/parts/port_client.js)
//     EMU_DIR       the trial's browser emulator (default FM1BASE_TREE/optimist/build/emu)
import vm from "node:vm";
import { readFileSync, writeSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const tree = resolve(process.argv[2] || "");
const emuDir = resolve(process.argv[3] || join(tree, "optimist", "build", "emu"));
let failed = 0;
const dbg = process.env.E2E_DEBUG ? (...a) => console.log("  .", ...a) : () => {};
const ok = (pass, label) => { console.log(`${label.padEnd(76)} ${pass ? "ok" : "FAIL"}`); if (!pass) failed++; };

const html = readFileSync(new URL("./editor.html", import.meta.url), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(`${proto}
;({ CMD, parse, req, Link, Comp, CP, cpBkList, cpBkCheckAll, cpBkWrite, cpBkCtl, cpSmpInfo, emuPorts, backupFile, readBackupFile,
   bkPlan, crc32, zipStore, zipEntries })`, { setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder });

/* ---- the emulator's side: app.wasm, rendered in steps of simulated time, its port on the channel */
let mem, restarts = 0;
const wasi = new Proxy({
  clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => {                  /* (the simulator's prints, all of them taken: a write short of them is
                                                       written again by its C library, for ever; E2E_DEBUG shows them) */
    const dv = new DataView(mem.buffer);
    let len = 0;
    for (let k = 0; k < n; k++) {
      const p = dv.getUint32(iov + 8 * k, true), l = dv.getUint32(iov + 8 * k + 4, true);
      const text = Buffer.from(new Uint8Array(mem.buffer, p, l)).toString();
      if (text.includes("restart (plat_restart)")) restarts++;
      if (process.env.E2E_DEBUG) writeSync(2, text);
      len += l;
    }
    dv.setUint32(out, len, true);
    return 0;
  },
}, { get: (t, k) => (k in t ? t[k] : () => 0) });
const { instance } = await WebAssembly.instantiate(readFileSync(join(emuDir, "app.wasm")),
  { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports;
mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_boot();
const render = (ms) => { for (let i = 0; i < Math.ceil(ms * 44.1 / 128); i++) ex.web_render(128); };
const emu = "e2e" + process.pid, name = "OPTIMIST (emulator)";
const ch = new BroadcastChannel("fm1-emu");
const inbox = [];
ch.onmessage = (e) => {
  const m = e.data || {};
  if (m.t === "who") ch.postMessage({ t: "here", emu, name });
  else if (m.t === "to" && m.emu === emu) inbox.push(Uint8Array.from(m.data));
};
function drain() {                                  /* what the FM-1 sent: each frame to the page, as port.js does */
  const all = new Uint8Array(mem.buffer, ex.web_sx_out_buf(), ex.web_sx_out_len()).slice();
  ex.web_sx_out_clear();
  for (let at = 0; at < all.length;) {
    let k = at + 1;
    while (k < all.length && all[k] !== 0xF7 && all[k] !== 0xF0) k++;
    const end = k < all.length && all[k] === 0xF7 ? k + 1 : k;
    if (all[end - 1] === 0xF7) ch.postMessage({ t: "from", emu, data: all.slice(at, end) });
    at = end;
  }
}
const clock = setInterval(() => {                   /* 10 ms of the FM-1 each 2 ms of ours; a frame in when one came */
  const f = inbox.shift();
  if (f) { new Uint8Array(mem.buffer, ex.web_sx_in_buf(), f.length).set(f); ex.web_sx_in(f.length); }
  render(10);
  drain();
}, 2);
ch.postMessage({ t: "here", emu, name });
const fmMs = () => ex.web_now();
const waitFm = async (ms) => { const t0 = fmMs(); while (fmMs() - t0 < ms) await new Promise((r) => setTimeout(r, 5)); };
await waitFm(1500);                                 /* (the FM-1 powers on) */

/* ---- the page's side: fm1-base's port_client.js, the editor's Link and companion client */
const { emuAccess } = await import(pathToFileURL(join(tree, "web", "emu", "parts", "port_client.js")).href);
const access = emuAccess();
await new Promise((r) => setTimeout(r, 400));
const ports = E.emuPorts(access);
ok(!!ports && ports.input.name === name, "the editor's fm1-base emulator device: emuAccess() lists the trial's ports");
const link = new E.Link((b) => ports.output.send(b), { timeout: 1500, onRaw: (m) => comp.push(m) });
ports.input.addEventListener("midimessage", (m) => link.receive(m.data));
const comp = new E.Comp(link);

try {
  const h = await comp.hello({ timeout: 1500, retries: 1 });
  ok(h.name === "OPTIMIST" && h.extras.editor === "optimist" && !!(h.services & 2) && !!(h.services & 4),
    `the capability query first: ${h.name} ${h.appVersion} (${h.identity}), files and parameters`);
  const info = E.parse[E.CMD.INFO](await link.request(E.req.info(), { timeout: 1500 }));
  ok(info.lite && info.opti.opti_cpfiles === 1 && info.opti.opti_backup === undefined,
    "INFO: the editor without live editing, the backup as the companion's files (opti_cpfiles_1)");
  const up = E.parse[E.CMD.UP_LIST](await link.request(E.req.upList(0, 16)));
  ok(up.total === 32 && up.slots.length === 16, "the librarian over Optimist's own protocol: UP_LIST (32 user presets)");
  const noBk = await link.request(E.req.bkList(0), { timeout: 400, retries: 0, quiet: true }).then(() => false, () => true);
  ok(noBk, "BK_LIST (43) not answered: the trial leaves it to the companion");

  /* a parameter through the companion: BPM 133, the working project saved when quiet (its autosave), then the backup */
  const ps = await comp.params(), bpm = ps.find((p) => p.name === "BPM");
  ok(!!bpm && (await comp.set(bpm.id, 133)) === 133 && (await comp.get([bpm.id])).get(bpm.id) === 133,
    `a parameter set through the companion: ${ps.length} described, BPM set to 133 and read back`);
  await waitFm(12000);                              /* (project.c: the work saved once it stayed the same 10 s) */
  let L = await E.cpBkList(comp);
  const pick = L.objs.filter((o) => o.hasData && o.kind !== "usr");
  const objects = [];
  for (const o of pick) objects.push({ tag: o.tag, kind: o.kind, fm6: o.fm6, data: await comp.read({ i: o.file, size: o.len }) });
  const file = E.backupFile({ version: info.version, bk: L.version, caps: L.caps }, objects);
  const back = E.readBackupFile(file);
  ok(pick.length >= 2 && pick.some((o) => o.tag === "AUTO") && back.objects.length === pick.length,
    `a backup through the companion's files: ${pick.length} objects (${pick.map((o) => o.tag.trim()).join(" ")}), ${file.length} B, read back`);

  /* the state moved on (BPM 99: the restore's session saves it first, then writes the backup over it), then the backup
     restored as the editor's bkRestore does */
  ok((await comp.set(bpm.id, 99)) === 99, "BPM set to 99 through the companion");
  L = await E.cpBkList(comp);
  const plan = E.bkPlan(back, L).filter((x) => !x.why);
  dbg("plan", plan.map((x) => x.o.tag));
  const bad = await E.cpBkCheckAll(L, plan.filter((x) => x.o.kind === "st"), null);
  dbg("checked", bad);
  for (const x of plan) { dbg("write", x.o.tag, x.o.data.length); await E.cpBkWrite(L, x.d.i, x.o.data); }
  await E.cpBkCtl(L, 1);
  await waitFm(2500);                               /* (the restart 150 ms later, the FM-1 powers on) */
  ok(!bad.length && plan.length === pick.length && restarts === 1, `a restore: ${plan.length} objects checked and written, BKCTL 1: ${restarts} restart`);
  const again = await comp.get([bpm.id]);
  ok(again.get(bpm.id) === 133, `after the restart BPM is the backup's: ${again.get(bpm.id)}`);
  const L2 = await E.cpBkList(comp);
  const same = pick.every((o) => { const x = L2.objs.find((y) => y.tag === o.tag); return x && x.len > 0; });
  ok(same, "after the restart every object restored holds data again");
} catch (e) {
  console.error(e);
  failed++;
} finally {
  clearInterval(clock);
  link.close();
  ch.close();
}
console.log(failed ? `e2e_companion: ${failed} FAILED` : "e2e_companion: all ok");
process.exit(failed ? 1 : 0);
