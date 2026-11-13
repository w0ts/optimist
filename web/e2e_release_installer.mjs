// SPDX-License-Identifier: GPL-3.0-only
// The release's own install page (fm1-base's web/installer.html, made for Optimist by web/make_site.py --release-site: its
// name, the release package, m-bootloader offered) in a real (headless) Chrome against a simulated FM-1 (web/installer/
// mock_fm1.js) put behind navigator.requestMIDIAccess before the page runs: no real MIDI port is ever opened, downloads are
// refused. It checks the site's pages (front page, manual, emulator, firmware), then walks connect -> confirm (back up
// first) -> write -> done, with the m-bootloader option left off. Not part of `make test` (it needs Chrome and Emscripten).
//   node web/e2e_release_installer.mjs
// CHROME=path overrides the browser; E2E_PORT the first port tried (default 8890).
import { execFileSync, spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { dirname, extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";
import { fakePackage } from "./installer/mock_fm1.js";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const ROOT = join(HERE, "..");
const CHROMES = [process.env.CHROME, "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/usr/bin/google-chrome",
  "/usr/bin/chromium", "/usr/bin/chromium-browser"].filter(Boolean);
const chrome = CHROMES.find((p) => existsSync(p));
if (!chrome) { console.log("e2e: no Chrome found (CHROME=path)"); process.exit(0); }
const PYTHON = process.env.PYTHON || "python3";
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(100)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/* ---- the release's site, from a simulated package (identity FM-1_709 and the version's six digits) */
const PRODUCT = "FM-1_709000400";
const WORK = join(ROOT, "worktrees", "g3-release-e2e");
rmSync(WORK, { recursive: true, force: true });
mkdirSync(WORK, { recursive: true });
writeFileSync(join(WORK, "fake.fwsc"), fakePackage(PRODUCT));
const SITE = join(WORK, "site");
try {
  execFileSync(PYTHON, ["web/make_site.py", "--release-site", "0.3", SITE, join(WORK, "fake.fwsc")], { cwd: ROOT, stdio: ["ignore", "ignore", "inherit"] });
} catch (e) { console.log("e2e: the release's site did not build (Emscripten, fm1base/?)"); process.exit(1); }
const read = (p) => readFileSync(join(SITE, p), "utf8");
ok(existsSync(join(SITE, "firmware", "optimist-0.3.fwsc")) && existsSync(join(SITE, "firmware", "m-bootloader-2.1.fwsc")),
  "the site has the release's package and m-bootloader");
ok(/<title>Optimist · a modular firmware for the M-VAVE FM-1/.test(read("index.html")) && read("index.html").includes("w0ts/optimist"),
  "the front page: Optimist's name, tagline and source");
ok(/<h2[^>]*id="s7"/.test(read("manual/index.html")) && read("manual/index.html").includes("The sequencer"), "the manual: Optimist's guide, seven sections");
ok(existsSync(join(SITE, "emu", "app.wasm")), "the browser emulator is there");
const html = read("install/index.html");
ok(html.includes("Optimist") && !html.includes("{{") && html.includes(`"product":"${PRODUCT}"`) || html.includes(`"product": "${PRODUCT}"`),
  "the install page: Optimist's name, the package's identity, no placeholder left");

const TYPES = { ".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript", ".json": "application/json", ".css": "text/css",
  ".svg": "image/svg+xml", ".png": "image/png", ".wasm": "application/wasm" };
const server = createServer((q, r) => {
  let url = decodeURIComponent(q.url.split("?")[0]), base = SITE;
  if (url.startsWith("/src/")) { base = HERE; url = url.slice(4); }   /* the sources: the mock device */
  if (url.endsWith("/")) url += "index.html";
  const f = normalize(join(base, url));
  if (!f.startsWith(normalize(base)) || !existsSync(f)) { r.writeHead(404); r.end(); return; }
  r.writeHead(200, { "Content-Type": TYPES[extname(f)] || "application/octet-stream" });
  r.end(readFileSync(f));
});
const PORT0 = +(process.env.E2E_PORT || 8890);
let port = PORT0;
for (;; port++) {
  const up = await new Promise((res) => { server.once("error", () => res(false)); server.listen(port, "127.0.0.1", () => res(true)); });
  if (up) break;
  if (port > PORT0 + 49) throw new Error(`no free port in ${PORT0}..${PORT0 + 49}`);
}
const ORIGIN = `http://127.0.0.1:${port}`;
const cdpPort = 9500 + Math.floor(Math.random() * 400);
const prof = join(WORK, "chrome");
mkdirSync(prof, { recursive: true });
const proc = spawn(chrome, ["--headless=new", "--mute-audio", `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${prof}`, "--no-first-run",
  "--disable-background-timer-throttling", "--disable-renderer-backgrounding", "about:blank"], { stdio: "ignore" });
const closeAll = () => { try { proc.kill(); } catch (e) { /* already gone */ } server.close(); };
process.on("exit", closeAll);
for (const sig of ["SIGINT", "SIGTERM", "SIGHUP"]) process.on(sig, () => { closeAll(); process.exit(130); });
let tab;
for (let i = 0; i < 50 && !tab; i++) {
  await sleep(200);
  tab = await fetch(`http://127.0.0.1:${cdpPort}/json/new?about:blank`, { method: "PUT" }).then((r) => r.json()).catch(() => null);
}
const ws = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener("open", r));
let id = 0;
const pending = new Map();
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
ws.addEventListener("message", (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } });
const run = async (expr) => {
  const m = await send("Runtime.evaluate", { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true });
  if (m.result.exceptionDetails) throw new Error(JSON.stringify(m.result.exceptionDetails).slice(0, 600));
  return m.result.result.value;
};
const U = `const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const until = async (f, ms = 60000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) return false; await sleep(50); } return true; };
  const $ = (q) => document.querySelector(q);`;
