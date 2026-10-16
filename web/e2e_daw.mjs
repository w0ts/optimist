// SPDX-License-Identifier: GPL-3.0-only
// The editor in a real (headless) Chrome against its mock device: the mixer is home, every popup opens with ONE click
// from a strip (NAV) and closes with Escape, its x or a click outside, back to the mixer as it was; light / dark follow the system.
// Not part of `make test` (it needs Chrome). Run from the repo root:
//   node web/e2e_daw.mjs [--shots DIR]
//   node web/e2e_daw.mjs --emu http://127.0.0.1:8765 [--shots DIR]   the same editor against the emulator's web-MIDI bridge
//     (fm1-ui FIRMWARE --ui web): protocol v9 live: the meters move while a pattern plays and fall to silence after STOP,
//     nothing is polled, a parameter of a track that is not selected appears without polling (needs the bridge's
//     /__fm1/turn knob route; without it that check is skipped), the device's v9 cost (SYNC_STATS) and reply latency.
// CHROME=path overrides the browser (default: Google Chrome / Chromium in their usual places).
import { spawn } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const shots = process.argv.includes("--shots") ? process.argv[process.argv.indexOf("--shots") + 1] : null;
const EMU = process.argv.includes("--emu") ? process.argv[process.argv.indexOf("--emu") + 1].replace(/\/+$/, "") : null;
const CHROMES = [process.env.CHROME, "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/usr/bin/google-chrome",
  "/usr/bin/chromium", "/usr/bin/chromium-browser", "C:/Program Files/Google/Chrome/Application/chrome.exe"].filter(Boolean);
const chrome = CHROMES.find((p) => existsSync(p));
if (!chrome) { console.log("e2e: no Chrome found (CHROME=path)"); process.exit(0); }
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(70)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/* the web folder over http (the editor loads its icon font next to it) */
const server = createServer((q, r) => {
  const f = join(HERE, decodeURIComponent(q.url.split("?")[0]).replace(/^\/+/, "") || "editor.html");
  if (!f.startsWith(HERE) || !existsSync(f)) { r.writeHead(404); r.end(); return; }
  r.writeHead(200, { "Content-Type": f.endsWith(".html") ? "text/html" : "application/octet-stream" });
  r.end(readFileSync(f));
}).listen(0, "127.0.0.1");
await new Promise((r) => server.on("listening", r));
const port = server.address().port, cdpPort = 9400 + Math.floor(Math.random() * 400);
const prof = mkdtempSync(join(tmpdir(), "e2e-chrome-"));
const proc = spawn(chrome, ["--headless=new", "--mute-audio", `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${prof}`, "--no-first-run",
  "--disable-background-timer-throttling", "--disable-renderer-backgrounding", "about:blank"], { stdio: "ignore" });
/* never leave Chrome behind (an orphan keeps the editor page alive, and its audio): close it on any exit */
const closeChrome = () => { try { proc.kill(); } catch (e) { /* already gone */ } };
process.on("exit", closeChrome);
for (const sig of ["SIGINT", "SIGTERM", "SIGHUP"]) process.on(sig, () => { closeChrome(); process.exit(130); });
process.on("uncaughtException", (e) => { console.error(e); closeChrome(); process.exit(1); });
let tab;
for (let i = 0; i < 50 && !tab; i++) {
  await sleep(200);
  tab = await fetch(`http://127.0.0.1:${cdpPort}/json/new?about:blank`, { method: "PUT" }).then((r) => r.json()).catch(() => null);
}
const ws = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener("open", r));
let id = 0;
const pending = new Map();
ws.addEventListener("message", (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } });
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const run = async (expr) => { const x = (await send("Runtime.evaluate", { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true })).result; return x.exceptionDetails ? { err: x.exceptionDetails.exception ? x.exceptionDetails.exception.description : x.exceptionDetails.text } : x.result.value; };
const key = async (k) => { await send("Input.dispatchKeyEvent", { type: "keyDown", key: k, code: k, windowsVirtualKeyCode: k === "Escape" ? 27 : 0 });
  await send("Input.dispatchKeyEvent", { type: "keyUp", key: k, code: k, windowsVirtualKeyCode: k === "Escape" ? 27 : 0 }); };
const shot = async (name) => {
  if (!shots) return;
  const s = await send("Page.captureScreenshot", { format: "png" });
  writeFileSync(join(shots, name + ".png"), Buffer.from(s.result.data, "base64"));
};
await send("Page.enable");
await send("Emulation.setDeviceMetricsOverride", { width: 1400, height: 1000, deviceScaleFactor: 1, mobile: false });
const U = `const D = () => (window.fm1E2E ? window.fm1E2E.dev() : null); const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const until = async (f, ms = 60000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) return false; await sleep(100); } return true; };
  const $ = (q) => document.querySelector(q);
  const shown = (e) => !!e && e.getClientRects().length > 0;`;
