// SPDX-License-Identifier: GPL-3.0-only
// The editor in a real (headless) Chrome against its mock device: the mixer is home, every popup opens with ONE click
// from a strip (NAV) and closes with Escape, its x or a click outside, back to the mixer as it was; the theme switch.
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
const proc = spawn(chrome, ["--headless=new", `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${prof}`, "--no-first-run",
  "--disable-background-timer-throttling", "--disable-renderer-backgrounding", "about:blank"], { stdio: "ignore" });
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
const run = async (expr) => (await send("Runtime.evaluate", { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true })).result.result.value;
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
/* not connected (?connect=0: no auto-connect): the connect card only, no mixer, no strip, no value */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&connect=0#mixer` });
await sleep(1200);
ok(await run(`${U} return shown($("#connectcard")) && shown($("#connect")) && document.querySelectorAll("#mixer .strip").length === 0
  && [...document.querySelectorAll(".panel")].every((p) => !shown(p)) && !shown($("#tabs")) && !shown($("#tp")) && !shown($("#disconnect"))
  && /installer/i.test($("#connectcard").textContent);`), "e2e: not connected: the connect card only (no mixer, no tabs, no transport)");
await shot("editor-disconnected");
/* the theme is a browser preference: on the connect card before a device is connected */
ok(await run(`${U} const sel = $("#theme"); const bg0 = getComputedStyle(document.body).backgroundColor; sel.value = "mint"; sel.dispatchEvent(new Event("change")); await sleep(100);
  const r = shown(sel) && !!sel.closest("#connectcard") && getComputedStyle(document.body).backgroundColor !== bg0 && document.documentElement.dataset.skin === "mint";
  sel.value = "auto"; sel.dispatchEvent(new Event("change")); await sleep(100); return r;`), "e2e: not connected: the theme picker is on the connect card and works");
