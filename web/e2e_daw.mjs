// SPDX-License-Identifier: GPL-3.0-only
// The editor in a real (headless) Chrome against its mock device: the mixer is home, every popup opens with ONE click
// from a strip (NAV) and closes with Escape, its x or a click outside, back to the mixer as it was; the theme switch.
// Not part of `make test` (it needs Chrome). Run from the repo root:
//   node web/e2e_daw.mjs [--shots DIR]
// CHROME=path overrides the browser (default: Google Chrome / Chromium in their usual places).
import { spawn } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const shots = process.argv.includes("--shots") ? process.argv[process.argv.indexOf("--shots") + 1] : null;
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
const U = `const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const until = async (f, ms = 60000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) return false; await sleep(100); } return true; };
  const $ = (q) => document.querySelector(q);
  const shown = (e) => !!e && e.getClientRects().length > 0;`;
/* not connected (?connect=0: no auto-connect): the connect card only, no mixer, no strip, no value */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&connect=0#mixer` });
await sleep(1200);
ok(await run(`${U} return shown($("#connectcard")) && shown($("#connect")) && document.querySelectorAll("#mixer .strip").length === 0
  && [...document.querySelectorAll(".panel")].every((p) => !shown(p)) && !shown($("#tabs")) && !shown($("#tp")) && !shown($("#disconnect"))
  && /installer/i.test($("#connectcard").textContent);`), "e2e: not connected: the connect card only (no mixer, no tabs, no transport)");