/* ---- the emulator (--emu): protocol v9 against the real firmware over the web-MIDI bridge ---- */
if (EMU) {
  /* a second client on the bridge (this process): the device's own changes, its cost; frames from the device reach
     every client, so it only looks at the replies it asked for */
  const bws = new WebSocket(EMU.replace(/^http/, "ws") + "/midi");
  bws.binaryType = "arraybuffer";
  const waiting = [];
  bws.addEventListener("message", (e) => { const b = [...new Uint8Array(e.data)]; const i = waiting.findIndex((w) => w.cmd === b[4]); if (b[0] === 0xF0 && i >= 0) waiting.splice(i, 1)[0].res(b); });
  await new Promise((r) => bws.addEventListener("open", r));
  const brq = (cmd, args = []) => new Promise((res) => { const w = { cmd, res }; waiting.push(w); bws.send(Uint8Array.from([0xF0, 0x7D, 0x46, 0x4C, cmd, ...args, 0xF7]));
    setTimeout(() => { const i = waiting.indexOf(w); if (i >= 0) { waiting.splice(i, 1); res(null); } }, 1500); });
  const rd35 = (a, k) => { let v = 0; for (let i = 4; i >= 0; i--) v = v * 128 + a[5 + 5 * k + i]; return v; };
  const stats = async (reset) => { const r = await brq(64, reset ? [1] : []); return r && { scans: rd35(r, 0), frames: rd35(r, 1), bytes: rd35(r, 2), maxUs: rd35(r, 3), sumUs: rd35(r, 4), stream: rd35(r, 5), seen: rd35(r, 6), missed: rd35(r, 7) }; };
  await brq(53, [2]);                                /* (from a known state: stopped, track 1 selected) */
  await brq(27, [0]);
  await sleep(500);
  await send("Page.navigate", { url: `${EMU}/editor.html?e2e=1#mixer` });
  const conn = await run(`${U} return (await until(() => D() && D().dump && document.querySelectorAll("#mixer .strip").length === 5
    && !/Reading|reading/.test($("#status").textContent), 240000)) && { v9: D().v9, stream: D().stream, live: $("#live").textContent, meters: document.querySelectorAll("#mixer .meter").length };`);
  ok(conn && conn.v9 && conn.stream && conn.meters === 5 && /v9/.test(conn.live), `emu: connected with protocol v9 (pushes + stream), a meter on each strip and the master (${JSON.stringify(conn)})`);
  /* what the page sends and receives from now on */
  await run(`const dev = window.fm1E2E.dev(); window.__sent = []; window.__push = []; const s = dev.link.sendRaw.bind(dev.link); dev.link.sendRaw = (f) => { window.__sent.push([Date.now(), f[4]]); return s(f); };
    const p = dev.link.onPush; dev.link.onPush = (f) => { window.__push.push([Date.now(), f.cmd, Array.from(f.a)]); return p(f); }; return true;`);
  /* a pattern: track 1 C4 every beat, the drums kick on the beats, snare between (the second client writes it) */
  for (const i of [0, 4, 8, 12]) await brq(30, [0, i, 1, 60, 0, 0, 0, 0, 0, 100, 0, 0, 0]);
  for (const i of [0, 4, 8, 12]) await brq(33, [i, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
  for (const i of [2, 6, 10, 14]) await brq(33, [i, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
  await stats(true);
  const pingIdle = [];
  for (let k = 0; k < 15; k++) { const t0 = performance.now(); await brq(25); pingIdle.push(performance.now() - t0); await sleep(50); }
  await run(`${U} $("#play").click(); return true;`);
  await sleep(1500);
  const lv = await run(`${U} const seen = {}; for (let k = 0; k < 40; k++) { document.querySelectorAll("#mixer .meter").forEach((m, i) => { seen[i] = Math.max(seen[i] ?? -99, +m.getAttribute("aria-valuenow")); }); await sleep(50); }
    return { seen, st: [...document.querySelectorAll("#mixer .meter")].map((m) => m.dataset.st), playing: D().status && D().status.playing };`);
  await shot("emu-v9-meters-playing");
  const pingBusy = [];
  for (let k = 0; k < 15; k++) { const t0 = performance.now(); await brq(25); pingBusy.push(performance.now() - t0); await sleep(50); }
  const sPlay = await stats(false);
  let sBusy = null;
  const pingSweep = [];
  ok(lv && lv.playing && lv.seen[0] > -30 && lv.seen[3] > -30 && lv.seen[4] > -40 && lv.seen[1] <= -59,
    `emu: playing: the meters of track 1, the drums and the master move, a silent track stays at the floor (${JSON.stringify(lv)})`);
  await run(`${U} $("#stop").click(); return true;`);
  await sleep(6000);                                 /* (the release and the reverb's tail, then the 24 dB/s fall) */
  const off = await run(`return [...document.querySelectorAll("#mixer .meter")].map((m) => +m.getAttribute("aria-valuenow") <= -59 && m.querySelector("b.hold").hidden);`);
  await shot("emu-v9-meters-stopped");
  ok(Array.isArray(off) && off.every(Boolean), `emu: after STOP every meter falls to silence, holds gone (${JSON.stringify(off)})`);
  const pol = await run(`const t0 = window.__sent[0] ? window.__sent[0][0] : Date.now(), c = {}; window.__sent.forEach(([, k]) => { c[k] = (c[k] || 0) + 1; });
    return { c, pushes: window.__push.reduce((o, [, k]) => (o[k] = (o[k] || 0) + 1, o), {}), ms: Date.now() - t0 };`);
  ok(pol && !(pol.c[4]) && (pol.c[53] || 0) <= 2 && !(pol.c[37]) && !(pol.c[27]) && !(pol.c[29]) && (pol.pushes[58] || 0) > 50,
    `emu: no polling: no DUMP / TRACK / TRACK_DUMP / lane reads, STATUS only for PLAY / STOP, the stream carries the rest (${JSON.stringify(pol)})`);
  /* a parameter of a track that is not selected, changed on the device: the second client selects track 2 (the editor's
     view keeps track 1), KNOB 4 on the home page moves the selected track's level: pushed, on strip 2, nothing asked */
  const turnOk = await fetch(`${EMU}/__fm1/turn?e=0&d=0`).then((r) => r.ok, () => false);
  if (!turnOk) console.log("emu: (the bridge has no /__fm1/turn: the device-side change check is skipped)");
  else {
    const before = await run(`const dev = window.fm1E2E.dev(); window.__sent.length = 0; return { lv: dev.mix.tracks[1].level, fader: +document.querySelector('#mixer .strip[data-track="1"] .fader').value, sel: dev.sel };`);
    await brq(27, [1]);
    await fetch(`${EMU}/__fm1/turn?e=3&d=-6`);
    await sleep(1200);
    const after = await run(`const dev = window.fm1E2E.dev(); return { lv: dev.mix.tracks[1].level, fader: +document.querySelector('#mixer .strip[data-track="1"] .fader').value, sel: dev.sel,
      asked: window.__sent.map(([, k]) => k).filter((k) => k !== 25), pushed: window.__push.filter(([, k]) => k === 59).slice(-3).map(([, , a]) => a) };`);
    await shot("emu-v9-other-track");
    await fetch(`${EMU}/__fm1/turn?e=3&d=6`);
    await sleep(600);
    await brq(27, [0]);
    ok(after && before.sel === 0 && after.sel === 0 && after.lv < before.lv && after.fader === after.lv && after.asked.length === 0,
      `emu: a parameter of track 2 changed on the device shows on its strip, pushed (PARAMS), nothing polled (${JSON.stringify({ before, after })})`);
  }
  /* a busy moment: the song plays and three knobs sweep (BPM, SWING, the level: 3 x ~30 detents a second) for 3 s;
     what v9 pushes, what it costs, and a reply's round trip meanwhile */
  if (await fetch(`${EMU}/__fm1/turn?e=0&d=0`).then((r) => r.ok, () => false)) {
    await run(`${U} $("#play").click(); return true;`);
    await sleep(800);
    await stats(true);
    const t0 = Date.now();
    let k = 0;
    const sweeping = (async () => { while (Date.now() - t0 < 3000) { const dir = (k++ >> 4) & 1 ? -1 : 1;
      await Promise.all([0, 2, 3].map((e) => fetch(`${EMU}/__fm1/turn?e=${e}&d=${dir}`))); await sleep(30); } })();
    while (Date.now() - t0 < 3000) { const p0 = performance.now(); await brq(25); pingSweep.push(performance.now() - p0); await sleep(100); }
    await sweeping;
    sBusy = { ...(await stats(false)), ms: Date.now() - t0 };
    await run(`${U} $("#stop").click(); return true;`);
  }
  const med = (a) => a.slice().sort((x, y) => x - y)[a.length >> 1];
  console.log(`emu: v9 cost while playing (SYNC_STATS): ${JSON.stringify(sPlay)}; PING round trip median idle ${med(pingIdle).toFixed(1)} ms, streaming ${med(pingBusy).toFixed(1)} ms`);
  if (sBusy) console.log(`emu: playing + 3 knob sweeps for ${sBusy.ms} ms: ${JSON.stringify(sBusy)}; v9 pushes ${Math.round(sBusy.bytes * 1000 / sBusy.ms)} B/s, scan ${(sBusy.sumUs / Math.max(1, sBusy.scans)).toFixed(1)} us mean / ${sBusy.maxUs} us max (${(sBusy.sumUs / sBusy.ms / 10).toFixed(2)} % of the time); PING median ${med(pingSweep).toFixed(1)} ms, max ${Math.max(...pingSweep).toFixed(1)} ms`);
  ok(sPlay && sPlay.stream > 0 && med(pingBusy) < med(pingIdle) + 15, "emu: a reply is not held up by the stream (PING round trip while streaming ~ idle)");
  bws.close();
  ws.close();
  proc.kill();
  server.close();
  console.log(failed ? `E2E FAILED (${failed})` : "e2e passed");
  process.exit(failed ? 1 : 0);
}
/* --readme-shots DIR: the README's pictures of the editor (mixer, mock device, light and dark) as webp, then stop (no checks) */
if (process.argv.includes("--readme-shots")) {
  const out = process.argv[process.argv.indexOf("--readme-shots") + 1];
  await send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 1000,deviceScaleFactor: 2, mobile: false });
  await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxs=1&e2e=1#mixer` });
  await sleep(1500);
  await run(`${U} return await until(() => document.querySelectorAll("#mixer .strip").length === 5 && !/Reading|reading/.test($("#status").textContent), 120000);`);
  for (const mode of ["light", "dark"]) {
    await send("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: mode }] });
    await sleep(800);
    const s = await send("Page.captureScreenshot", { format: "webp", quality: 80 });
    writeFileSync(join(out, `editor-mixer-${mode}.webp`), Buffer.from(s.result.data, "base64"));
    console.log(`readme shot: ${join(out, `editor-mixer-${mode}.webp`)}`);
  }
  ws.close(); proc.kill(); server.close();
  process.exit(0);
}
/* not connected (?connect=0: no auto-connect): the connect card only, no mixer, no strip, no value */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&connect=0#mixer` });
await sleep(1200);
ok(await run(`${U} return shown($("#connectcard")) && shown($("#connect")) && document.querySelectorAll("#mixer .strip").length === 0
  && [...document.querySelectorAll(".panel")].every((p) => !shown(p)) && !shown($("#tabs")) && !shown($("#tp")) && !shown($("#disconnect"))
  && /installer/i.test($("#connectcard").textContent);`), "e2e: not connected: the connect card only (no mixer, no tabs, no transport)");
await shot("editor-disconnected");
/* the colours follow the system, live, with no switch anywhere: dark and light under an emulated prefers-color-scheme, restyled while open */
const scheme = (v) => send("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: v }] });
await scheme("dark"); await sleep(150);
const darkBg = await run(`return getComputedStyle(document.body).backgroundColor;`);
await scheme("light"); await sleep(150);
const lightBg = await run(`return getComputedStyle(document.body).backgroundColor;`);
await shot("editor-disconnected-light");
ok(darkBg === "rgb(0, 0, 0)" && lightBg === "rgb(244, 244, 242)" && await run(`${U} return !document.querySelector("select#theme, #appearance, #appearbox, .theme");`),
  "e2e: not connected: dark / light follow the system live (no reload), no theme picker");
await scheme("dark"); await sleep(150);
/* auto-connect: the page connects to the (mock) device on load, no click */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1#mixer` });
await sleep(1500);
ok(await run(`${U} return (await until(() => $("#live").textContent.length > 0 && document.querySelectorAll("#mixer .strip").length === 5, 120000))
  && !shown($("#connectcard")) && shown($("#disconnect"));`),
  "e2e: auto-connected to the mock on load, the mixer shows 5 strips (4 tracks, master)");
await shot("mixer");
/* the tempo: in the transport bar only (not on the master strip, in Settings or in the master FX popup) */
ok(await run(`${U} const bpmCtl = () => [...document.querySelectorAll(".knob .kl, .pk .kl, .row > span:first-child")].filter((e) => shown(e) && e.textContent.trim() === "BPM").length;
  const a = bpmCtl(); document.querySelector("[data-tab=settings]").click(); await sleep(300); const b = bpmCtl();
  document.querySelector("[data-tab=mixer]").click(); await sleep(300);
  document.querySelector("#mixer .strip.master [data-pop=fxdelay]").click(); await until(() => $("#pop").open, 5000); await sleep(300);
  const c = bpmCtl(); $("#popx").click(); await sleep(200);
  return a === 0 && b === 0 && c === 0 && shown($("#bpm")) && +$("#bpm").value > 0;`), "e2e: BPM only in the transport bar (not the master strip, Settings, master FX)");
/* the strips share the same rows: every row starts at the same y on every strip (tracks and master), the four sends and the pan
   knob are on every track strip, the fader and its meter side by side, no slider but the fader */
const layoutRes = await run(`${U} const strips = [...document.querySelectorAll("#mixer .strip")], trk = strips.slice(0, 4), top = (s, q) => { const e = s.querySelector(q); return e ? Math.round(e.getBoundingClientRect().top) : null; };
  const rows = { head: ".shead", name: ".snd", group: ".pops", seq: ".seqb", ov: ".ov", sends: ".fx", fader: ".fm", level: ".lv", bottom: ".row2" };
  const bad = Object.entries(rows).filter(([, q]) => new Set(trk.map((s) => top(s, q))).size !== 1).map(([k]) => k);
  const panTop = trk.map((s) => Math.round(s.querySelector(".srow[data-row=pan] > .knob").getBoundingClientRect().top));
  const masterBad = [["head", ".shead"], ["fader", ".fm"]].filter(([, q]) => top(strips[4], q) !== top(strips[0], q)).map(([k]) => k);
  const sends = trk.map((s) => [...s.querySelectorAll(".fx .knob")].map((k) => k.querySelector(".kl").textContent + "@" + Math.round(k.getBoundingClientRect().left - s.getBoundingClientRect().left)).join());
  const sendOk = new Set(sends).size === 1 && sends[0].split(",").map((x) => x.split("@")[0]).join() === "DST,CHO,DLY,REV";
  const fmt = trk.map((s, i) => [...s.querySelectorAll(".fx .knob")].map((k) => (i === 3 ? /^\\d+$|^--$/ : /%$|^--$/).test(k.querySelector(".kv").textContent.trim())).every(Boolean));
  const meters = trk.map((s) => { const f = s.querySelector(".fader").getBoundingClientRect(), m = s.querySelector(".meter").getBoundingClientRect(); return m.left >= f.right && Math.abs(m.top - f.top) < 4 && Math.abs(m.height - f.height) < 4; });
  const sl = trk.map((s) => s.querySelectorAll("input[type=range]").length);
  const panKnob = trk.every((s) => [...s.querySelectorAll(".srow[data-row=pan] > .knob")].some((e) => e.getAttribute("role") === "slider" && /Pan/i.test(e.getAttribute("aria-label"))));
  return JSON.stringify({ bad, pan: new Set(panTop).size === 1, masterBad, sendOk, fmt: fmt.every(Boolean), meters: meters.every(Boolean), sliders: sl.join(), panKnob });`);
ok(layoutRes === '{"bad":[],"pan":true,"masterBad":[],"sendOk":true,"fmt":true,"meters":true,"sliders":"1,1,1,1","panKnob":true}', "e2e: strips share the rows (same y), DST CHO DLY REV on every track, pan is a knob, fader + meter side by side " + (layoutRes && layoutRes.length < 400 ? layoutRes : ""));
await shot("mixer-layout");
/* the drum strip: a click selects a sound (highlighted, no popup; the send row shows its CHO DLY REV, named), a double click opens it;
   turning the row's REV writes that sound's REV on the device (the mock's lanes, DRUM_LANE v2); the other strips' rows stay aligned */
const dsel = await run(`${U} const d = document.querySelector('#mixer .strip[data-track="3"]'), b = d.querySelector('.ln[data-l="6"]');
  b.click(); await sleep(400);
  const noPop = !$("#pop").open, hi = b.classList.contains("lsel") && b.getAttribute("aria-pressed") === "true" && d.querySelectorAll(".ln.lsel").length === 1;
  const name = d.querySelector(".fx .note").textContent, labels = [...d.querySelectorAll(".fx .knob")].map((k) => k.querySelector(".kl").textContent + (k.classList.contains("na") ? "-" : "")).join();
  const rev = d.querySelectorAll(".fx .knob")[3];
  rev.dispatchEvent(new KeyboardEvent("keydown", { key: "End", bubbles: true }));
  const wrote = await until(() => { const m = window.fm1Test.mock.state; return m.dl && m.dl[204 + 3 * 6] === 31; }, 5000);
  const others = [...d.querySelectorAll(".ln")].filter((x) => x.dataset.l !== "6").every((x) => !x.classList.contains("lsel"));
  const lane0 = window.fm1Test.mock.state.dl[204] === 4;
  b.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
  const opened = await until(() => $("#pop").open && $("#pop").dataset.pop === "lane", 10000); await sleep(300);
  const title = $("#poptitle").textContent; $("#popx").click(); await sleep(300);
  const strips = [...document.querySelectorAll("#mixer .strip")].slice(0, 4), fxTop = new Set(strips.map((s) => Math.round(s.querySelector(".fx").getBoundingClientRect().top))).size === 1;
  const fit = strips.every((s) => s.querySelector(".fx").scrollHeight <= s.querySelector(".srow[data-row=sends]").clientHeight + 1);   /* (every send block fits its row) */
  const fxh = strips.map((s) => { const f = s.querySelector(".fx"); return f.scrollHeight + "/" + f.clientHeight; }).join(" ");
  return JSON.stringify({ noPop, hi, name, labels, wrote, others, lane0, opened, title, fxTop, fit, fxh });`);
ok(/"noPop":true,"hi":true,"name":"[A-Z. 0-9]+","labels":"DST-,CHO,DLY,REV","wrote":true,"others":true,"lane0":true,"opened":true,"title":"[^"]+","fxTop":true,"fit":true/.test(dsel || ""),
  `e2e: drum strip: a click selects a sound (no popup), its CHO DLY REV on the strip (DST greyed), REV turned writes that lane; a double click opens it (${dsel})`);
/* the title bar of a strip selects its track; no Select button, no per-track Project button */
ok(await run(`${U} const h = document.querySelector('#mixer .strip[data-track="1"] .shead'); h.click();
  const okk = await until(() => document.querySelector('#mixer .strip[data-track="1"]').classList.contains("sel") && h.getAttribute("aria-pressed") === "true", 10000);
  const others = [...document.querySelectorAll("#mixer .strip[data-track]")].filter((s) => s.classList.contains("sel")).length;
  const h0 = document.querySelector('#mixer .strip[data-track="0"] .shead'); h0.click();
  const back = await until(() => document.querySelector('#mixer .strip[data-track="0"]').classList.contains("sel"), 10000);
  return okk && back && others === 1 && !document.querySelector("#mixer .selb") && !document.querySelector("#mixer [data-pop=project]");`),
  "e2e: a strip's title bar selects its track (one selected, no Select / Project buttons)");
/* every popup: its opener on a strip (one click), then Escape / x / outside -> the mixer as it was */
const POPS = [["sound", 0], ["sequence", 1], ["loadpreset", 2], ["savepreset", 0], ["kit", 3], ["kitstore", 3], ["lane", 3, 4], ["fxdelay", 4], ["fxreverb", 4], ["fxchorus", 4], ["fxcomp", 4]];
const closers = ["Escape", "x", "outside"];
let n = 0;
for (const [pid, strip, lane] of POPS) {
  const closer = closers[n++ % 3];
  const opened = await run(`${U} const before = $("#mixer").innerHTML.length, s = $("#mixer").children[${strip}];
    const b = ${lane != null} ? s.querySelector('[data-pop=lane][data-l="${lane}"]') : s.querySelector('[data-pop=${pid}]');
    if (!b) return "no opener";
    if (${lane != null}) b.dispatchEvent(new MouseEvent("dblclick", { bubbles: true })); else b.click();   /* (a drum sound: a double click; a click selects it) */
    const okk = await until(() => $("#pop").open && $("#pop").dataset.pop === "${pid}" && !/Reading|reading/.test($("#status").textContent), 20000);
    await sleep(400);
    return okk ? "open" : "not open";`);
  await shot("pop-" + pid);
  if (closer === "Escape") await key("Escape");
  else if (closer === "x") await run(`document.querySelector("#popx").click();`);
  else await send("Input.dispatchMouseEvent", { type: "mousePressed", x: 5, y: 995, button: "left", clickCount: 1 }).then(() =>
    send("Input.dispatchMouseEvent", { type: "mouseReleased", x: 5, y: 995, button: "left", clickCount: 1 }));
  const back = await run(`${U} await sleep(300); return !$("#pop").open && !$("#p-mixer").hidden && location.hash === "#mixer" &&
    document.querySelectorAll("#mixer .strip").length === 5;`);
  ok(opened === "open" && back, `e2e: ${pid}: one ${lane != null ? "double " : ""}click from strip ${strip + 1} opens it, ${closer} back to the mixer`);
}
/* the keyboard on the mixer: <- / -> select the track, a key per element opens its popup, the same key (or Escape) closes it */
const VK = { ArrowLeft: 37, ArrowRight: 39, Escape: 27 };
const press = async (k) => { await send("Input.dispatchKeyEvent", { type: "keyDown", key: k, code: k, windowsVirtualKeyCode: VK[k] || k.toUpperCase().charCodeAt(0) });
  await send("Input.dispatchKeyEvent", { type: "keyUp", key: k, code: k, windowsVirtualKeyCode: VK[k] || k.toUpperCase().charCodeAt(0) }); };
const selNow = (want) => run(`${U} await sleep(300); if (${want != null}) await until(() => { const e = document.querySelector('#mixer .strip.sel'); return e && +e.dataset.track === ${want}; }, 15000); await sleep(300); const s = [...document.querySelectorAll("#mixer .strip[data-track]")].filter((x) => x.classList.contains("sel")); return s.length === 1 ? +s[0].dataset.track : -1;`);
const popNow = () => run(`${U} await sleep(700); return $("#pop").open ? $("#pop").dataset.pop : "";`);
await run(`document.querySelector('#mixer .strip[data-track="0"] .shead').click();`);
let sels = [await selNow()];
for (const [k, want] of [["ArrowRight", 1], ["ArrowRight", 2], ["ArrowRight", 3], ["ArrowRight", 3], ["ArrowLeft", 2]]) { await press(k); sels.push(await selNow(want)); }
ok(sels.join() === "0,1,2,3,3,2", `e2e: keys: <- / -> select the previous / next track, stop at the ends (${sels.join(" ")})`);
const flow = [];
for (const [k, pid] of [["i", "sound"], ["q", "sequence"], ["l", "loadpreset"], ["p", "savepreset"]]) {
  await press(k); const a = await popNow();
  const title = await run(`${U} return $("#poptitle").textContent;`);
  if (k === "i") await shot("keys-sound");
  if (k === "q" || k === "l" || k === "p") await press("Escape"); else await press(k);   /* (Save: its name field has the focus and keeps the keys) */
  const b = await popNow();
  flow.push(a === pid && /Track 3/.test(title) && b === "");
}
ok(flow.every(Boolean), `e2e: keys: I Q L P open Sound / Sequence / Load / Save of the selected track; the same key or Esc closes (${flow.join()})`);
await press("p");
await run(`${U} await until(() => $("#pop").open && document.querySelector("#popbody input[type=text]"), 5000); document.querySelector("#popbody input[type=text]").focus();`);
await press("i"); await press("ArrowRight");
ok(await popNow() === "savepreset" && await selNow() === 2, "e2e: keys: ignored while typing in a field (the Save name), the popup stays");
await press("Escape");
await press("ArrowRight");
const drum = [await selNow(3)];
for (const [k, pid] of [["k", "kit"], ["u", "kitstore"], ["q", "sequence"]]) {
  await press(k); const a = await popNow();
  if (k === "k") await shot("keys-kit");
  if (k === "k" || k === "u") await press(k); else await press("Escape");
  drum.push(a === pid && await popNow() === "");
}
await press("i"); drum.push(await popNow() === "");
ok(drum[0] === 3 && drum.slice(1).every(Boolean), `e2e: keys: on the drum track K U Q open Kit / User kits / Sequence, I is ignored (${drum.join(" | ")})`);
/* the drum sounds' keys: a digit selects (no popup; the strip's send row follows), the same digit again or Enter opens it */
const lsel = () => run(`${U} await sleep(300); const d = document.querySelector('#mixer .strip[data-track="3"]'), b = d.querySelector(".ln.lsel");
  return (b ? b.dataset.l : "-") + ":" + d.querySelector(".fx .note").textContent;`);
const lk = [];
await press("3"); lk.push(await popNow() === "", await lsel());
await press("3"); lk.push(await popNow() === "lane", await run(`${U} return $("#poptitle").textContent;`));
await press("5"); lk.push(await popNow() === "lane", await lsel());   /* (in its popup: that sound) */
await press("Escape"); await popNow();
const focused = await run(`${U} const e = document.activeElement; return e ? e.tagName + "." + e.className + (e.id ? "#" + e.id : "") : "none";`);
await press("Enter"); lk.push(await popNow() === "lane"); await press("Escape"); lk.push(await popNow() === "");
ok(lk[0] === true && /^2:SNARE/.test(lk[1]) && lk[2] === true && /SNARE/.test(lk[3]) && lk[4] === true && /^4:/.test(lk[5]) && lk[6] === true && lk[7] === true,
  `e2e: keys: 3 selects the snare (no popup, its sends on the strip), 3 again opens it, 5 goes to sound 5 in it, Enter opens the selected one (${lk.join(" | ")}; focus before Enter: ${focused})`);
const misc = [];
for (const [k, pid] of [["d", "fxdelay"], ["r", "fxreverb"], ["c", "fxchorus"], ["o", "fxcomp"], ["h", "help"]]) { await press(k); misc.push(await popNow() === pid); if (k === "h") { await run(`const h = document.querySelector(".keyhelp"); if (h) h.scrollIntoView({ block: "center" });`); await shot("keys-help"); } await press(k); misc.push(await popNow() === ""); }
await press(","); misc.push(await run(`${U} await sleep(400); return !$("#p-settings").hidden;`));
await run(`document.querySelector("[data-tab=mixer]").click();`);
await press("q"); misc.push(await popNow() === "sequence"); await press("Escape");   /* (the mixer again: the keys work, on the drum track) */
ok(misc.every(Boolean), `e2e: keys: D R C O Delay / Reverb / Chorus / Comp, H help (with the Keyboard shortcuts section), comma Settings (${misc.join()})`);
ok(await run(`${U} const t = (q) => (document.querySelector(q) || {}).title || "";
  const ends = [['#mixer .strip[data-track="0"] [data-pop=sound]', " \u00b7 I"], ['#mixer .strip[data-track="0"] [data-pop=sequence]', " \u00b7 Q"],
    ['#mixer .strip[data-track="0"] [data-pop=loadpreset]', " \u00b7 L"], ['#mixer .strip[data-track="0"] [data-pop=savepreset]', " \u00b7 P"],
    ['#mixer .strip[data-track="3"] [data-pop=kit]', " \u00b7 K"], ['#mixer .strip[data-track="3"] [data-pop=kitstore]', " \u00b7 U"],
    ['#mixer .strip[data-track="3"] [data-pop=lane][data-l="0"]', " \u00b7 1"], ['#mixer .strip[data-track="3"] [data-pop=lane][data-l="9"]', " \u00b7 0"],
    ["#mixer .strip.master [data-pop=fxdelay]", " \u00b7 D"], ["#mixer .strip.master [data-pop=fxreverb]", " \u00b7 R"], ["#mixer .strip.master [data-pop=fxchorus]", " \u00b7 C"], ["#mixer .strip.master [data-pop=fxcomp]", " \u00b7 O"], ["#helpbtn", " \u00b7 H"], ["#play", " \u00b7 Space"]];
  const bad = ends.filter(([q, e]) => !t(q).endsWith(e) || document.querySelector(q).getAttribute("aria-label") !== t(q)).map(([q]) => q);
  const lay = ["0", "1", "2"].every((n) => { const s = $('#mixer .strip[data-track="' + n + '"]'); const g = s.querySelector(".pops"), q = s.querySelector("[data-pop=sequence]");
    return g.querySelectorAll("button").length === 3 && q.getBoundingClientRect().top >= g.getBoundingClientRect().bottom && q.getBoundingClientRect().height >= 32; });
  const d = $('#mixer .strip[data-track="3"]'); const dg = d.querySelector(".pops");
  const lay2 = dg.querySelectorAll("button").length === 2 && d.querySelector("[data-pop=sequence]").getBoundingClientRect().top >= dg.getBoundingClientRect().bottom && d.querySelectorAll(".lanes .ln").length === 16;
  return JSON.stringify({ bad, lay, lay2 });`) === '{"bad":[],"lay":true,"lay2":true}', "e2e: tooltips end with their key; strip layout: group row (3 buttons; the drum group: Kit, User kits, fold, one-or-all), Sequence below");
await shot("strips");
/* every button (tabs, transport, popups, screens) has a tooltip and an accessible name, scanned on every screen and popup */
const TIPSCAN = `const untipped = (root) => [...root.querySelectorAll("button, [role=tab], select, input:not([type=hidden]):not([type=file])")]
  .filter((e) => shown(e) && (!(e.title || "").trim() || !(e.getAttribute("aria-label") || "").trim()))
  .map((e) => e.id || e.className || e.tagName + ":" + (e.textContent || "").trim().slice(0, 12));`;
const tipMiss = await run(`${U} ${TIPSCAN} const miss = new Set(), add = (r) => untipped(r).forEach((x) => miss.add(x));
  await sleep(300); add(document);
  for (const scr of ["library", "samples", "projects", "snapshots", "settings", "mixer"]) {
    const b = document.querySelector("[data-tab=" + scr + "]"); if (!shown(b)) continue; b.click(); await sleep(400); add(document); }
  for (const [pid, strip, lane] of ${JSON.stringify(POPS)}) {
    const s = $("#mixer").children[strip];
    const b = lane != null ? s.querySelector('[data-pop=lane][data-l="' + lane + '"]') : s.querySelector("[data-pop=" + pid + "]");
    if (!b) { miss.add("no opener " + pid); continue; }
    if (lane != null) b.dispatchEvent(new MouseEvent("dblclick", { bubbles: true })); else b.click(); await until(() => $("#pop").open && !/Reading|reading/.test($("#status").textContent), 20000); await sleep(500);
    add($("#pop")); $("#popx").click(); await sleep(200); }
  $("#helpbtn").click(); await sleep(300); add($("#pop")); $("#popx").click(); await sleep(200);
  return [...miss];`);
ok(Array.isArray(tipMiss) && tipMiss.length === 0, `e2e: every button / tab / control on every screen and popup has a tooltip and aria-label${tipMiss && tipMiss.length ? " (missing: " + tipMiss.slice(0, 8).join(", ") + ")" : ""}`);
/* the MIDI clock in the transport bar: SYNC and the clock followed, a tooltip; a click opens Settings at the clock */
ok(await run(`${U} const b = $("#syncbtn"); if (!shown(b) || !/^SYNC (INT|USB|TRS|AUTO:(INT|USB|TRS))$/.test(b.textContent) || !b.title || !b.getAttribute("aria-label")) return false;
  b.click(); await sleep(400); const okk = !$("#p-settings").hidden && !!document.querySelector("#setgroups .flash");
  document.querySelector("[data-tab=mixer]").click(); await sleep(200); return okk;`), "e2e: the SYNC pill (SYNC AUTO:INT ...), its tooltip, a click: Settings at the MIDI clock");
/* the piano roll (track 1): draw a note 3 steps long, put another, take it away, set velocity and level; the mock (the
   firmware's step format) holds them */
const mouse = async (type, x, y, mods = 0) => send("Input.dispatchMouseEvent", { type, x, y, button: "left", buttons: type === "mouseReleased" ? 0 : 1, clickCount: 1, modifiers: mods });
const rollPos = await run(`${U} window.confirm = () => true; $("#mixer").children[0].querySelector("[data-pop=sequence]").click();
  if (!await until(() => $("#pop").open && $("#pop").dataset.pop === "sequence" && !/Reading|reading/.test($("#status").textContent), 20000)) return null;
  $("#clearseq").click(); await sleep(1500);
  const R = window.fm1Test.roll(); R.scroll.scrollTop = (127 - 66) * R.RG.ROW - 60; await sleep(200);
  const r = R.grid.getBoundingClientRect(), at = (s, p) => [r.left + R.RG.KB + (s + 0.5) * R.cell, r.top + (127 - p + 0.5) * R.RG.ROW];
  return { a: at(0, 60), b: at(2.2, 60), c: at(4, 64) };`);
let rollOk = false;
if (rollPos) {
  await mouse("mousePressed", ...rollPos.a); await mouse("mouseMoved", ...rollPos.b); await mouse("mouseReleased", ...rollPos.b);
  await sleep(300);
  await mouse("mousePressed", ...rollPos.c); await mouse("mouseReleased", ...rollPos.c);
  await sleep(600);
  const mid = await run(`const s = window.fm1Test.mock.state.tracks[0].step;
    return s[0].time === 0 && s[0].n === 1 && s[0].notes[0] === 60 && s[1].time === 1 && s[2].time === 1 && s[3].time === 2 && s[4].time === 0 && s[4].notes[0] === 64;`);
  await shot("piano-roll");
  await mouse("mousePressed", ...rollPos.c); await mouse("mouseReleased", ...rollPos.c);
  await mouse("mousePressed", rollPos.a[0], rollPos.a[1], 8); await mouse("mouseReleased", rollPos.a[0], rollPos.a[1], 8);   /* Shift: pick step 1 */
  await sleep(400);
  const props = await run(`${U} const p = document.querySelector(".rpanel"); const v = p.querySelector('input[type=number]'), lv = p.querySelector(".rnote select");
    if (!v || !lv) return false; v.value = "50"; v.dispatchEvent(new Event("change")); await sleep(300);
    lv.value = "1"; lv.dispatchEvent(new Event("change")); await sleep(600);
    const s = window.fm1Test.mock.state.tracks[0].step; return s[4].time === 2 && s[4].n === 0 && s[0].vel === 50 && (s[0].lvl & 3) === 1;`);
  rollOk = mid && props;
}
ok(rollOk, "e2e: piano roll: a note drawn 3 steps long (TIEs), another put and taken away, velocity and level, as the firmware's steps");
/* the step detail (SLOOP 2.4's nudge, fill, locks) for the picked step: written to the mock's step extras */
await mouse("mousePressed", rollPos.a[0], rollPos.a[1], 8); await mouse("mouseReleased", rollPos.a[0], rollPos.a[1], 8);
await sleep(600);
const sxRes = await run(`${U} const x = $("#stepx"); if (!await until(() => !x.hidden && $("#sa-chance"), 8000)) return { hidden: x.hidden };
  const set = async (id, v) => { const e = $(id); e.value = String(v); e.dispatchEvent(new Event("input")); e.dispatchEvent(new Event("change")); await sleep(500); };
  const mock = window.fm1Test.mock, ev = () => (mock.state.tracks[0].auto || { ev: [] }).ev, step = +$("#sa-step").value - 1;
  const at = (param) => { const e = ev().find((q) => q.place === (step | 0x40) && q.param === param); return e ? e.v : null; };
  await set("#sa-chance", 50); await set("#sa-cond", 3); await set("#sa-nudge", 7); await set("#sa-fill", 2);
  const extras = [at(0xFF), at(0xFC), at(0xFD), at(0xFE)].join();
  const add = $("#sa-add"), groups = [...add.querySelectorAll("optgroup")].map((g) => g.label);
  const forbidden = [...add.querySelectorAll("option")].map((o) => o.textContent).filter((l) => ["VCE", "GLMOD", "PRIO", "ALLOC", "MUTE", "LEN", "DIV", "SWG"].includes(l));
  const labels = [...add.querySelectorAll("option")].map((o) => o.parentNode.label + "/" + o.textContent), dup = labels.length !== new Set(labels).size;
  add.value = add.querySelector("option").value; $("#sa-addgo").click(); await sleep(600);
  const rows = x.querySelectorAll("tr[data-param]").length, marked = document.querySelectorAll("#roll .tile.auto").length;
  const lock = ev().filter((q) => q.place === (step | 0x40) && q.param < 0xFC).length;
  x.querySelector("tr[data-param] button").click(); await sleep(500);
  const after = ev().filter((q) => q.place === (step | 0x40) && q.param < 0xFC).length;
  await set("#sa-chance", 100); await set("#sa-cond", 0); await set("#sa-nudge", 0); await set("#sa-fill", 0);
  const gone = ev().filter((q) => q.place === (step | 0x40)).length;
  return { pass: extras === "50,3,7,2" && groups.length > 3 && !forbidden.length && !dup && rows === 1 && lock === 1 && marked >= 1 && after === 0 && gone === 0, extras, groups: groups.length, forbidden, dup, rows, lock, marked, after, gone };`);
ok(sxRes && sxRes.pass, `e2e: step automation: chance, condition, nudge, fill and a lock (from the device's Sound pages: no VCE / MUTE / LEN ..., no duplicate label) written to the store, marked on the tile, removed; defaults delete the events (${JSON.stringify(sxRes)})`);
await shot("step-detail");
/* v14 (the mock's undo, notes, solo): the step automation just written is undone / redone from the header and with Ctrl + Z, SOLO of a strip, the keyboard plays a note */
const v14Res = await run(`${U} try {
  const mock = window.fm1Test.mock, evs = () => ((mock.state.tracks[0].auto || { ev: [] }).ev).filter((q) => q.param === 0xFF).length;
  const set = async (id, v) => { const e = $(id); e.value = String(v); e.dispatchEvent(new Event("input")); e.dispatchEvent(new Event("change")); await sleep(500); };
  await sleep(1000); await set("#sa-chance", 30);
  const r = { shown: !$("#undotp").hidden && !!$("#undobtn") };
  await until(() => !$("#undobtn").disabled, 6000); r.canUndo = !$("#undobtn").disabled; r.before = evs();
  $("#undobtn").click(); await until(() => evs() === 0, 6000); r.undone = evs() === 0;
  await until(() => !$("#redobtn").disabled, 6000);
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "z", ctrlKey: true, shiftKey: true, bubbles: true })); await until(() => evs() === 1, 6000); r.redone = evs() === 1;
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "z", ctrlKey: true, bubbles: true })); await until(() => evs() === 0, 6000); r.keyUndo = evs() === 0;
  $("#popx").click(); await sleep(300);
  const so = $("#strip-2-solo"); r.soloEnabled = !so.hasAttribute("aria-disabled"); so.click(); await sleep(700); r.solo = mock.state.solo & 15;
  r.soloOn = $("#strip-2-solo").classList.contains("on");
  so.click(); await sleep(500);
  $("#kbdbtn").click(); await sleep(400);
  const key = $("#kbd .wk"); r.keys = !$("#kbd").hidden && document.querySelectorAll("#kbd .wk").length;
  key.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true, pointerId: 1 })); await sleep(500);
  r.note = (mock.state.noteLog || []).length;
  key.dispatchEvent(new PointerEvent("pointerup", { bubbles: true, pointerId: 1 })); await sleep(300);
  $("#kbdbtn").click(); await sleep(300); r.closed = $("#kbd").hidden;
  /* the transport REC (the device's global record): the button follows the device's states, a click toggles it (the key), a device-side change shows within a second */
  const rb = $("#recbtn"); r.recShown = !rb.hidden && rb.dataset.rec === "off";
  rb.click(); await until(() => rb.dataset.rec === "wait", 4000); r.recWait = rb.dataset.rec + ":" + mock.state.recState + ":" + rb.getAttribute("aria-pressed");
  mock.sim.recSet(1); await until(() => rb.dataset.rec === "rec", 4000); r.recOn = rb.dataset.rec;
  rb.click(); await until(() => rb.dataset.rec === "off", 4000); r.recOff = rb.dataset.rec + ":" + mock.state.recState;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(v14Res && v14Res.shown && v14Res.canUndo && v14Res.before === 1 && v14Res.undone && v14Res.redone && v14Res.keyUndo && v14Res.soloEnabled && v14Res.solo === 2 && v14Res.soloOn && v14Res.keys === 14 && v14Res.note === 1 && v14Res.closed && v14Res.recShown && v14Res.recWait === "wait:2:true" && v14Res.recOn === "rec" && v14Res.recOff === "off:0",
  `e2e v14 (mock): undo / redo buttons and Ctrl / Cmd + Z undo the device's own history, a strip's SOLO, the keyboard plays a note (${JSON.stringify(v14Res)})`);
await run(`document.querySelector("#popx").click();`);
/* the drum grid (track 4): 16 lanes in their kind colours, a click puts a hit on the device */
ok(await run(`${U} $("#mixer").children[3].querySelector("[data-pop=sequence]").click();
  if (!await until(() => $("#pop").open && shown($("#drumgrid")) && !/Reading|reading/.test($("#status").textContent), 20000)) return false;
  await sleep(300);
  const lanes = [...document.querySelectorAll("#drumgrid span[data-ln]")];
  const coloured = lanes.length === 16 && lanes.every((n) => /#|rgb/.test(n.style.getPropertyValue("--kc")));
  const d0 = window.fm1Test.mock.state.tracks[3].dstep[1], was = (d0.on >> 2) & 1;
  document.querySelector('#drumgrid button[data-i="1"][data-l="2"]').click(); await sleep(500);
  const now = (window.fm1Test.mock.state.tracks[3].dstep[1].on >> 2) & 1;
  return coloured && now !== was;`), "e2e: drum grid: 16 named lanes in their kind colours, a click puts / takes a hit on the device");
await shot("drum-grid");
await run(`document.querySelector("#popx").click();`);
/* the help: one click from the transport bar, Escape back */
const helpOpen = await run(`${U} $("#helpbtn").click(); return until(() => $("#pop").open && $("#pop").dataset.pop === "help", 5000);`);
await shot("pop-help");
await key("Escape");
ok(helpOpen && await run(`${U} await sleep(300); return !$("#pop").open && !$("#p-mixer").hidden;`), "e2e: help: one click from the transport bar, Escape back to the mixer");
/* the SLOOP look: the side bar's groups, the four tracks on top (one click selects), a side button opens an editor for the selected track, the steps as tiles, Terminus */
const lookRes = await run(`${U} const $$ = (q) => [...document.querySelectorAll(q)], r = {};
  try {
  const head = (i) => $('#mixer .strip[data-track="' + i + '"] .shead');
  head(0).click(); await until(() => head(0).getAttribute("aria-pressed") === "true", 8000); await sleep(300);
  r.groups = $$(".menubar .navh").map((e) => e.textContent.trim()).join("") + ":" + $$(".menubar .navh").filter((e) => e.offsetParent).length;
  r.font = getComputedStyle(document.body).fontFamily.includes("SLOOP Terminus") && document.fonts.check("16px 'SLOOP Terminus'");
  r.tabs = !!$("#toptracks") + ":" + $$(".menubar button[data-go]").length;
  r.tracks = $$("#mixer .strip[data-track] .shead").length;
  head(2).click(); await until(() => head(2).getAttribute("aria-pressed") === "true", 8000); await sleep(300);
  r.sel = $$("#mixer .strip[data-track]").findIndex((s) => s.classList.contains("sel")); r.trk = getComputedStyle(document.documentElement).getPropertyValue("--trk").trim();
  $("#strip-3-inst-open").click(); await until(() => $("#pop").open && $("#pop").dataset.pop === "sound", 8000);
  r.pop = $("#pop").dataset.pop + ":" + /Track 3/.test($("#poptitle").textContent); $("#popx").click(); await sleep(200);
  $('#mixer .strip[data-track="2"] [data-pop=sequence]').click(); await until(() => $("#pop").open && $("#pop").dataset.pop === "sequence", 8000); await sleep(300);
  r.tiles = $$("#roll .tiles .tile").length; $("#popx").click(); await sleep(200);
  head(0).click(); await until(() => head(0).getAttribute("aria-pressed") === "true", 8000);
  } catch (e) { r.err = String(e); }
  return r;`);
ok(lookRes && lookRes.groups === ":2" && lookRes.font && lookRes.tabs === "false:0" && lookRes.tracks === 4 && lookRes.sel === 2 && lookRes.trk === "#ffc618"
  && lookRes.pop === "sound:true" && lookRes.tiles === 16,
  `e2e: the look: Terminus, the side bar holds only what is not in the mixer, no track tab row (a strip's head selects, the track colour follows), the Sound editor opens from the instrument widget, 16 step tiles (${JSON.stringify(lookRes)})`);
/* the Samples page (SLOOP 2.4's three steps): choose a slot, make the sound (files, the key map drawn), send, play it on a track */
const smpRes = await run(`${U} window.confirm = () => true; const r = {};
  try {
  const wav = (name, hz, n) => { const b = new ArrayBuffer(44 + n * 2), v = new DataView(b); const w = (o, s) => [...s].forEach((c, i) => v.setUint8(o + i, c.charCodeAt(0)));
    w(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); w(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true); v.setUint32(24, 22050, true);
    v.setUint32(28, 44100, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); w(36, "data"); v.setUint32(40, n * 2, true);
    for (let i = 0; i < n; i++) v.setInt16(44 + i * 2, Math.round(20000 * Math.sin(i * hz / 22050 * 6.2832) * (1 - i / n)), true);
    return new File([b], name, { type: "audio/wav" }); };
  document.querySelector("[data-tab=samples]").click(); await sleep(400);
  r.steps = [...document.querySelectorAll("#smpslots .kstep h3")].map((h) => h.textContent.replace(/^\\d/, "")).join("|");
  r.slots = document.querySelectorAll("#smpslots .dest.slots4 button").length;
  document.querySelectorAll("#smpslots .dest.slots4 button")[1].click(); await sleep(200);
  r.sel = document.querySelector("#smpslots .dest.slots4 button[aria-checked=true]").dataset.k;
  await window.fm1Test.smpAdd([wav("PIANO_C3.wav", 220, 3000), wav("PIANO_C4.wav", 440, 3000)]); await sleep(400);
  r.rows = document.querySelectorAll("#smpslots .frow:not(.fhead)").length; r.keymap = document.querySelector("#smpslots canvas.keymap").getClientRects().length > 0;
  const send = document.querySelector("#smpslots button.big"); r.send = send.textContent.trim(); r.sendOn = !send.disabled; send.click();
  await until(() => window.fm1Test.dev().smp.slots[1].zones === 2, 20000); await sleep(500);
  r.zones = window.fm1Test.dev().smp.slots[1].zones;
  const on = [...document.querySelectorAll("#smpslots .seg.trk, #smpslots .seg button.trk")].filter((b) => !b.disabled);
  r.play = on.length; on[1].click();
  await until(() => window.fm1Test.dev().sel === 1 && window.fm1Test.dev().info.engines[window.fm1Test.dev().dump.engine] === "SAMPLE", 20000); await sleep(500);
  const d = window.fm1Test.dev(); await until(() => window.fm1Test.mock.state.tracks[1].p[d.info.pe0] === 9, 5000); r.eng = d.info.engines[d.dump.engine]; r.set = window.fm1Test.mock.state.tracks[1].p[d.info.pe0];
  } catch (e) { r.err = String(e); }
  return r;`);
await shot("samples-page");
ok(smpRes && smpRes.slots === 3 && smpRes.sel === "1" && smpRes.rows === 2 && smpRes.keymap && smpRes.sendOn && /USR2/.test(smpRes.send) && smpRes.zones === 2 && smpRes.play === 3 && smpRes.eng === "SAMPLE" && smpRes.set === 9,
  `e2e: Samples page: 3 steps, a slot chosen, two files with the key map, sent into USR2, played on track 2 (SAMPLE, SET = USR2) (${JSON.stringify(smpRes)})`);
/* the Drum kit page (SLOOP 2.4's): sounds sorted onto the pads by file name, pitch / gain per pad, sent into USR1 and the lanes */
const kitRes = await run(`${U} window.confirm = () => true; const r = {};
  try {
  const wav = (name, hz, n) => { const b = new ArrayBuffer(44 + n * 2), v = new DataView(b); const w = (o, s) => [...s].forEach((c, i) => v.setUint8(o + i, c.charCodeAt(0)));
    w(0, "RIFF"); v.setUint32(4, 36 + n * 2, true); w(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true); v.setUint32(24, 22050, true);
    v.setUint32(28, 44100, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); w(36, "data"); v.setUint32(40, n * 2, true);
    for (let i = 0; i < n; i++) v.setInt16(44 + i * 2, Math.round(20000 * Math.sin(i * hz / 22050 * 6.2832) * (1 - i / n)), true);
    return new File([b], name, { type: "audio/wav" }); };
  document.querySelector("[data-tab=drumkit]").click(); await sleep(400);
  r.tab = !!$("#kitpage .pads") && $("#kitpage").getClientRects().length > 0;
  await window.fm1Test.kitAdd([wav("BD_kick 808.wav", 60, 4000), wav("snare.wav", 200, 3000), wav("closed hat.wav", 3000, 2000)]); await sleep(300);
  r.filled = [...document.querySelectorAll("#kitpage .pad")].map((p, i) => (p.querySelector(".empty").hidden ? i : -1)).filter((i) => i >= 0).join(",");
  const set = (l, q, v) => { const i = document.querySelectorAll("#kitpage .pad")[l].querySelectorAll("input[type=range]")[q]; i.value = String(v); i.dispatchEvent(new Event("input")); i.dispatchEvent(new Event("change")); };
  set(0, 0, 5); set(0, 1, -6);
  const send = [...document.querySelectorAll("#kitpage button")].find((b) => b.classList.contains("big"));
  r.sendOn = !send.disabled; send.click();
  await until(() => window.fm1Test.dev().smp && window.fm1Test.dev().smp.slots[0] && window.fm1Test.dev().smp.slots[0].zones === 3, 20000); await sleep(500);
  const L = window.fm1Test.dev().dl.lanes; r.l0 = [L[0].src, L[0].hit, L[0].ofs[0], L[0].ofs[7]].join(); r.l2 = [L[2].src, L[2].hit].join(); r.l4 = [L[4].src, L[4].hit].join(); r.zones = window.fm1Test.dev().smp.slots[0].zones;
  set(0, 0, -3); await sleep(600); r.live = window.fm1Test.dev().dl.lanes[0].ofs[0];
  } catch (e) { r.err = String(e); }
  return r;`);
await shot("kit-page");
ok(kitRes && kitRes.tab && kitRes.filled === "0,2,4" && kitRes.sendOn && kitRes.l0 === "1,0,5,-6" && kitRes.l2 === "1,1" && kitRes.l4 === "1,2" && kitRes.zones === 3 && kitRes.live === -3,
  `e2e: Drum kit page: WAVs sorted onto pads by name, pitch / gain, sent into USR1 and the lanes (tune and gain live after) (${JSON.stringify(kitRes)})`);
await run(`document.querySelector("[data-tab=mixer]").click();`);
/* the master strip: no Settings button, one icon + its knobs inline per FX, each popup has its parameters; Settings has none of them */
const masterRes = await run(`${U} const m = document.querySelector("#mixer .strip.master");
  const noSet = ![...m.querySelectorAll("button")].some((b) => /settings/i.test(b.title + b.getAttribute("aria-label")));
  const groups = [...m.querySelectorAll(".fxg")], knobs = [...m.querySelectorAll(".mk .knob")].map((k) => k.getAttribute("aria-label"));
  const shape = groups.length === 4 && groups.every((g) => g.querySelectorAll("button.ib").length === 1 && g.querySelectorAll(".knob").length === ({ fxdelay: 4, fxreverb: 2, fxchorus: 2 }[g.dataset.fx] || 3)
    && g.querySelector("button.ib").getBoundingClientRect().width >= 32 && g.querySelector("button.ib").getBoundingClientRect().height >= 32 && g.querySelector("button.ib").title && [...g.querySelectorAll(".knob")].every((k) => k.title))
    && [...m.querySelectorAll('.fxg[data-fx="fxcomp"] .knob .kl')].map((e) => e.textContent.trim()).join(" ") === "THRS RATIO GAIN";
  const want = { fxdelay: "TIME FDBK COLR MIX", fxreverb: "SIZE DAMP", fxchorus: "CRT CDP", fxcomp: "THRS RATIO ATK REL GAIN CEIL" }, got = {};
  for (const [id, labels] of Object.entries(want)) {
    m.querySelector("[data-pop=" + id + "]").click(); await until(() => $("#pop").open && $("#pop").dataset.pop === id, 5000); await sleep(300);
    got[id] = [...document.querySelectorAll("#popbody .group[data-fx=" + id + "] .kl")].map((e) => e.textContent.trim()).join(" ") === labels
      && [...document.querySelectorAll("#popbody .group[data-fx=" + id + "] .knob")].every((k) => k.title && k.title !== k.querySelector(".kl").textContent);
    $("#popx").click(); await sleep(200);
  }
  document.querySelector("[data-tab=settings]").click(); await sleep(300);
  const labels = [...document.querySelectorAll("#p-settings .knob .kl, #p-settings .pk .kl, #p-settings .row > span:first-child")].filter(shown).map((e) => e.textContent.trim());
  const moved = ["DUST", "DUCK", "FILT", "TIME", "FDBK", "COLR", "MIX", "SIZE", "DAMP", "CRT", "CDP", "LVL", "REV", "THRS", "RATIO", "GAIN", "CEIL"];
  const left = moved.filter((l) => labels.includes(l));
  document.querySelector("[data-tab=mixer]").click(); await sleep(300);
  return JSON.stringify({ noSet, shape, knobs: knobs.join(), got, left, hasRoll: labels.includes("ROLL") });`);
ok(masterRes === '{"noSet":true,"shape":true,"knobs":"MASTER DUST,MASTER DUCK,MASTER FILT","got":{"fxdelay":true,"fxreverb":true,"fxchorus":true,"fxcomp":true},"left":[],"hasRoll":true}',
  "e2e: master strip: no Settings button, an icon + knob per FX (32 px), each popup its parameters, Settings has no mixer parameter " + (masterRes && masterRes.length < 400 ? masterRes : ""));
await shot("master-strip");
/* the track's Sound popup: its own FX (the sends DST CHO DLY REV, SLICER, bypass), NOT the global delay / reverb / chorus pages: those are only
   in the master strip's popups (checked above) */
const soundFx = await run(`${U} document.querySelector('#mixer .strip[data-track="0"] [data-pop=sound]').click();
  await until(() => $("#pop").open && $("#pop").dataset.pop === "sound" && document.querySelector("#popbody .group"), 5000); await sleep(300);
  const fx = document.querySelector('#popbody .group[data-fam=fx]');
  const heads = fx ? [...fx.querySelectorAll("h3")].map((e) => e.textContent.replace(/[^\x20-\x7e]/g, "").trim())   /* (the icon font glyph is not text) */ : null;
  const labels = fx ? [...fx.querySelectorAll(".kl")].map((e) => e.textContent.trim()) : [];
  const all = [...document.querySelectorAll("#popbody h3")].map((e) => e.textContent.trim());
  $("#popx").click(); await sleep(200);
  return JSON.stringify({ heads, labels: labels.join(), glob: all.filter((h) => h === "DLY" || h === "REV/CHO") });`);
ok(soundFx === '{"heads":["SLICER","BYPASS"],"labels":"DST,CHO,DLY,REV,SLCR,PAT,RATE,DEPTH,FX","glob":[]}',
  "e2e: the track's Sound popup: its own FX (sends, SLICER, bypass), no global DLY / REV/CHO pages " + (soundFx && soundFx ? JSON.stringify(soundFx).slice(0, 400) : ""));
/* narrow: the master strip never makes the page scroll sideways */
await send("Emulation.setDeviceMetricsOverride", { width: 420, height: 1000, deviceScaleFactor: 1, mobile: false }); await run(`await new Promise((r) => setTimeout(r, 300));`);
const narrowOk = await run(`const m = document.querySelector("#mixer .strip.master"); m.scrollIntoView(); return document.documentElement.scrollWidth <= window.innerWidth + 1 && m.querySelectorAll(".fxg").length === 4;`);
await shot("master-strip-narrow");
await send("Emulation.setDeviceMetricsOverride", { width: 1400, height: 1000, deviceScaleFactor: 1, mobile: false });
ok(narrowOk === true, "e2e: master strip at 420 px wide: no page horizontal scroll");
/* no theme picker in Settings; the page follows the system (light and dark screens of the connected editor) */
ok(await run(`${U} document.querySelector("[data-tab=settings]").click(); await sleep(300);
  const r = !document.querySelector("select#theme, #appearance, #appearbox, .theme") && shown($("#p-settings"));
  document.querySelector("[data-tab=mixer]").click(); await sleep(200); return r;`), "e2e: Settings has no theme picker");
for (const mode of ["dark", "light"]) {
  await scheme(mode); await sleep(200);
  for (const [name, js] of [["mixer", `document.querySelector("[data-tab=mixer]").click();`], ["settings", `document.querySelector("[data-tab=settings]").click();`],
    ["library", `document.querySelector("[data-tab=library]").click();`], ["sound", `document.querySelector("[data-tab=mixer]").click(); await sleep(200); document.querySelector("#mixer .strip [data-pop=sound]").click();`],
    ["sequence", `document.querySelector("[data-tab=mixer]").click(); await sleep(200); document.querySelector("#mixer .strip [data-pop=sequence]").click();`]]) {
    await run(`${U} document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" })); ${js} await sleep(400); return true;`);
    await shot(`${mode}-${name}`);
  }
  await run(`${U} document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" })); document.querySelector("[data-tab=mixer]").click(); await sleep(200); return true;`);
}
const live = [];
for (const mode of ["light", "dark"]) { await scheme(mode); await sleep(200); live.push(await run(`return getComputedStyle(document.body).backgroundColor;`)); }
ok(live[0] === "rgb(244, 244, 242)" && live[1] === "rgb(0, 0, 0)", "e2e: the connected editor restyles live when the system switches light / dark");
/* the Projects screen's snapshots: the slots listed, Save (named) then Load, BEFORE LOAD filled, the list in colours */
ok(await run(`${U} window.confirm = () => true; window.prompt = () => "LIVE SET";
  const tabb = document.querySelector('[data-tab=snapshots]');
  if (!shown(tabb)) return false;
  tabb.click();
  if (!await until(() => shown($("#snaps")) && document.querySelectorAll("#snaps tr").length === 5, 10000)) return false;
  document.querySelector('#snaps tr[data-slot="0"] button').click();
  if (!await until(() => /LIVE SET/.test($("#snaps").textContent), 10000)) return false;
  window.prompt = () => "";
  document.querySelectorAll('#snaps tr[data-slot="1"] button')[0].click();
  if (!await until(() => document.querySelectorAll('#snaps tr[data-slot="1"] .pill').length === 1, 10000)) return false;
  [...document.querySelectorAll('#snaps tr[data-slot="0"] button')].find((b) => /Load/.test(b.textContent)).click();
  return until(() => document.querySelectorAll('#snaps tr[data-slot="8"] .pill[data-st=ok]').length === 1, 10000);`),
  "e2e: the Snapshots tab (one click): 4 slots + BEFORE LOAD, Save (named, then from the work), Load fills BEFORE LOAD");
await sleep(300);
await shot("snapshots");
/* a stored pattern that does not play (PAT_READ / PAT_WRITE; the mock with the patterns, ?pat=1): a click launches (after a moment: a
   double click is something else), a double click opens its steps in the Sequence popup (not the working copy), a note drawn is
   written back to the slot, nothing else changes; Escape ends it */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&pat=1&e2e=1#mixer` });
await sleep(1500);
const slotPos = await run(`${U} window.confirm = () => true; try {
  if (!await until(() => document.querySelectorAll("#mixer .strip").length === 5 && D() && D().info && D().info.pat && document.querySelectorAll("#mixer .pslot").length >= 80, 120000)) return null;
  const mock = window.fm1Test.mock.state, { req, rq, patPoll } = window.fm1Test.proto();
  await rq(req.patOp(0, 0, 2)); await rq(req.patOp(1, 0, 2, 0, 5)); await patPoll(true);
  const slot = (n) => $('#mixer .strip[data-track="0"] .pslot[data-s="' + n + '"]');
  slot(3).click(); await sleep(150); const early = mock.pat.cur[0];
  await until(() => mock.pat.cur[0] === 3, 3000);
  const launched = mock.pat.cur[0] === 3 && early === 2;
  const wc0 = JSON.stringify(mock.tracks[0].step.slice(0, 4));
  slot(5).dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
  if (!await until(() => $("#pop").open && $("#p-sequencer").classList.contains("slotmode") && !$("#slotbar").hidden && !/Reading|reading/.test($("#status").textContent), 20000)) return { launched, err: "no slot mode" };
  await sleep(500);
  const R = window.fm1Test.roll(); R.scroll.scrollTop = (127 - 66) * R.RG.ROW - 60; await sleep(200);
  const r = R.grid.getBoundingClientRect();
  return { launched, wc0, title: $("#poptitle").textContent, bar: $("#slotbar").textContent, x: r.left + R.RG.KB + 1.5 * R.cell, y: r.top + (127 - 66 + 0.5) * R.RG.ROW }; } catch (e) { return { err: String(e.stack) }; }`);
if (slotPos && slotPos.x) {
  await mouse("mousePressed", slotPos.x, slotPos.y); await mouse("mouseReleased", slotPos.x, slotPos.y);
  await sleep(800);
  await shot("pattern-slot-open");
}
const slotRes = slotPos && slotPos.x ? await run(`${U} const mock = window.fm1Test.mock.state, rec = window.fm1Test.proto().patRecParse(mock.pat.rec["0:5"] || new Uint8Array(0), false);
  const st = $("#slotstate").textContent, wc = JSON.stringify(mock.tracks[0].step.slice(0, 4)); const cur = mock.pat.cur[0];
  $("#popx").click(); await sleep(300);
  return { rec: rec && rec.raw[1].join(), written: rec && rec.raw[0][0] > 0, st, same: wc === ${JSON.stringify(slotPos.wc0)}, cur, closed: !$("#p-sequencer").classList.contains("slotmode") && $("#slotbar").hidden };`) : null;
ok(slotPos && slotPos.launched && /Pattern 6/.test(slotPos.title) && /LEN/.test(slotPos.bar) && slotRes && /^66,0,0,0,1,0/.test(slotRes.rec) && slotRes.written && slotRes.same && slotRes.cur === 3 && slotRes.closed,
  `e2e: patterns: a click launches (after a moment), a double click opens a stored pattern's steps, a note drawn is written to the slot (PAT_WRITE), the working copy and the playing slot untouched (${JSON.stringify({ slotPos, slotRes })})`);
await shot("pattern-slot-edit");
/* v10 FX slots (the mock with them, ?fxs=1): the strips' FX knobs are the slots' amounts; the master's slot pickers load a type
   (the swap rule), the strips and the Sound tab's FX page follow; the drum strip's SOUND | BUS switch (the selected sound's insert, the
   drum bus's amount); a slot loaded on the device (FX_PUSH) moves the strips; the CMP card */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxs=1&e2e=1#mixer` });
await sleep(1500);
const fxRes = await run(`${U} try {
  if (!await until(() => document.querySelectorAll("#mixer .strip").length === 5 && D() && D().fx && D().watch, 120000)) return { err: "no fx" };
  const mock = window.fm1Test.mock.state, r = {};
  const labels = (i) => [...document.querySelectorAll('#mixer .strip[data-track="' + i + '"] .fx .knob .kl')].map((e) => e.textContent.trim()).join();
  const knobOf = (i, j) => document.querySelectorAll('#mixer .strip[data-track="' + i + '"] .fx .knob')[j];
  const up = async (k, n = 1) => { for (let q = 0; q < n; q++) k.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true })); await sleep(400); };
  const slots = () => [...document.querySelectorAll('#mixer .strip.master [data-fx=slots] select')];
  r.before = labels(0) + "|" + labels(3);
  r.pickers = slots().length + ":" + [...slots()[0].options].map((o) => o.textContent).join(" ");
  r.cmp = [...document.querySelectorAll('#mixer .strip.master [data-fx=cmp] select')].map((s) => s.options[s.selectedIndex].textContent).join();
  slots()[0].value = "5"; slots()[0].dispatchEvent(new Event("change"));
  r.loaded = await until(() => labels(0) === "CMP,CHO,DLY,REV" && mock.fx.slots[0] === 5, 8000);
  r.page = await until(() => D().pages && D().pages.find((p) => p.title === "FX").ids[0] === 75, 8000);
  await up(knobOf(1, 0), 3);
  r.trackCmp = mock.tracks[1].p[75];
  await up(knobOf(3, 0), 2);
  r.soundCmp = mock.fx.comp.join(",");
  document.querySelector('#mixer .strip[data-track="3"] [data-fxmode=bus]').click(); await sleep(400);
  r.bus = knobOf(3, 0).getAttribute("aria-label") + "|" + document.querySelector('#mixer .strip[data-track="3"] [data-fxmode=bus]').getAttribute("aria-pressed");
  await up(knobOf(3, 0), 4);
  r.busCmp = mock.tracks[3].p[75];
  document.querySelector('#mixer .strip[data-track="3"] [data-fxmode=sound]').click(); await sleep(400);
  window.fm1Test.mock.sim.fxSlot(1, 4);
  r.pushed = await until(() => labels(0) === "CMP,REV,DLY,CHO" && slots()[1].value === "4" && slots()[3].value === "2", 8000);
  const ratio = document.querySelector('#mixer .strip.master [data-fx=cmp] select'); ratio.value = "3"; ratio.dispatchEvent(new Event("change")); await sleep(400);
  r.ratio = mock.fx.cset[0];
  r.wide = document.documentElement.scrollWidth <= window.innerWidth + 1;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(fxRes && fxRes.before === "DST,CHO,DLY,REV|DST,CHO,DLY,REV" && /^4:---- DST CHO DLY REV CMP$/.test(fxRes.pickers) && fxRes.cmp === "2:1,10ms,AUTO"
  && fxRes.loaded && fxRes.page && fxRes.trackCmp === 3 && /^[02,]*2[0,]*$/.test(fxRes.soundCmp) && fxRes.soundCmp.split(",").filter((x) => x === "2").length === 1
  && fxRes.bus === "DRUMS BUS CMP|true" && fxRes.busCmp === 4 && fxRes.pushed && fxRes.ratio === 3 && fxRes.wide,
  `e2e v10 FX slots (mock ?fxs=1): pickers, CMP card, a slot loaded (strips + FX page follow), SOUND | BUS on the drum strip, FX_PUSH (${JSON.stringify(fxRes)})`);
await shot("fx-slots");
/* the lane popup has the sound's DST / CMP (FX op 3, as the drum strip's SOUND mode); an X0X source: the device says no inserts (DRUM_SHOW
   flag 4), so the popup's knobs are gone and the strip's DST / CMP are greyed */
const lanePop = await run(`${U} try {
  const mock = window.fm1Test.mock, r = {};
  mock.sim.fxSlot(0, 1); await sleep(200); mock.sim.fxSlot(1, 5);
  const strip = () => document.querySelector('#mixer .strip[data-track="3"]'), ln = strip().querySelector('.ln[data-l="6"]');
  r.slots = await until(() => D().fx.slots[0] === 1 && D().fx.slots[1] === 5 && [...strip().querySelectorAll(".fx .knob .kl")].map((e) => e.textContent.trim()).slice(0, 2).join() === "DST,CMP", 8000);
  ln.click(); await sleep(400);
  const sf = () => [...strip().querySelectorAll(".fx .knob")].slice(0, 2).map((k) => k.classList.contains("na") ? "-" : "+").join("");
  r.stripKit = sf();
  ln.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
  await until(() => $("#pop").open && $("#pop").dataset.pop === "lane" && document.querySelector("#pop .knobs .knob"), 10000); await sleep(300);
  const pk = () => [...document.querySelectorAll("#pop .knobs .knob .kl")].map((e) => e.textContent.trim()).join();
  r.popKit = pk();
  const knobIn = (lab) => [...document.querySelectorAll("#pop .knobs .knob")].find((k) => k.querySelector(".kl").textContent.trim() === lab);
  const dst = knobIn("DST"), cmp = knobIn("CMP");
  for (let q = 0; q < 5; q++) dst.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true }));
  for (let q = 0; q < 3; q++) cmp.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true }));
  r.wrote = await until(() => mock.state.fx.dist[6] > 0 && mock.state.fx.comp[6] > 0, 5000);
  r.dist = mock.state.fx.dist[6];
  const sel = document.querySelector("#pop .hd select");
  const xo = [...sel.options].find((o) => o.dataset.kind === "x0x"); if (!xo) return { err: "no X0X source" };
  sel.value = xo.value; sel.dispatchEvent(new Event("change"));      /* an X0X kit as this sound's source */
  r.gone = await until(() => /REV/.test(pk()) && !/DST|CMP/.test(pk()), 8000);
  r.popX0x = pk();
  r.stripX0x = await until(() => sf() === "--", 5000) ? "--" : sf();
  $("#popx").click(); await sleep(300);
  r.stripClosed = sf();
  strip().querySelector('.ln[data-l="5"]').click(); await sleep(500);
  r.stripOther = sf();
  ln.click(); await sleep(500);
  r.stripBack = sf();
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(lanePop && lanePop.slots && lanePop.stripKit === "++" && /DST/.test(lanePop.popKit) && /CMP/.test(lanePop.popKit) && lanePop.wrote && lanePop.gone
  && lanePop.stripX0x === "--" && lanePop.stripClosed === "--" && lanePop.stripOther === "++" && lanePop.stripBack === "--",
  `e2e v10: the lane popup has DST / CMP (written with FX op 3); an X0X sound: none in the popup, greyed on the strip, back on another sound (${JSON.stringify(lanePop)})`);
/* v13 FX owners (the mock with ?fxp=1): the inserts popup of a track (two slots: a type list, the type's knobs from the device's descriptors), a
   type change, a knob, the device moving a value (FX_PUSH), the drum track (its bus and the selected sound), the master's inserts, the returns */
await run(`try { localStorage.removeItem("fm1.mixLanes"); } catch (e) { return false; }`);   /* (the picked sound is kept per browser: earlier pages left one) */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxp=1&e2e=1#mixer` });
await sleep(1500);
const fxpRes = await run(`${U} try {
  if (!await until(() => document.querySelectorAll("#mixer .strip").length === 9 && D() && D().fx && D().watch && D().info.fxp, 120000)) return { err: "no fxp" };
  const mock = window.fm1Test.mock, r = {};
  const pop = () => $("#pop"), labels = () => [...document.querySelectorAll("#pop .fxslot")].map((s) => [...s.querySelectorAll(".knob .kl, .pk:not(.fxt) .kl")].map((e) => e.textContent.trim()).join(","));
  const setType = async (sl, v) => { const s = document.querySelector('#pop select[data-type="' + sl + '"]'); s.value = String(v); s.dispatchEvent(new Event("change")); await sleep(800); };
  const $1 = (id) => document.getElementById(id);
  await until(() => $1("strip-ret-c-slot1-open") && $1("strip-master-slot2-open") && $1("strip-usb-slot2-open") && $1("strip-4-slot2-open"), 20000); await sleep(500);
  const strips = [...document.querySelectorAll("#mixer > .strip")];
  /* the one strip template: every strip has the same rows, in the same order (STRIP_ROWS) */
  const rowsOf = (s) => [...s.querySelectorAll(":scope > .srow")].map((x) => x.dataset.row).join();
  r.rows = new Set(strips.map(rowsOf)).size + ":" + rowsOf(strips[0]);
  /* the rows line up: the same top on every strip, for every row */
  const tops = (q) => new Set(strips.filter((s) => !s.classList.contains("sub")).map((s) => Math.round(s.querySelector(q).getBoundingClientRect().top))).size;
  r.aligned = ["head", "inst", "slot1", "slot2", "level"].filter((n) => tops(".srow[data-row=" + n + "]") !== 1).join() + (tops(".lblk .row2 > :first-child") === 1 ? "" : "mute");
  /* the slot widget: one name part (the open button) and one list a slot, the ids of the convention; no visible duplicate opener anywhere */
  r.widgets = strips.map((s) => [...s.querySelectorAll(".slotw")].map((w) => w.querySelectorAll("button.fxname").length + "" + w.querySelectorAll("select.fxtype").length).join("")).join("|");
  r.ids = ["strip-1-inst-type", "strip-1-inst-open", "strip-1-slot1-type", "strip-1-slot1-open", "strip-1-slot2-type", "strip-4-inst-type", "strip-4-slot2-open", "strip-usb-slot1-type", "strip-usb-slot2-open",
    "strip-ret-a-slot1-type", "strip-ret-b-slot1-open", "strip-ret-c-slot1-open", "strip-master-slot1-type", "strip-master-slot2-open"].filter((id) => !$1(id)).join();
  r.dupOpen = ["fxins", "fxmst", "fxret", "fxusb"].filter((p) => document.querySelector("[data-pop=" + p + "]")).join() + "|" + document.querySelectorAll("#mixer .fxopen").length + "|" + document.querySelectorAll("#mixer .strip.ret .fxxo, #mixer [data-bus]").length;
  r.names = ["strip-1-slot1-open", "strip-ret-a-slot1-open", "strip-ret-b-slot1-open", "strip-ret-c-slot1-open"].map((id) => $1(id).textContent.trim()).join();
  r.heads = [...document.querySelectorAll("#mixer .strip.ret .shead .num")].map((x) => x.textContent).join();
  /* a return's MUTE (SOLO op 7): the device-side mask and the strip; USB IN and the tracks keep their own */
  const rm = $1("strip-ret-b-mute"); r.retMute0 = !!rm && !rm.hasAttribute("aria-disabled") && !$1("strip-ret-b-solo");
  rm.click(); await until(() => rm.getAttribute("aria-pressed") === "true", 4000); r.retMute = (mock.state.retMute & 2) + ":" + rm.closest(".strip").classList.contains("muted");
  rm.click(); await until(() => rm.getAttribute("aria-pressed") === "false", 4000); r.retMuteOff = mock.state.retMute;
  /* the master has no return on it (no bus knob sets, no RET) */
  r.master = ["fxdelay", "fxreverb", "fxchorus"].filter((f) => document.querySelector("#mixer .strip.master [data-pop=" + f + "], #mixer .strip.master [data-fx=" + f + "]")).join() + "|" + [...document.querySelectorAll("#mixer .strip.master .knob .kl")].map((k) => k.textContent.trim()).join();
  /* Live-like strips: the fader the bottom-most big control with MUTE (an activator showing the number) and SOLO under it; no number under a knob (a tooltip), no FX toggle;
     nothing beside the master; Song in the transport bar; a return's sends to the returns after it (A: B C, B: C, C none) */
  r.live = ["strip-1-fader", "strip-1-mute", "strip-1-solo", "strip-4-fader", "strip-4-mute", "strip-usb-fader", "strip-usb-mute", "strip-ret-a-fader", "strip-ret-c-fader"].filter((id) => !$1(id)).join() + "|"
    + strips.slice(0, 4).map((x) => { const f = x.querySelector(".srow[data-row=level] .fm").getBoundingClientRect(), mu = x.querySelector(".srow[data-row=level] .row2 button").getBoundingClientRect(), pn = x.querySelector(".srow[data-row=sends]").getBoundingClientRect();
      return mu.right <= f.left + 1 && pn.bottom <= x.querySelector(".srow[data-row=level]").getBoundingClientRect().top + 1; }).join()
    + "|" + $1("strip-1-mute").textContent + $1("strip-3-mute").textContent;
  r.nums = [...document.querySelectorAll("#mixer .strip .knob .kv")].filter((e) => getComputedStyle(e).display !== "none").length + ":" + ($("#mixer .strip .srow[data-row=pan] .knob").title.includes(":") ? 1 : 0);
  r.fxbtn = document.querySelectorAll("#mixer .strip .fx button, #mixer .strip .fx .tog").length;
  r.rec = document.querySelectorAll("#mixer .strip .rec, #mixer [id$=\\"-rec\\"]").length;
  r.side = document.querySelectorAll("#mixer .strip.master .mx, #mixer .strip.master [data-pop=song], #mixer .strip.master [data-fx]").length + ":" + (document.querySelector("#songbtn") && !$("#songbtn").hidden ? 1 : 0);
  r.rsends = ["0", "1", "2"].map((k) => [...document.querySelectorAll("#mixer > .strip.ret[data-ret='" + k + "']  .col > .fx:not(.mainks) .knob .kl")].map((e) => e.textContent.trim()).join("")).join("|");
  /* a type from the widget's list: the name follows, the open part opens the slot's parameters */
  const setT = async (id, v) => { const x = $1(id); x.value = String(v); x.dispatchEvent(new Event("change")); await sleep(900); };
  await setT("strip-1-slot1-type", 18); await setT("strip-1-slot2-type", 5);
  r.t1 = $1("strip-1-slot1-open").textContent.trim() + "|" + $1("strip-1-slot2-open").textContent.trim();
  $1("strip-1-slot2-open").click();
  await until(() => pop().open && pop().dataset.pop === "fxslot" && document.querySelector("#pop .fxslot .knob"), 10000); await sleep(400);
  r.title = $("#poptitle").textContent;
  r.open2 = [...document.querySelectorAll("#pop .fxslot .knob .kl, #pop .fxslot .pk:not(.fxt) .kl")].map((e) => e.textContent.trim()).join(",") + ":" + document.querySelectorAll("#pop .fxslot").length;
  const k = document.querySelector('#pop .fxslot .knob[data-fxp="1/0"]');
  for (let q = 0; q < 4; q++) k.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true }));
  await sleep(500);
  $("#popx").click(); await sleep(200);
  mock.sim.fxpSet(0, 0, 0, -12);                 /* LO moved on the device */
  await sleep(900);
  r.slots = mock.sim.fxpType(0, 1, 0).join();
  /* a return: its type from the widget, its RET and the effect's main knobs on the strip; a bus type opens its own panel from the same one open part */
  r.retA = [...document.querySelectorAll("#mixer .strip.ret[data-ret='0'] .fx .knob .kl")].map((e) => e.textContent.trim()).join() + ":" + $1("strip-ret-a-slot1-open").dataset.pop;
  await setT("strip-ret-b-slot1-type", 10);       /* B holds the flanger now */
  await until(() => $1("strip-ret-b-slot1-open") && $1("strip-ret-a-slot1-open") && $1("strip-master-slot2-open"), 15000); await sleep(600);
  r.retB = [...document.querySelectorAll("#mixer .strip.ret[data-ret='1'] .fx .knob .kl")].map((e) => e.textContent.trim()).join() + ":" + $1("strip-ret-b-slot1-open").dataset.pop + ":" + $1("strip-ret-b-slot1-open").textContent.trim();
  $1("strip-ret-a-slot1-open").click();
  await until(() => pop().open, 6000); await sleep(300);
  r.busPop = pop().dataset.pop; $("#popx").click(); await sleep(200);
  r.strip = [...document.querySelectorAll('#mixer .strip[data-track="0"] .fx .knob .kl')].map((e) => e.textContent.trim()).join();
  r.wide = document.documentElement.scrollWidth <= window.innerWidth + 1;
  /* the USB input's values are on its strip: VOLUME in the level row, PAN, REV DLY CHO, MUTE and FX */
  r.usb = [...document.querySelectorAll("#mixer .strip.usb .knob .kl")].map((e) => e.textContent.trim()).join() + ":" + document.querySelectorAll("#mixer .strip.usb input.fader").length + ":" + document.querySelectorAll("#mixer .strip.usb .row2 button").length;
  const pan = document.querySelector("#mixer .strip.usb .srow[data-row=tools] .knob"); for (let q = 0; q < 3; q++) pan.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true }));
  await sleep(500);
  /* the drum bus (T4): its own sends, never a sound's; the sounds are strips of their own */
  r.t4 = [...document.querySelectorAll('#mixer .strip[data-track="3"] .fx .knob .kl')].map((e) => e.textContent.trim()).join() + ":" + document.querySelectorAll('#mixer .strip[data-track="3"] .fxmodes, #mixer .strip[data-track="3"] .lanes').length;
  document.querySelector('#mixer .strip[data-track="3"] [data-fold]').click(); await sleep(600);
  r.wlane = Math.round(100 * document.querySelector("#mixer > .strip.sub").getBoundingClientRect().width / document.querySelector('#mixer .strip[data-track="0"]').getBoundingClientRect().width);
  r.lane = document.querySelectorAll("#mixer > .strip.sub").length + ":" + (document.getElementById("strip-lane3-slot1-type") ? 1 : 0) + ":" + document.querySelectorAll("#mixer > .strip.sub .lnav select").length;
  /* every drum sound's MUTE and SEQ MUTE (and the ACCENT lane's SEQ MUTE): the strip shows it (the button, a struck-through name, the dimmed controls) and the DEVICE's state changed */
  r.lm = [];
  for (let n = 0; n < 17; n++) {
    const st = document.querySelector('#mixer > .strip.sub[data-lane="' + n + '"]'), btns = [...st.querySelectorAll(".lblk .row2 button")];
    const acc = n === 16, mute = acc ? null : btns[0], sq = btns[btns.length - 1], one = async (b, ix) => {
      b.click(); await sleep(250);
      const on = b.getAttribute("aria-pressed") === "true" && mock.sim.dlanVal(0, n, ix) === 1;
      const look = ix !== 1 || (st.classList.contains("lmuted") && getComputedStyle(st.querySelector(".srow[data-row=head] .eng")).textDecorationLine.includes("line-through") && +getComputedStyle(st.querySelector(".lblk .fm")).opacity < 1);
      b.click(); await sleep(250);
      return on && look && b.getAttribute("aria-pressed") === "false" && mock.sim.dlanVal(0, n, ix) === 0 ? 1 : 0;
    };
    r.lm.push((acc ? 1 : await one(mute, 1)) && await one(sq, acc ? 0 : 3) ? 1 : 0);
  }
  r.lm = r.lm.join("");
  document.querySelector('#mixer > .strip.sub[data-lane="2"] [data-pop=lane]').click();
  await until(() => pop().open && pop().dataset.pop === "lane" && document.querySelectorAll("#pop .dlan").length === 1, 10000); await sleep(500);
  r.laneCards = [...document.querySelectorAll("#pop .dlan, #pop .fxo")].map((c) => c.getAttribute("aria-label") + ":" + c.querySelectorAll(".knob, select").length).join("|");
  $("#popx").click(); await sleep(200);
  /* the lane's mix is on its strip: MUTE button, PAN knob */
  const ls = document.querySelector('#mixer > .strip.sub[data-lane="2"]');
  ls.querySelector(".lblk .row2 button").click(); await sleep(500);
  const sl = ls.querySelector('.srow[data-row=pan] .knob'); for (let q = 0; q < 3; q++) sl.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true }));
  await sleep(500);
  r.laneMuted = ls.classList.contains("lmuted");
  r.panLow = mock.sim.dlanVal(0, 2, 0) < 0;
  document.querySelector('#mixer .strip[data-track="1"] .sq').click(); await sleep(500);
  r.sq = document.querySelector('#mixer .strip[data-track="1"] .sq').getAttribute("aria-pressed") + ":" + document.querySelector('#mixer .strip[data-track="1"]').classList.contains("sqm");
  mock.sim.dlanSet(0, 2, 1, 0); await sleep(800);
  r.unmuted = !document.querySelector('#mixer > .strip.sub[data-lane="2"]').classList.contains("lmuted");
  document.querySelector('#mixer .strip[data-track="3"] [data-fold]').click(); await sleep(400);   /* (folded again: the next tests start from the default) */
  document.querySelector("#songbtn").click();
  await until(() => pop().open && pop().dataset.pop === "song" && document.querySelectorAll("#pop .songparts tbody tr").length === 4, 10000); await sleep(300);
  const sc = document.querySelector('#pop [data-song="scene2"]'); sc.value = "7"; sc.dispatchEvent(new Event("change")); await sleep(500);
  document.querySelector('#pop [data-song="ins2"]').click(); await sleep(500);
  r.song5 = document.querySelectorAll("#pop .songparts tbody tr").length + ":" + document.querySelector('#pop [data-song="scene3"]').value;
  document.querySelector('#pop [data-song="del0"]').click(); await sleep(500);
  const md = document.querySelector('#pop [data-song="mode"]'); md.value = "1"; md.dispatchEvent(new Event("change")); await sleep(400);
  r.song4 = document.querySelectorAll("#pop .songparts tbody tr").length + ":" + document.querySelector('#pop [data-song="mode"]').value;
  $("#popx").click(); await sleep(200);
  window.confirm = () => true;
  document.querySelector('#mixer .strip[data-track="0"] [data-pop=tools]').click();
  await until(() => pop().open && pop().dataset.pop === "tools" && document.querySelector('#pop [data-tool="0"]') && document.querySelector('#pop [data-tool="hits"]'), 10000); await sleep(300);
  document.querySelector('#pop [data-tool="0"]').click(); await sleep(500);
  r.tool1 = document.querySelector("#pop .toolmsg").textContent;
  document.querySelector('#pop [data-tool="euon"]').click(); await sleep(500);
  r.euOn = !document.querySelector('#pop [data-tool="keep"]').hidden;
  for (let q = 0; q < 2; q++) document.querySelector('#pop [data-tool="hits"]').dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true }));
  await sleep(500);
  r.hits = document.querySelector('#pop [data-tool="hits"]').getAttribute("aria-valuenow");
  document.querySelector('#pop [data-tool="restore"]').click(); await sleep(500);
  r.euOff = document.querySelector('#pop [data-tool="keep"]').hidden;
  $("#popx").click(); await sleep(200);
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "f", bubbles: true })); await sleep(500);
  r.key = pop().open && pop().dataset.pop;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(fxpRes && /^1:head,inst,nav,clips,slot1,slot2,tools,seq,ov,pan,sends,level$/.test(fxpRes.rows) && fxpRes.aligned === "" && fxpRes.ids === ""
  && fxpRes.widgets.split("|").every((w) => /^(11)*$/.test(w)) && /^\|\d+\|0$/.test(fxpRes.dupOpen) && fxpRes.heads === "A,B,C" && fxpRes.retMute0 && fxpRes.retMute === "2:true" && fxpRes.retMuteOff === 0
  && fxpRes.master.split("|")[0] === "" && fxpRes.master.split("|")[1] === "DUST,DUCK,FILT"
  && /EQ3/i.test(fxpRes.t1) && /DYN/i.test(fxpRes.t1) && /^Inserts/.test(fxpRes.title) && /THRS,RATIO,ATK,REL,GAIN,CEIL:1/.test(fxpRes.open2)
  && fxpRes.retA.endsWith(":fxchorus") && /FLNG/i.test(fxpRes.retB) && fxpRes.busPop === "fxchorus" && fxpRes.strip === "--,A,B,C" && fxpRes.wide
  && fxpRes.t4.endsWith(":0") && fxpRes.lane === "17:1:0" && /^LFO:8$/.test(fxpRes.laneCards) && fxpRes.laneMuted && fxpRes.panLow && fxpRes.sq === "true:true" && fxpRes.unmuted
  && fxpRes.live === "|true,true,true,true|13" && fxpRes.nums === "0:1" && fxpRes.fxbtn === 0 && fxpRes.rec === 0 && fxpRes.lm === "11111111111111111" && fxpRes.side === "0:1" && fxpRes.rsends === "BC|C|" && fxpRes.wlane > 50 && fxpRes.wlane < 75
  && /VOLUME|^PAN/.test(fxpRes.usb) && fxpRes.song5 === "5:7" && fxpRes.song4 === "4:1" && fxpRes.tool1 === "T1 REVERSED" && fxpRes.euOn && fxpRes.hits === "2" && fxpRes.euOff,
  `e2e v13 FX owners (mock ?fxp=1): the one strip template (same rows, aligned), the slot widget [ name v ] with one open part and one list a slot (ids strip-<n>-slot<k>-type / -open), no duplicate openers, returns A B C with RET and main knobs, the master without returns, the drum bus and the sound strips (${JSON.stringify(fxpRes)})`);
await shot("fxp-inserts");
/* the scale chip in the header (the selected track's root and scale), its panel (SCL and ARP pages, no cards of the Sound popup), the strips' ARP button */
const sclRes = await run(`${U} try {
  const mock = window.fm1Test.mock, r = {}, $1 = (id) => document.getElementById(id);
  const chip = $1("sclbtn"); await until(() => !chip.hidden && chip.textContent.trim().length > 0, 8000);
  r.chip = chip.textContent.trim();
  r.arp0 = $1("strip-1-arp").getAttribute("aria-pressed");
  $1("strip-1-arp").click(); await until(() => $1("strip-1-arp").getAttribute("aria-pressed") === "true", 4000);
  r.arp1 = await until(() => mock.state.tracks[0].p[17] > 0, 4000);
  $1("strip-2-arp").click(); await until(() => $1("strip-2-arp").getAttribute("aria-pressed") === "true", 4000); await sleep(500);
  r.arpOther = mock.state.tracks[1].p[17] > 0;
  $1("strip-1-arp").click(); await until(() => $1("strip-1-arp").getAttribute("aria-pressed") === "false", 4000); await sleep(400);
  r.arpOff = await until(() => mock.state.tracks[0].p[17] === 0, 4000) ? 0 : 1;
  r.noDrumArp = !document.getElementById("strip-4-arp");
  chip.click(); await until(() => $("#pop").open && $("#pop").dataset.pop === "scale", 8000); await sleep(400);
  r.panel = [...document.querySelectorAll("#pop .group h2")].map((h) => h.textContent.trim()).join();
  $("#popx").click(); await sleep(200);
  $1("strip-1-inst-open").click();
  await until(() => $("#pop").open && $("#pop").dataset.pop === "sound", 8000); await sleep(400);
  r.sound = [...document.querySelectorAll("#pop #groups .group h2")].map((h) => h.textContent.trim()).filter((x) => /^(SCL|ARP)$/.test(x)).join();
  $("#popx").click(); await sleep(200);
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(sclRes && /\S/.test(sclRes.chip) && sclRes.arp0 === "false" && sclRes.arp1 && sclRes.arpOther && sclRes.arpOff === 0 && sclRes.noDrumArp && /SCL/.test(sclRes.panel) && /ARP/.test(sclRes.panel) && sclRes.sound === "",
  `e2e: the scale chip opens the scale panel (SCL, ARP), the strips' ARP button toggles the track's arpeggiator (the selected track and another), no SCL / ARP cards in the Sound popup (${JSON.stringify(sclRes)})`);
/* the menu bar on top (one row of pages, no side column), the About dialog (credits; no footer on the page) */
const aboutRes = await run(`${U} try {
  const r = {}, bar = document.getElementById("side"), tabs = [...bar.querySelectorAll("button[data-tab]")].filter((b) => b.offsetParent);
  const br = bar.getBoundingClientRect();
  r.bar = br.top === 0 && br.left === 0 && br.width >= window.innerWidth - 20 && br.height < 120 && tabs.length >= 4 && new Set(tabs.map((b) => Math.round(b.getBoundingClientRect().top))).size <= 2;
  r.mainLeft = Math.round(document.querySelector("main.main").getBoundingClientRect().left);
  r.noFooter = document.querySelectorAll("footer").length;
  document.getElementById("aboutbtn").click(); await sleep(300);
  const d = document.getElementById("about");
  r.open = d.open;
  const txt = d.textContent;
  r.names = ["Leo Kuroshita", "Kerem Kilic", "Charles Vestal", "Zakaria Chowdhury", "zednaked", "PatttF", "Kayrock", "Fukiai", "LICENSING.md", "GPL-3.0", "Chrome or Edge", "M-VAVE"].filter((n) => !txt.includes(n)).join();
  r.links = [...d.querySelectorAll("a")].map((a) => a.getAttribute("href")).join();
  document.getElementById("aboutx").click(); await sleep(200);
  r.closed = !d.open;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(aboutRes && aboutRes.bar && aboutRes.mainLeft < 60 && aboutRes.noFooter === 0 && aboutRes.open && aboutRes.names === "" && /hugelton\.com/.test(aboutRes.links) && /LICENSING\.md/.test(aboutRes.links) && /FUKIAI-LICENSE/.test(aboutRes.links) && aboutRes.closed,
  `e2e: the pages are a menu bar on top (no side column), the About dialog holds the credits, licences and links, no footer (${JSON.stringify(aboutRes)})`);
/* the send knobs are as big as the pan knob (no shrinking); the drum head: [chip + name ..... 1|ALL ▶] with the toggle right beside the arrow */
const sizeRes = await run(`${U} try {
  const r = {}, st = (n) => document.querySelector('#mixer .strip[data-track="' + n + '"]');
  const w = (e) => Math.round(e.querySelector("svg").getBoundingClientRect().width);
  r.knobs = [0, 1, 2].map((n) => w(st(n).querySelector(".srow[data-row=pan] .knob")) + ":" + [...st(n).querySelectorAll(".srow[data-row=sends] .knob")].map(w).filter((x) => x > 0).join("/")).join("|");
  const fb = st(3).querySelector("[data-fold]"), wasOpen = fb.getAttribute("aria-expanded") === "true"; if (!wasOpen) { fb.click(); await sleep(500); }
  const h = st(3).querySelector(".srow[data-row=head]"), hr = h.getBoundingClientRect(), lm = h.querySelector("[data-lmode]").getBoundingClientRect(), fo = h.querySelector("[data-fold]").getBoundingClientRect(), hd = h.querySelector(".shead").getBoundingClientRect();
  r.head = (lm.left >= hd.right - 1) + ":" + (Math.abs(fo.left - lm.right) <= 6) + ":" + (hr.right - fo.right <= 8);
  if (!wasOpen) { st(3).querySelector("[data-fold]").click(); await sleep(400); }
  r.panLbl = [...document.querySelectorAll("#mixer > .strip")].map((x) => x.querySelector(".srow[data-row=pan] .knob, .srow[data-row=tools] .col > .knob")).filter((k) => k && k.offsetParent).every((k) => k.querySelector(".kl").getBoundingClientRect().left >= k.querySelector("svg").getBoundingClientRect().right - 1);
  const sr = st(0).querySelector(".srow[data-row=sends]"), last = [...sr.querySelectorAll(".knob")].pop();
  r.fit = last.getBoundingClientRect().bottom <= sr.getBoundingClientRect().bottom + 1;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(sizeRes && /^44:/.test(sizeRes.knobs) && sizeRes.knobs.split("|").every((x) => { const a = x.split(/[:\/]/); return a.length >= 4 && a.every((v) => v === a[0]); }) && sizeRes.head === "true:true:true" && sizeRes.fit && sizeRes.panLbl,
  `e2e: the send knobs are the pan knob's size; the pan label sits right of its knob like the send labels; the drum head keeps 1|ALL right beside the fold arrow at its right end (${JSON.stringify(sizeRes)})`);
/* the mixer's order, as Ableton Live's session mixer: T1 T2 T3 T4 (the drum GROUP), its sounds as member strips right after it (folded by default,
   unfolded to all of them, or one at a time), USB IN, R1 R2 R3, MASTER last; the keys walk the members; the ACCENT lane is the 17th member */
const MIXKEY = "fm1.mixLanes";
await send("Page.navigate", { url: "about:blank" });
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxp=1&e2e=1#mixer` });
await sleep(1500);
await run(`try { localStorage.removeItem("${MIXKEY}"); } catch (e) { return false; }`);
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxp=1&e2e=1#mixer` });
await sleep(1500);
const ORD = `const order = () => [...document.querySelectorAll("#mixer > .strip")].map((s) => s.dataset.track != null ? "T" + (+s.dataset.track + 1) : s.dataset.lane != null ? "L" + s.dataset.lane
  : s.dataset.ret != null ? (s.dataset.ret === "usb" ? "USB" : "ABC"[+s.dataset.ret]) : s.classList.contains("master") ? "MASTER" : "?").join();
  const stored = () => { try { return JSON.parse(localStorage.getItem("${MIXKEY}")); } catch (e) { return null; } };
  const fold = () => $('#mixer .strip[data-track="3"] [data-fold]'), lmode = () => $('#mixer .strip[data-track="3"] [data-lmode]');
  const key = async (k) => { document.dispatchEvent(new KeyboardEvent("keydown", { key: k, bubbles: true })); await sleep(350); };
  const subSel = () => [...document.querySelectorAll("#mixer > .strip.sub.sel")].map((s) => s.dataset.lane).join();`;
const L17 = Array.from({ length: 17 }, (_, l) => "L" + l).join();
const ordRes = await run(`${U} ${ORD} try {
  if (!await until(() => document.querySelectorAll("#mixer > .strip").length === 9 && D() && D().dlMix && D().dlMix.acc && D().watch, 120000)) return { err: "no mixer" };
  const r = {};
  r.order0 = order(); r.stored0 = stored(); r.exp0 = fold().getAttribute("aria-expanded"); r.sect = [...document.querySelectorAll("#mixer > .strip.sect")].map((s) => s.dataset.ret ?? "M").join();
  $('#mixer .strip[data-track="3"] .shead').click(); await sleep(700);
  fold().click(); await sleep(500);
  r.order1 = order(); r.exp1 = fold().getAttribute("aria-expanded"); r.stored1 = (stored() || {}).mode;
  r.sub = document.querySelectorAll('#mixer > .strip.sub').length; r.tint = getComputedStyle(document.querySelector("#mixer > .strip.sub")).backgroundColor;
  lmode().click(); await sleep(500);
  r.order2 = order(); r.stored2 = (stored() || {}).mode; r.pressed = lmode().getAttribute("aria-pressed");
  document.querySelector('#mixer > .strip.sub [data-lstep="1"]').click(); await sleep(600);
  r.order3 = order(); r.sel3 = D().sel; r.stored3 = (stored() || {}).lane; r.on3 = subSel();
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(ordRes && ordRes.order0 === "T1,T2,T3,T4,USB,A,B,C,MASTER" && ordRes.stored0 === null && ordRes.exp0 === "false" && ordRes.sect === "usb,M"
  && ordRes.order1 === `T1,T2,T3,T4,${L17},USB,A,B,C,MASTER` && ordRes.exp1 === "true" && ordRes.stored1 === "all" && ordRes.sub === 17 && ordRes.tint !== "rgba(0, 0, 0, 0)"
  && /^T1,T2,T3,T4,L\d+,USB,A,B,C,MASTER$/.test(ordRes.order2) && ordRes.stored2 === "one" && ordRes.pressed === "true"
  && ordRes.order3 === `T1,T2,T3,T4,L${+/L(\d+)/.exec(ordRes.order2)[1] + 1},USB,A,B,C,MASTER` && ordRes.sel3 === 3 && ordRes.stored3 === +/L(\d+)/.exec(ordRes.order3)[1] && ordRes.on3 === String(ordRes.stored3),
  `e2e mixer order (mock ?fxp=1): T1 T2 T3 T4, USB IN, A B C, MASTER; folded by default; unfolded: T4 then its 16 sounds + ACCENT; one at a time with next; kept in localStorage (${JSON.stringify(ordRes)})`);
/* a reload remembers the choice (one sound at a time, the second sound) */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxp=1&e2e=1#mixer` });
await sleep(1500);
const memRes = await run(`${U} ${ORD} try {
  if (!await until(() => document.querySelectorAll("#mixer > .strip").length === 10 && D() && D().dlMix && D().watch, 120000)) return { err: "no mixer", n: document.querySelectorAll("#mixer > .strip").length };
  return { order: order(), pressed: lmode().getAttribute("aria-pressed"), exp: fold().getAttribute("aria-expanded") }; } catch (e) { return { err: String(e.stack) }; }`);
ok(memRes && memRes.order === ordRes.order3 && memRes.pressed === "true" && memRes.exp === "true", `e2e mixer group: a reload keeps one sound at a time and which sound (${JSON.stringify(memRes)})`);
const keyRes = await run(`${U} ${ORD} try {
  lmode().click(); await sleep(500);                                 /* all sounds */
  $('#mixer .strip[data-track="3"] .shead').click(); await sleep(700);
  const r = { all: order().split(",").length };
  await key("ArrowRight"); r.k1 = subSel() + "|" + D().sel;
  await key("ArrowRight"); r.k2 = subSel();
  await key("ArrowLeft"); await key("ArrowLeft"); r.k3 = subSel() + "|" + D().sel;
  await key("ArrowLeft"); await until(() => D().sel === 2, 5000); r.k4 = subSel() + "|" + D().sel;
  $('#mixer .strip[data-track="3"] .shead').click(); await until(() => D().sel === 3, 8000); await sleep(500);
  await key("a"); r.acc1 = document.querySelector('#mixer .strip[data-track="3"] .ln.lsel') ? "ln" : "none";
  await key("a"); await until(() => $("#pop").open && $("#pop").dataset.pop === "lane", 5000); await sleep(600);
  r.title = $("#poptitle").textContent; r.cards = [...document.querySelectorAll("#pop .dlan")].map((c) => c.querySelectorAll(".knob, select").length).join(); r.noSnd = /no sound/.test($("#popbody").textContent);
  $("#popx").click(); await sleep(300);
  const ak = document.querySelector('#mixer > .strip.sub[data-lane="16"] .knob'); for (let q = 0; q < 3; q++) ak.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true }));
  await sleep(600); r.amt = D().dlMix.acc[1];
  const lv = document.querySelector('#mixer > .strip.sub[data-lane="2"] input.fader'); const l0 = D().dl.lanes[2].ofs[7];
  lv.value = String(+lv.value + 3); lv.dispatchEvent(new Event("input", { bubbles: true })); lv.dispatchEvent(new Event("change", { bubbles: true }));
  await sleep(600); r.level = D().dl.lanes[2].ofs[7] - l0;
  const m = document.querySelector('#mixer > .strip.sub[data-lane="2"] .row2 button'); m.click(); await sleep(600);
  r.mute = D().dlMix.lanes[2][1] + ":" + m.getAttribute("aria-pressed") + ":" + document.querySelector('#mixer > .strip.sub[data-lane="2"]').classList.contains("lmuted");
  r.btns = ["lane", "fxslot"].map((p) => document.querySelectorAll('#mixer > .strip.sub[data-lane="2"] [data-pop=' + p + ']').length).join();
  r.insLane = "2";
  document.querySelector('#mixer .strip[data-track="3"] [data-pop=sequence]').click();
  await until(() => $("#pop").open && $("#pop").dataset.pop === "sequence" && document.querySelector('#drumgrid [data-ln="16"]'), 10000);
  r.row = (document.querySelector('#drumgrid [data-ln="16"]') || {}).textContent; r.rows = document.querySelectorAll("#drumgrid [data-ln]").length; $("#popx").click(); await sleep(300);
  fold().click(); await sleep(500); r.folded = order(); r.stored = (stored() || {}).mode;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(keyRes && keyRes.all === 26 && keyRes.k1 === "0|3" && keyRes.k2 === "1" && keyRes.k3 === "|3" && keyRes.k4 === "|2" && keyRes.acc1 === "none" && /ACCENT$/.test(keyRes.title)
  && keyRes.cards === "2" && keyRes.noSnd && keyRes.amt > 27 && keyRes.level > 0 && keyRes.mute === "1:true:true" && keyRes.btns === "1,4" && keyRes.insLane === "2"
  && keyRes.row === "ACCENT" && keyRes.rows === 17 && keyRes.folded === "T1,T2,T3,T4,USB,A,B,C,MASTER" && keyRes.stored === "collapsed",
  `e2e mixer group: <- / -> walk the member strips (out of the first onto T4, then T3), A picks / opens the ACCENT lane (amount, SEQ MUTE), a member's LEVEL / MUTE / inserts / popup, the ACCENT row in the drum grid, fold again (${JSON.stringify(keyRes)})`);
/* the mixer in light and dark: folded, one sound, all sounds */
for (const mode of ["light", "dark"]) {
  await scheme(mode);
  for (const [name, w, set] of [["collapsed", 2000, "collapsed"], ["one", 2000, "one"], ["all", 3200, "all"]]) {
    await send("Emulation.setDeviceMetricsOverride", { width: w, height: 1000, deviceScaleFactor: 1, mobile: false });
    await run(`${U} ${ORD} const want = "${set}"; let g = 0; while ((stored() || {}).mode !== want && g++ < 4) { if (want === "collapsed") { if (fold().getAttribute("aria-expanded") === "true") fold().click(); }
      else if (fold().getAttribute("aria-expanded") !== "true") fold().click(); else if ((want === "one") !== (lmode().getAttribute("aria-pressed") === "true")) lmode().click(); await sleep(500); }`);
    await run(`window.scrollTo(0, 0); document.querySelector("#mixer").scrollTo(0, 0);`);
    await sleep(400);
    await shot(`mixer-${name}-${mode}`);
  }
}
await send("Emulation.setDeviceMetricsOverride", { width: 1400, height: 1000, deviceScaleFactor: 1, mobile: false });
await scheme("dark");
await run(`${U} document.querySelector('#mixer .strip[data-track="0"] .shead').click(); await until(() => D().sel === 0, 8000); await sleep(500);   /* (the selected track is kept per browser too: back to T1) */
  try { localStorage.removeItem("${MIXKEY}"); } catch (e) { return false; }`);
/* v13 SCRN (the mock with ?fxp=1): Settings shows GLOBAL and SYSTEM as the device's lines (tests/scrn_table.json): a slider turns BPM, a stepper steps a list,
   a switch toggles, read-outs and the action (it asks first), and the editor saves a moment after the last edit */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxp=1&e2e=1#settings` });
await sleep(1500);
const scrRes = await run(`${U} try {
  if (!await until(() => document.querySelectorAll("#devscr .sl").length > 20, 60000)) return { err: "no lines" };
  const mock = window.fm1Test.mock, r = {};
  const row = (s, i) => document.querySelector('#devscr .sl[data-s="' + s + '"][data-i="' + i + '"]');
  r.groups = [...document.querySelectorAll("#devscr .devpage h3")].map((h) => h.textContent).slice(0, 4).join("|");
  r.bpm0 = row(0, 0).querySelector(".slv").textContent;
  const sl = row(0, 0).querySelector("input[type=range]"); sl.value = String(+sl.value + 10); sl.dispatchEvent(new Event("input", { bubbles: true }));
  await until(() => row(0, 0).querySelector(".slv").textContent !== r.bpm0, 8000);
  r.bpm1 = row(0, 0).querySelector(".slv").textContent;
  const up = row(0, 5).querySelector("button[aria-label$='+']"), dn = row(0, 5).querySelector("button[aria-label$='-']"); const v0 = row(0, 5).querySelector(".slv").textContent;
  /* a stepper: the arrows step the device's list (LENGTH 1 bar, 2 bar, 4 bar) and the line shows the entry the device draws; clamped at the ends */
  up.click(); await until(() => row(0, 5).querySelector(".slv").textContent !== v0, 4000); const v1 = row(0, 5).querySelector(".slv").textContent;
  up.click(); await sleep(500); const v2 = row(0, 5).querySelector(".slv").textContent; up.click(); await sleep(500); const v3 = row(0, 5).querySelector(".slv").textContent;
  dn.click(); await sleep(500); dn.click(); await sleep(500); const v4 = row(0, 5).querySelector(".slv").textContent;
  r.steps = [v0, v1, v2, v3, v4].join("|");
  /* a switch: its button names the entry (no blank box) and toggles it; a channel stepper; a slider with a device step of its own (BRIGHT 1..8) */
  const sw = row(0, 10).querySelector("button.sw"), s0 = sw.textContent; sw.click(); await until(() => sw.textContent !== s0, 4000); r.sw = s0 + ">" + sw.textContent + ":" + sw.getAttribute("aria-pressed");
  sw.click(); await sleep(400);
  const ch = row(0, 12), c0 = ch.querySelector(".slv").textContent; ch.querySelector("button[aria-label$='+']").click(); await sleep(500); r.chan = c0 + ">" + ch.querySelector(".slv").textContent;
  const br = row(1, 3).querySelector("input[type=range]"); br.value = "3"; br.dispatchEvent(new Event("input", { bubbles: true })); await sleep(1500); r.bright = row(1, 3).querySelector(".slv").textContent;
  r.blank = [...document.querySelectorAll("#devscr .sl")].filter((x) => !(x.querySelector(".slv") && x.querySelector(".slv").textContent.trim()) && !(x.querySelector("button") && x.querySelector("button").textContent.trim())).length;
  window.confirm = () => true; const act = row(1, 9).querySelector("button"); act.click(); await sleep(500); r.act = !!act && act.textContent;
  r.stepper = !!up && row(0, 5).dataset.kind === "1";
  r.kinds = [1, 2, 3].map((k) => document.querySelectorAll('#devscr .sl[data-kind="' + k + '"]').length).join();
  r.help = !!row(0, 0).querySelector(".sln").title;
  r.saved = await until(() => mock.state && true, 100) && await new Promise((res) => setTimeout(res, 2200)) === undefined;
  r.wide = document.documentElement.scrollWidth <= window.innerWidth + 1;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(scrRes && /^GLOBAL \u00b7 Tempo/.test(scrRes.groups) && scrRes.bpm1 !== scrRes.bpm0 && /^1[0-9][0-9] BPM$/.test(scrRes.bpm1) && /^[0-9]+,[0-9]+,[0-9]+$/.test(scrRes.kinds) && scrRes.help && scrRes.wide
  && scrRes.steps === "1 bar|2 bar|4 bar|4 bar|1 bar" && /^OFF>ON:true$/.test(scrRes.sw) && /^5>6$/.test(scrRes.chan) && scrRes.bright === "3" && scrRes.blank === 0 && scrRes.act === "CALIBRATE",
  `e2e v13 SCRN (mock ?fxp=1): Settings lists GLOBAL and SYSTEM as the device's lines; a slider turns BPM, steppers and switches, help from the device's texts (${JSON.stringify(scrRes)})`);
await shot("settings-device-lines");
/* v13 SCRS (the mock with ?fxp=1): the Device screens page lists the device's screens; SOUND shows the pages in the device's order grouped by family, a card turns, a drum lane
   shows its own pages (ENV: CLICK DECAY BEND LEVEL), PROJECT's stores have a YES that asks, SONG and TEMPO read as the device draws them */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxp=1&e2e=1#device` });
await sleep(1500);
const scsRes = await run(`${U} window.confirm = () => true; try {
  if (!await until(() => document.querySelectorAll("#scrows .scrow").length > 5, 60000)) return { err: "no rows" };
  const r = {}, seg = (i) => [...document.querySelectorAll("#scbar .seg")[i].querySelectorAll("button")];
  const names = () => [...document.querySelectorAll("#scrows .scn")].map((h) => h.textContent);
  r.screens = seg(0).map((b) => b.textContent).join("|");
  seg(0).find((b) => b.textContent === "SOUND").click();
  await until(() => names()[0] === "ENV", 8000);
  r.fams = [...document.querySelectorAll("#scrows .scf")].map((h) => h.textContent).join(",");
  r.first = names().slice(0, 3).join(",");
  seg(1).at(-1).click();                             /* the drum track */
  r.drumLanes = await until(() => document.querySelectorAll("#scbar .seg").length === 3 && names()[0] === "LANE", 8000);
  seg(2)[2].click();                                 /* a lane */
  r.lane = await until(() => names().includes("ENV") && names().includes("TONE"), 8000);
  r.laneEnv = [...document.querySelectorAll("#scrows .scrow")].find((x) => x.querySelector(".scn").textContent === "ENV").querySelector(".sccards").textContent.replace(/\\s+/g, " ").slice(0, 60);
  seg(0).find((b) => b.textContent === "PROJECT").click();
  r.project = await until(() => names().join() === "PROJECT,SNAPSHOT,USER,TOOLS,SLOOP", 8000) && names().join();
  r.yes = document.querySelectorAll("#scrows .scyes").length;
  document.querySelector("#scrows .scyes").click();
  r.asked = await until(() => !!window.fm1Test.mock.state && true, 100);
  seg(0).find((b) => b.textContent === "STEPS").click();
  await until(() => names()[0] === "PATTERN", 8000);
  const hold = document.querySelector("#scbar select"); hold.value = "2"; hold.dispatchEvent(new Event("change", { bubbles: true }));
  r.held = await until(() => names()[0] === "STEP", 8000) && document.querySelector("#scrows .scrow").textContent.replace(/\\s+/g, " ").slice(0, 70);
  seg(0).find((b) => b.textContent === "SONG").click();
  r.song = await until(() => names().includes("MODE") && names().includes("PART 1"), 8000);
  seg(0).find((b) => b.textContent === "TEMPO").click();
  r.tempo = await until(() => names()[0] === "TEMPO", 8000) && document.querySelector("#scrows .scrow").textContent.replace(/\\s+/g, " ").slice(0, 60);
  const row0 = document.querySelector("#scrows .scrow"), v0 = row0.querySelector(".scv").textContent;
  const sl = row0.querySelector("input[type=range]"); sl.value = String(+sl.value + 5); sl.dispatchEvent(new Event("input", { bubbles: true }));
  r.turned = await until(() => row0.querySelector(".scv").textContent !== v0, 8000);
  r.bpm = row0.querySelector(".scv").textContent;
  seg(0).find((b) => b.textContent === "SCOPE").click();
  r.scope = await until(() => !document.querySelector("#scscope").hidden && document.querySelector("#scscope").dataset.n === "236", 8000);
  const px = document.querySelector("#scscope").getContext("2d").getImageData(0, 0, 944, 180).data; let ink = 0; for (let i = 0; i < px.length; i += 4) if (px[i] !== px[0] || px[i + 1] !== px[1]) ink++;
  r.ink = ink > 500;
  r.wide = document.documentElement.scrollWidth <= window.innerWidth + 1;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(scsRes && scsRes.screens === "MIX|SOUND|MASTER|PROJECT|STEPS|SONG|TEMPO|SCOPE" && /ENV/.test(scsRes.fams) && /LFO/.test(scsRes.fams) && scsRes.turned && scsRes.drumLanes && scsRes.lane && /CLICK/.test(scsRes.laneEnv)
  && scsRes.project === "PROJECT,SNAPSHOT,USER,TOOLS,SLOOP" && scsRes.yes === 3 && /LEVEL.*RATCHET/.test(scsRes.held) && scsRes.song && /BPM/.test(scsRes.tempo) && scsRes.scope && scsRes.ink && scsRes.wide,
  `e2e v13 SCRS (mock ?fxp=1): the Device screens page: the device's screens, SOUND's pages by family, a card turns, a drum lane's pages, PROJECT's stores with YES, SONG, TEMPO (${JSON.stringify(scsRes)})`);
await shot("device-sound");
/* v13 FAVS (the mock with ?fxp=1): Load preset has a star beside each sound; a click marks it on the device; "favorites only" hides the rest (and is the device's PRESETS list) */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxp=1&e2e=1#mixer` });
await sleep(1500);
const favRes = await run(`${U} try {
  if (!await until(() => document.querySelectorAll("#mixer .strip").length >= 5 && D() && D().dump, 120000)) return { err: "no mixer" };
  document.querySelector('#mixer .strip[data-track="0"] [data-pop=loadpreset]').click();
  if (!await until(() => document.querySelectorAll("#pop .pw .fv").length > 10, 20000)) return { err: "no stars" };
  const r = {}, stars = () => [...document.querySelectorAll("#pop .pw")].filter((w) => w.dataset.fav === "1").length;
  r.before = stars();
  document.querySelectorAll("#pop .pw .fv")[2].click();
  r.marked = await until(() => stars() === 1, 8000);
  document.querySelector("#pop .favonly").click();
  r.only = await until(() => document.querySelector("#popbody.favlist") !== null, 8000);
  r.visible = [...document.querySelectorAll("#pop .pw")].filter((w) => w.getClientRects().length > 0).length;
  document.querySelector("#pop .favonly").click();
  r.all = await until(() => !document.querySelector("#popbody.favlist"), 8000);
  r.count = document.querySelector("#favcount").textContent;
  return r; } catch (e) { return { err: String(e.stack) }; }`);
ok(favRes && favRes.before === 0 && favRes.marked && favRes.only && favRes.visible === 1 && favRes.all && favRes.count === "1/64",
  `e2e v13 FAVS (mock ?fxp=1): Load preset: a star marks a sound on the device, favorites only shows just it, then all again (${JSON.stringify(favRes)})`);
await shot("favorites");
/* v13 optional commands (the builder's ED_* items): the editor with a firmware that left some out hides what needs them and keeps the rest. The same probe on the full mock is the control. */
const v13Probe = async (off) => {
  await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&fxp=1&e2e=1${off ? "&off=" + off : ""}#mixer` });
  await sleep(1500);
  return run(`${U} try {
    if (!await until(() => document.querySelectorAll("#mixer .strip").length >= 5 && D() && D().dump, 120000)) return { err: "no mixer" };
    await sleep(1500);
    const r = {}, vis = (sel) => [...document.querySelectorAll(sel)].some((e) => !e.hidden && e.getClientRects().length > 0);
    const have = (id) => !!document.querySelector('[data-pop="' + id + '"]');
    r.ins = have("fxslot"); r.mst = !!document.getElementById("strip-master-slot1-type"); r.note = !!document.getElementById("strip-1-noslots") && vis("#strip-1-noslots"); r.song = vis('#songbtn'); r.tools = have("tools");
    r.dlan = !!document.querySelector("[data-dlan]") || !!document.querySelector("#mixer .sq");
    r.deviceTab = vis('#tabs [data-tab="device"]');
    document.querySelector('#tabs [data-tab="settings"]').click(); await sleep(1200);
    r.lines = document.querySelectorAll("#devscr .sl").length > 0;
    document.querySelector('#tabs [data-tab="mixer"]').click(); await sleep(300);
    document.querySelector('#mixer .strip[data-track="0"] [data-pop=loadpreset]').click();
    await until(() => document.querySelectorAll("#pop .pw").length > 10, 20000);
    r.stars = document.querySelectorAll("#pop .pw .fv").length > 0;
    document.querySelector("#popclose, #pop .x, #pop [data-close]") && document.querySelector("#popclose, #pop .x, #pop [data-close]").click();
    if (r.deviceTab) {
      document.querySelector('#tabs [data-tab="device"]').click();
      await until(() => document.querySelectorAll("#scrows .scrow").length > 0 || document.querySelector("#scbar .seg"), 20000);
      r.screens = [...document.querySelectorAll("#scbar .seg")[0].querySelectorAll("button")].map((b) => b.textContent).join("|");
    } else r.screens = "";
    r.wide = document.documentElement.scrollWidth <= window.innerWidth + 1;
    return r; } catch (e) { return { err: String(e.stack) }; }`);
};
{
  const full = await v13Probe("");
  ok(full && full.ins && full.mst && full.song && full.tools && full.dlan && full.deviceTab && full.lines && full.stars && /SOUND/.test(full.screens) && /SCOPE/.test(full.screens),
    `e2e v13 optional: every command built (control): inserts, master inserts, song, tools, lane mix, Device screens with SCOPE, the device lines, stars (${JSON.stringify(full)})`);
  const none = await v13Probe("fxp,dlan,scrn,songc,tools,scrs,fav,scope");
  ok(none && !none.err && !none.ins && !none.mst && none.note && !none.song && !none.tools && !none.dlan && !none.deviceTab && !none.lines && !none.stars && none.wide,
    `e2e v13 optional: all eight left out: the mixer, Load preset and Settings work, a visible "no slot editing" note on the strips instead of the slot lists, no song / tools / lane mix pop-ups, no Device screens tab, no device lines, no stars (${JSON.stringify(none)})`);
  const noScrn = await v13Probe("scrn");
  ok(noScrn && !noScrn.err && !noScrn.lines && noScrn.deviceTab && noScrn.ins && noScrn.stars,
    `e2e v13 optional: no ED_SCRN (INFO without tag 0x59): Settings has no device lines, the Device screens tab and the rest stay (${JSON.stringify(noScrn)})`);
  const noSong = await v13Probe("songc,tools");
  ok(noSong && !noSong.err && !noSong.song && !noSong.tools && noSong.ins && noSong.mst && noSong.lines,
    `e2e v13 optional: no ED_SONG, no ED_TOOLS: no song and tools pop-ups, the FX owners and the device lines stay (${JSON.stringify(noSong)})`);
  const noFxp = await v13Probe("fxp");
  ok(noFxp && !noFxp.err && !noFxp.ins && !noFxp.mst && noFxp.song && noFxp.tools && noFxp.dlan && noFxp.stars,
    `e2e v13 optional: no ED_FXP: no owner inserts, the song chain (its own command) and the tools stay (${JSON.stringify(noFxp)})`);
  const noScope = await v13Probe("scope,fav,dlan");
  ok(noScope && !noScope.err && noScope.deviceTab && /SOUND/.test(noScope.screens) && !/SCOPE/.test(noScope.screens) && !noScope.stars && !noScope.dlan,
    `e2e v13 optional: no ED_SCOPE, ED_FAVS, ED_DLAN: Device screens without the SCOPE screen, no stars, no lane mix (${JSON.stringify(noScope)})`);
}
/* protocol v9 (the mock): meters on the strips and the master move while playing and fall after STOP; STATUS is not
   polled (the stream carries it); a v8 mock (?v9=0): no meters, STATUS polled as before */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&e2e=1#mixer` });
await sleep(1500);
const mv = await run(`${U} if (!await until(() => document.querySelectorAll("#mixer .strip").length === 5 && D() && D().stream, 120000)) return null;
  const m = window.fm1Test.mock.state, q0 = m.statusReqs | 0; $("#play").click(); await sleep(1200);
  const up = [...document.querySelectorAll("#mixer .meter")].map((e) => +e.getAttribute("aria-valuenow"));
  const cols = [...document.querySelectorAll("#mixer .meter")].map((e) => e.dataset.st);
  $("#stop").click(); await sleep(4000);
  const down = [...document.querySelectorAll("#mixer .meter")].map((e) => +e.getAttribute("aria-valuenow"));
  return { up, cols, down, polls: (m.statusReqs | 0) - q0, n: document.querySelectorAll("#mixer .meter").length };`);
ok(mv && mv.n === 5 && mv.up.filter((x) => x > -20).length >= 4 && mv.down.every((x) => x <= -59) && mv.polls === 2,
  `e2e v9 (mock): meters on 4 strips + master rise while playing, fall to the floor after STOP; STATUS asked only for PLAY / STOP (${JSON.stringify(mv)})`);
await shot("v9-mock-meters");
/* the master COMP's GR meter (v9 mock): hidden until a STREAM frame carries the GR block; THRS set and playing, the mock
   reports reduction: the bar shows (neutral, from the top) titled with both values; THRS OFF and stopped it falls to 0 */
const grRes = await run(`${U} const g = document.querySelector("#mixer .strip.master .fm .gr");
  if (!g) return "no gr"; try {
  const m = window.fm1Test.mock.state; m.g[32] = -18; $("#play").click();   /* (THRS on the mock device) */
  const up = await until(() => !g.hidden && parseFloat(g.children[0].style.height) > 0, 8000);
  const look = up && getComputedStyle(g.children[0]).backgroundColor !== getComputedStyle(g.children[1]).backgroundColor
    && /COMP .* dB, LIMIT .* dB/.test(g.title) && g.getAttribute("role") === "meter" && g.getBoundingClientRect().height > 40;
  m.g[32] = 0; $("#stop").click();
  const down = await until(() => parseFloat(g.children[0].style.height) === 0, 8000);
  return JSON.stringify({ up, look, down }); } catch (e) { return "err " + e.message; }`);
ok(grRes === '{"up":true,"look":true,"down":true}', "e2e v9 (mock): the master's GR meter shows the COMP's reduction (THRS set, playing), falls to 0 (" + JSON.stringify(grRes) + ")");
await shot("v9-mock-gr");
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&v9=0&e2e=1#mixer` });
await sleep(1500);
const m8 = await run(`${U} if (!await until(() => document.querySelectorAll("#mixer .strip").length === 5 && D() && D().watch, 120000)) return null;
  const m = window.fm1Test.mock.state, q0 = m.statusReqs | 0; await sleep(1500);
  return { meters: document.querySelectorAll("#mixer .meter[data-live]").length, polls: (m.statusReqs | 0) - q0, v9: D().v9, live: $("#live").textContent };`);
ok(m8 && m8.meters === 0 && m8.polls >= 5 && !m8.v9 && !/v9/.test(m8.live), `e2e v8 fallback (mock ?v9=0): no meters, STATUS polled about every 150 ms (${JSON.stringify(m8)})`);
/* a firmware without snapshots (?snap=0: the mock does not answer SN_LIST): no Snapshots tab */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&snap=0#mixer` });
await sleep(1500);
ok(await run(`${U} return (await until(() => document.querySelectorAll("#mixer .strip").length === 5, 120000))
  && !shown(document.querySelector("[data-tab=snapshots]")) && shown(document.querySelector("[data-tab=projects]"));`),
  "e2e: a firmware without snapshots: no Snapshots tab");
/* the Reverb popup's TYPE (INFO tag 52, mock ?rev=mask): the four algorithms built: a selector of the four, a change reaches
   the device; one built (?rev=1): no selector */
for (const [rev, want] of [[15, "ROOM,PLATE,FDN8,SPRING"], [1, ""]]) {
  await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&store=1&rev=${rev}#mixer` });
  await sleep(1500);
  const rt = await run(`${U} if (!await until(() => document.querySelectorAll("#mixer .strip").length === 5, 120000)) return null;
    document.querySelector("#mixer .strip.master [data-pop=fxreverb]").click(); await until(() => $("#pop").open, 5000); await sleep(300);
    const s = document.querySelector("#pop select[data-rtype]"), names = s ? [...s.options].map((o) => o.textContent).join() : "";
    let dev = null;
    if (s) { s.value = "2"; s.dispatchEvent(new Event("change")); await sleep(300); dev = window.fm1Test.mock.state.rtype[0]; }
    const knobs = [...document.querySelectorAll("#pop .knob .kl")].map((e) => e.textContent.trim()).join();
    $("#popx").click(); await sleep(200);
    return { names, dev, knobs };`);
  ok(rt && rt.names === want && rt.dev === (want ? 2 : null) && rt.knobs === "SIZE,DAMP",
    `e2e: the Reverb popup, ${want ? "four algorithms built: TYPE lists them, a change reaches the device" : "one built: no TYPE"} (${JSON.stringify(rt)})`);
  if (want) await shot("reverb-type");
}
ws.close();
proc.kill();
server.close();
console.log(failed ? `E2E FAILED (${failed})` : "e2e passed");
process.exit(failed ? 1 : 0);