/* auto-connect: the page connects to the (mock) device on load, no click */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0#mixer` });
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
  const rows = { head: ".shead", name: ".snd", group: ".pops", seq: ".seqb", ov: ".ov", content: ".content", sends: ".fx", fader: ".fm", level: ".lv", bottom: ".row2" };
  const bad = Object.entries(rows).filter(([, q]) => new Set(trk.map((s) => top(s, q))).size !== 1).map(([k]) => k);
  const panTop = trk.map((s) => Math.round([...s.children].find((e) => e.classList.contains("knob")).getBoundingClientRect().top));
  const masterBad = [["head", ".shead"], ["fader", ".fm"]].filter(([, q]) => top(strips[4], q) !== top(strips[0], q)).map(([k]) => k);
  const sends = trk.map((s) => [...s.querySelectorAll(".fx .knob")].map((k) => k.querySelector(".kl").textContent + "@" + Math.round(k.getBoundingClientRect().left - s.getBoundingClientRect().left)).join());
  const sendOk = new Set(sends).size === 1 && sends[0].split(",").map((x) => x.split("@")[0]).join() === "DST,CHO,DLY,REV";
  const fmt = trk.map((s, i) => [...s.querySelectorAll(".fx .knob")].map((k) => (i === 3 ? /^\\d+$|^--$/ : /%$|^--$/).test(k.querySelector(".kv").textContent.trim())).every(Boolean));
  const meters = trk.map((s) => { const f = s.querySelector(".fader").getBoundingClientRect(), m = s.querySelector(".meter").getBoundingClientRect(); return m.left >= f.right && Math.abs(m.top - f.top) < 4 && Math.abs(m.height - f.height) < 4; });
  const sl = trk.map((s) => s.querySelectorAll("input[type=range]").length);
  const panKnob = trk.every((s) => [...s.children].some((e) => e.classList.contains("knob") && e.getAttribute("role") === "slider" && /Pan/i.test(e.getAttribute("aria-label"))));
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
  const fit = strips[3].querySelector(".fx").scrollHeight <= Math.max(...strips.slice(0, 3).map((s) => s.querySelector(".fx").scrollHeight));   /* (no taller than a synth strip's) */
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
const POPS = [["sound", 0], ["sequence", 1], ["loadpreset", 2], ["savepreset", 0], ["kit", 3], ["kitstore", 3], ["lane", 3, 4], ["fxdelay", 4], ["fxreverb", 4], ["fxchorus", 4]];
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
for (const [k, pid] of [["d", "fxdelay"], ["r", "fxreverb"], ["c", "fxchorus"], ["h", "help"]]) { await press(k); misc.push(await popNow() === pid); if (k === "h") { await run(`const h = document.querySelector(".keyhelp"); if (h) h.scrollIntoView({ block: "center" });`); await shot("keys-help"); } await press(k); misc.push(await popNow() === ""); }
await press(","); misc.push(await run(`${U} await sleep(400); return !$("#p-settings").hidden;`));
await run(`document.querySelector("[data-tab=mixer]").click();`);
await press("q"); misc.push(await popNow() === "sequence"); await press("Escape");   /* (the mixer again: the keys work, on the drum track) */
ok(misc.every(Boolean), `e2e: keys: D R C Delay / Reverb / Chorus, H help (with the Keyboard shortcuts section), comma Settings (${misc.join()})`);
ok(await run(`${U} const t = (q) => (document.querySelector(q) || {}).title || "";
  const ends = [['#mixer .strip[data-track="0"] [data-pop=sound]', " \u00b7 I"], ['#mixer .strip[data-track="0"] [data-pop=sequence]', " \u00b7 Q"],
    ['#mixer .strip[data-track="0"] [data-pop=loadpreset]', " \u00b7 L"], ['#mixer .strip[data-track="0"] [data-pop=savepreset]', " \u00b7 P"],
    ['#mixer .strip[data-track="3"] [data-pop=kit]', " \u00b7 K"], ['#mixer .strip[data-track="3"] [data-pop=kitstore]', " \u00b7 U"],
    ['#mixer .strip[data-track="3"] [data-pop=lane][data-l="0"]', " \u00b7 1"], ['#mixer .strip[data-track="3"] [data-pop=lane][data-l="9"]', " \u00b7 0"],
    ["#mixer .strip.master [data-pop=fxdelay]", " \u00b7 D"], ["#mixer .strip.master [data-pop=fxreverb]", " \u00b7 R"], ["#mixer .strip.master [data-pop=fxchorus]", " \u00b7 C"], ["#helpbtn", " \u00b7 H"], ["#play", " \u00b7 Space"]];
  const bad = ends.filter(([q, e]) => !t(q).endsWith(e) || document.querySelector(q).getAttribute("aria-label") !== t(q)).map(([q]) => q);
  const lay = ["0", "1", "2"].every((n) => { const s = $('#mixer .strip[data-track="' + n + '"]'); const g = s.querySelector(".pops"), q = s.querySelector("[data-pop=sequence]");
    return g.querySelectorAll("button").length === 3 && q.getBoundingClientRect().top >= g.getBoundingClientRect().bottom && q.getBoundingClientRect().height >= 32; });
  const d = $('#mixer .strip[data-track="3"]'); const dg = d.querySelector(".pops");
  const lay2 = dg.querySelectorAll("button").length === 2 && d.querySelector("[data-pop=sequence]").getBoundingClientRect().top >= dg.getBoundingClientRect().bottom && d.querySelectorAll(".lanes .ln").length === 16;
  return JSON.stringify({ bad, lay, lay2 });`) === '{"bad":[],"lay":true,"lay2":true}', "e2e: tooltips end with their key; strip layout: group row (3 / 2 buttons), Sequence below");
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
/* the master strip: no Settings button, one icon + return knob per FX, each popup has its parameters; Settings has none of them */
const masterRes = await run(`${U} const m = document.querySelector("#mixer .strip.master");
  const noSet = ![...m.querySelectorAll("button")].some((b) => /settings/i.test(b.title + b.getAttribute("aria-label")));
  const groups = [...m.querySelectorAll(".fxg")], knobs = [...m.querySelectorAll(".mk .knob")].map((k) => k.getAttribute("aria-label"));
  const shape = groups.length === 3 && groups.every((g) => g.querySelectorAll("button.ib").length === 1 && g.querySelectorAll(".knob").length === 1
    && g.querySelector("button.ib").getBoundingClientRect().width >= 32 && g.querySelector("button.ib").getBoundingClientRect().height >= 32 && g.querySelector("button.ib").title && g.querySelector(".knob").title);
  const want = { fxdelay: "TIME FDBK COLR MIX", fxreverb: "SIZE DAMP", fxchorus: "CRT CDP" }, got = {};
  for (const [id, labels] of Object.entries(want)) {
    m.querySelector("[data-pop=" + id + "]").click(); await until(() => $("#pop").open && $("#pop").dataset.pop === id, 5000); await sleep(300);
    got[id] = [...document.querySelectorAll("#popbody .group[data-fx=" + id + "] .kl")].map((e) => e.textContent.trim()).join(" ") === labels
      && [...document.querySelectorAll("#popbody .group[data-fx=" + id + "] .knob")].every((k) => k.title && k.title !== k.querySelector(".kl").textContent);
    $("#popx").click(); await sleep(200);
  }
  document.querySelector("[data-tab=settings]").click(); await sleep(300);
  const labels = [...document.querySelectorAll("#p-settings .knob .kl, #p-settings .pk .kl, #p-settings .row > span:first-child")].filter(shown).map((e) => e.textContent.trim());
  const moved = ["DUST", "DUCK", "FILT", "TIME", "FDBK", "COLR", "MIX", "SIZE", "DAMP", "CRT", "CDP", "LVL", "REV"];
  const left = moved.filter((l) => labels.includes(l));
  document.querySelector("[data-tab=mixer]").click(); await sleep(300);
  return JSON.stringify({ noSet, shape, knobs: knobs.join(), got, left, hasRoll: labels.includes("ROLL") });`);