/* the mock behind requestMIDIAccess, before any page script: the stock V15 plugged in, the package's identity after */
const MOCK = `(() => {
  const ready = Promise.all([import("${ORIGIN}/src/installer/mock_fm1.js"), import("${ORIGIN}/src/fm1base/fm1pkg.js")]).then(([m, p]) => {
    window.__dev = new m.FakeFM1(p.logicalImage(m.fakePackage("${PRODUCT}")), { identity: "FM-1_015", finalIdentity: "${PRODUCT}", writeBlocks: 127 });
  });
  Object.defineProperty(Navigator.prototype, "requestMIDIAccess", { configurable: true, value: async function (o) {
    await ready; if (!o || !o.sysex) throw new Error("no sysex asked"); return window.__dev.access; } });
})();`;
await send("Page.enable");
await send("Runtime.enable");
await send("Browser.setDownloadBehavior", { behavior: "deny" });
await send("Browser.setPermission", { permission: { name: "midi", sysex: true }, setting: "denied", origin: ORIGIN }).catch(() => null);
await send("Page.addScriptToEvaluateOnNewDocument", { source: MOCK });
await send("Emulation.setDeviceMetricsOverride", { width: 1200, height: 1000, deviceScaleFactor: 1, mobile: false });
await send("Page.navigate", { url: ORIGIN + "/install/" });

const ready = await run(`${U} await until(() => $("#go") && !$("#go").disabled, 20000);
  return { go: $("#go") && !$("#go").disabled, title: document.title, status: $("#status").textContent, boot: !$("#mbootopt").hidden,
    alert: $("#alert").textContent, footer: document.body.textContent.includes("github.com/w0ts/optimist") };`);
ok(ready.go && ready.title === "Install · Optimist" && ready.status === "Optimist 0.3", `the package checked against its identity, Install on (${ready.status})`);
ok(ready.boot, "m-bootloader is offered (its package is here and is the one the site was made with)");
ok(ready.footer && !ready.alert, "the footer names Optimist's repository; no alert");
const asked = await run(`${U} $("#go").click(); await until(() => $("#confirm").open, 5000);
  const d = $("#confirm"); return { open: d.open, go: $("#confirmgo").disabled, boot: !$("#confirmboot").hidden };`);
ok(asked.open && asked.go && !asked.boot, "Install asks to back up first (the button is off until it is ticked; m-bootloader not chosen)");
const done = await run(`${U} $("#bakok").click(); $("#confirmgo").click();
  const finished = await until(() => /Done|done/.test($("#status").textContent), 120000);
  return { finished, status: $("#status").textContent, bar: $("#bar").value, bad: window.__dev.bad, served: window.__dev.served, id: window.__dev.identity, alert: $("#alert").textContent };`);
ok(done.finished && done.bad === 0 && done.id === PRODUCT && done.served > 100 && !done.alert,
  `written through the update loader: ${done.served} reads served, none wrong, the FM-1 reports ${done.id} (${done.status})`);
closeAll();
rmSync(WORK, { recursive: true, force: true });
console.log(failed ? `e2e_release_installer: ${failed} FAILED` : "e2e_release_installer: all passed");
process.exit(failed ? 1 : 0);