await shot("editor-disconnected");
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
  document.querySelector("#mixer .strip.master [data-pop=master]").click(); await until(() => $("#pop").open, 5000); await sleep(300);
  const c = bpmCtl(); $("#popx").click(); await sleep(200);
  return a === 0 && b === 0 && c === 0 && shown($("#bpm")) && +$("#bpm").value > 0;`), "e2e: BPM only in the transport bar (not the master strip, Settings, master FX)");
/* the title bar of a strip selects its track; no Select button, no per-track Project button */
ok(await run(`${U} const h = document.querySelector('#mixer .strip[data-track="1"] .shead'); h.click();
  const okk = await until(() => document.querySelector('#mixer .strip[data-track="1"]').classList.contains("sel") && h.getAttribute("aria-pressed") === "true", 10000);
  const others = [...document.querySelectorAll("#mixer .strip[data-track]")].filter((s) => s.classList.contains("sel")).length;
  const h0 = document.querySelector('#mixer .strip[data-track="0"] .shead'); h0.click();
  const back = await until(() => document.querySelector('#mixer .strip[data-track="0"]').classList.contains("sel"), 10000);
  return okk && back && others === 1 && !document.querySelector("#mixer .selb") && !document.querySelector("#mixer [data-pop=project]");`),
  "e2e: a strip's title bar selects its track (one selected, no Select / Project buttons)");
/* every popup: its opener on a strip (one click), then Escape / x / outside -> the mixer as it was */
const POPS = [["sound", 0], ["sequence", 1], ["loadpreset", 2], ["savepreset", 0], ["kit", 3], ["kitstore", 3], ["lane", 3, 4], ["master", 4]];
const closers = ["Escape", "x", "outside"];
let n = 0;
for (const [pid, strip, lane] of POPS) {
  const closer = closers[n++ % 3];
  const opened = await run(`${U} const before = $("#mixer").innerHTML.length, s = $("#mixer").children[${strip}];
    const b = ${lane != null} ? s.querySelector('[data-pop=lane][data-l="${lane}"]') : s.querySelector('[data-pop=${pid}]');
    if (!b) return "no opener";
    b.click();
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
  ok(opened === "open" && back, `e2e: ${pid}: one click from strip ${strip + 1} opens it, ${closer} back to the mixer`);
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
for (const [k, pid] of [["k", "kit"], ["u", "kitstore"], ["q", "sequence"], ["3", "lane"]]) {
  await press(k); const a = await popNow();
  if (k === "k") await shot("keys-kit");
  if (k === "3") await run(`${U} return $("#poptitle").textContent;`).then((x) => drum.push(x));
  if (k === "k" || k === "u") await press(k); else await press("Escape");
  drum.push(a === pid && await popNow() === "");
}
await press("i"); drum.push(await popNow() === "");
ok(drum[0] === 3 && drum.filter((x) => typeof x === "boolean").every(Boolean) && /Drum kit/.test(String(drum[4])), `e2e: keys: on the drum track K U Q 3 open Kit / User kits / Sequence / lane 3, I is ignored (${drum.join(" | ")})`);
const misc = [];
for (const [k, pid] of [["m", "master"], ["h", "help"]]) { await press(k); misc.push(await popNow() === pid); if (k === "h") { await run(`const h = document.querySelector(".keyhelp"); if (h) h.scrollIntoView({ block: "center" });`); await shot("keys-help"); } await press(k); misc.push(await popNow() === ""); }
await press(","); misc.push(await run(`${U} await sleep(400); return !$("#p-settings").hidden;`));
await run(`document.querySelector("[data-tab=mixer]").click();`);
await press("q"); misc.push(await popNow() === "sequence"); await press("Escape");   /* (the mixer again: the keys work, on the drum track) */
ok(misc.every(Boolean), `e2e: keys: M master, H help (with the Keyboard shortcuts section), comma Settings (${misc.join()})`);
ok(await run(`${U} const t = (q) => (document.querySelector(q) || {}).title || "";
  const ends = [['#mixer .strip[data-track="0"] [data-pop=sound]', " \u00b7 I"], ['#mixer .strip[data-track="0"] [data-pop=sequence]', " \u00b7 Q"],
    ['#mixer .strip[data-track="0"] [data-pop=loadpreset]', " \u00b7 L"], ['#mixer .strip[data-track="0"] [data-pop=savepreset]', " \u00b7 P"],
    ['#mixer .strip[data-track="3"] [data-pop=kit]', " \u00b7 K"], ['#mixer .strip[data-track="3"] [data-pop=kitstore]', " \u00b7 U"],
    ['#mixer .strip[data-track="3"] [data-pop=lane][data-l="0"]', " \u00b7 1"], ['#mixer .strip[data-track="3"] [data-pop=lane][data-l="9"]', " \u00b7 0"],
    ["#mixer .strip.master [data-pop=master]", " \u00b7 M"], ["#helpbtn", " \u00b7 H"], ["#play", " \u00b7 Space"]];
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
    b.click(); await until(() => $("#pop").open && !/Reading|reading/.test($("#status").textContent), 20000); await sleep(500);
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
/* the theme: an FM-1 edition sets the page colours, auto puts them back */
ok(await run(`${U} const sel = $("#theme"); sel.value = "mint"; sel.dispatchEvent(new Event("change")); await sleep(100);
  const bg = getComputedStyle(document.body).backgroundColor; sel.value = "auto"; sel.dispatchEvent(new Event("change")); await sleep(100);
  return bg === "rgb(47, 48, 50)" && getComputedStyle(document.body).backgroundColor !== bg && document.documentElement.dataset.skin === "auto";`),
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
/* a firmware without snapshots (?snap=0: the mock does not answer SN_LIST): no Snapshots tab */
await send("Page.navigate", { url: `http://127.0.0.1:${port}/editor.html?mock=1&auto=0&snap=0#mixer` });
await sleep(1500);
ok(await run(`${U} return (await until(() => document.querySelectorAll("#mixer .strip").length === 5, 120000))
  && !shown(document.querySelector("[data-tab=snapshots]")) && shown(document.querySelector("[data-tab=projects]"));`),
  "e2e: a firmware without snapshots: no Snapshots tab");
ws.close();
proc.kill();
server.close();
console.log(failed ? `E2E FAILED (${failed})` : "e2e passed");
process.exit(failed ? 1 : 0);
