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
/* every popup: its opener on a strip (one click), then Escape / x / outside -> the mixer as it was */
const POPS = [["sound", 0], ["sequence", 1], ["loadpreset", 2], ["savepreset", 0], ["project", 0], ["kit", 3], ["kitstore", 3], ["lane", 3, 4], ["master", 4]];
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
  document.querySelector('[data-tab=projects]').click();
  if (!await until(() => document.querySelectorAll("#snaps tr").length === 5, 10000)) return false;
  document.querySelector('#snaps tr[data-slot="0"] button').click();
  if (!await until(() => /LIVE SET/.test($("#snaps").textContent), 10000)) return false;
  window.prompt = () => "";
  document.querySelectorAll('#snaps tr[data-slot="1"] button')[0].click();
  if (!await until(() => document.querySelectorAll('#snaps tr[data-slot="1"] .pill').length === 1, 10000)) return false;
  [...document.querySelectorAll('#snaps tr[data-slot="0"] button')].find((b) => /Load/.test(b.textContent)).click();
  return until(() => document.querySelectorAll('#snaps tr[data-slot="8"] .pill[data-st=ok]').length === 1, 10000);`),
  "e2e: Projects > Snapshots: 4 slots + BEFORE LOAD, Save (named, then from the work), Load fills BEFORE LOAD");
await sleep(300);
await shot("projects-snapshots");
ws.close();
proc.kill();
server.close();
console.log(failed ? `E2E FAILED (${failed})` : "e2e passed");
process.exit(failed ? 1 : 0);