ok(masterRes === '{"noSet":true,"shape":true,"knobs":"MASTER DUST,MASTER DUCK,MASTER FILT","got":{"fxdelay":true,"fxreverb":true,"fxchorus":true},"left":[],"hasRoll":true}',
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
ok(soundFx === '{"heads":["FX","SLICER","BYPASS"],"labels":"DST,CHO,DLY,REV,SLCR,PAT,RATE,DEPTH,FX","glob":[]}',
  "e2e: the track's Sound popup: its own FX (sends, SLICER, bypass), no global DLY / REV/CHO pages " + (soundFx && soundFx.length < 300 ? soundFx : ""));
/* the theme (Settings > Appearance, or the connect card): an FM-1 edition sets the page colours, auto puts them back */
ok(await run(`${U} document.querySelector("[data-tab=settings]").click(); await sleep(300);
  const inSet = shown($("#theme")) && !!$("#theme").closest("#p-settings") && !document.querySelector(".tbar #theme") && $("#appearance") === $("#p-settings").firstElementChild.firstElementChild;
  const sel = $("#theme"); sel.value = "mint"; sel.dispatchEvent(new Event("change")); await sleep(100);
  const bg = getComputedStyle(document.body).backgroundColor; sel.value = "auto"; sel.dispatchEvent(new Event("change")); await sleep(100);
  document.querySelector("[data-tab=mixer]").click(); await sleep(200);
  return inSet && bg === "rgb(47, 48, 50)" && getComputedStyle(document.body).backgroundColor !== bg && document.documentElement.dataset.skin === "auto";`),
  "e2e: the theme switch (Mint, back to auto)");
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
/* protocol v9 (the mock): meters on the strips and the master move while playing and fall after STOP; STATUS is not
   polled (the stream carries it); a v8 mock (?v9=0): no meters, STATUS polled as before */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&e2e=1#mixer` });
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
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&v9=0&e2e=1#mixer` });
await sleep(1500);
const m8 = await run(`${U} if (!await until(() => document.querySelectorAll("#mixer .strip").length === 5 && D() && D().watch, 120000)) return null;
  const m = window.fm1Test.mock.state, q0 = m.statusReqs | 0; await sleep(1500);
  return { meters: document.querySelectorAll("#mixer .meter[data-live]").length, polls: (m.statusReqs | 0) - q0, v9: D().v9, live: $("#live").textContent };`);
ok(m8 && m8.meters === 0 && m8.polls >= 5 && !m8.v9 && !/v9/.test(m8.live), `e2e v8 fallback (mock ?v9=0): no meters, STATUS polled about every 150 ms (${JSON.stringify(m8)})`);
/* a firmware without snapshots (?snap=0: the mock does not answer SN_LIST): no Snapshots tab */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&snap=0#mixer` });
await sleep(1500);
ok(await run(`${U} return (await until(() => document.querySelectorAll("#mixer .strip").length === 5, 120000))
  && !shown(document.querySelector("[data-tab=snapshots]")) && shown(document.querySelector("[data-tab=projects]"));`),
  "e2e: a firmware without snapshots: no Snapshots tab");
/* the Reverb popup's TYPE (INFO tag 52, mock ?rev=mask): the four algorithms built: a selector of the four, a change reaches
   the device; one built (?rev=1): no selector */
for (const [rev, want] of [[15, "ROOM,PLATE,FDN8,SPRING"], [1, ""]]) {
  await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&rev=${rev}#mixer` });
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
