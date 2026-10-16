// SPDX-License-Identifier: GPL-3.0-only
// The installer (web/installer/installer.js) in a real (headless) Chrome against a simulated FM-1 (web/installer/mock_fm1.js)
// put behind navigator.requestMIDIAccess before the page runs: no real MIDI port is ever opened (the browser's MIDI
// permission is also denied) and downloads are refused. It walks connect -> choose -> install -> done, then the errors (the
// cable pulled while writing, then the resume; no FM-1; a file that is not a package), the keyboard, light / dark, a 390 px
// phone, a browser without Web MIDI, and the shipped module mounted by a host page (the one-page app's way, onNavigate).
// Not part of `make test` (it needs Chrome). Run from the repo root:
//   node web/e2e_installer.mjs [--shots DIR] [--style FILE.css]
// --style: the page with another shared style in place of web/style/optimist.css (a terminus.ttf beside it is inlined as
// make_site.py does), e.g. the editor's look of a branch that is not merged yet. FM1_V15=FM-1.fwsc: also the return to
// the official V15 with the real file. CHROME=path overrides the browser; E2E_PORT the first port tried (default 8850).
import { execFileSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { dirname, extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";
import { fakePackage } from "./installer/mock_fm1.js";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const ROOT = join(HERE, "..");
const arg = (k) => (process.argv.includes(k) ? process.argv[process.argv.indexOf(k) + 1] : null);
const shots = arg("--shots"), styleFile = arg("--style");
const CHROMES = [process.env.CHROME, "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/usr/bin/google-chrome",
  "/usr/bin/chromium", "/usr/bin/chromium-browser", "C:/Program Files/Google/Chrome/Application/chrome.exe"].filter(Boolean);
const chrome = CHROMES.find((p) => existsSync(p));
if (!chrome) { console.log("e2e: no Chrome found (CHROME=path)"); process.exit(0); }
const PYTHON = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(84)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/* ---- the site, made by make_site.py from a simulated package (the manifest's flavours, each with that package) */
const WORK = join(ROOT, "build", "web-installer-e2e");
rmSync(WORK, { recursive: true, force: true });
mkdirSync(WORK, { recursive: true });
const PRODUCT = "FM-1_720";
const raw = fakePackage(PRODUCT);
writeFileSync(join(WORK, "fake.fwsc"), raw);
writeFileSync(join(WORK, "bad.fwsc"), new Uint8Array(4000));
writeFileSync(join(WORK, "sloop.fwsc"), fakePackage("FM-1_905"));
const SHA = createHash("sha256").update(raw).digest("hex");
const SITE = join(WORK, "site");
execFileSync(PYTHON, ["-c", "import json, sys; sys.path.insert(0, 'web'); import make_site; from pathlib import Path; "
  + "fl = [{**f, 'pkg': Path(sys.argv[1])} for f in json.load(open('web/flavours.json'))['flavours']]; make_site.main(None, '0.1-e2e', sys.argv[2], fl)",
  join(WORK, "fake.fwsc"), SITE], { cwd: ROOT, stdio: ["ignore", "ignore", "inherit"], env: { ...process.env, OPTIMIST_SOURCE_REF: "v0.1-e2e" } });
const PAGE = join(SITE, "webapp", "installer", "index.html");
if (styleFile) {                                     /* another shared style (and its font) in place of ours */
  let css = readFileSync(styleFile, "utf8");
  const ttf = join(dirname(styleFile), "terminus.ttf");
  if (existsSync(ttf)) css = css.replace('url("terminus.ttf") format("truetype")', `url(data:font/ttf;base64,${readFileSync(ttf).toString("base64")}) format("truetype")`);
  const page = readFileSync(PAGE, "utf8"), ours = readFileSync(join(HERE, "style", "optimist.css"), "utf8");
  if (!page.includes(ours)) throw new Error("--style: the page does not hold web/style/optimist.css");
  writeFileSync(PAGE, page.replace(ours, () => css));
  console.log(`style: ${styleFile}`);
}
const site = JSON.parse(readFileSync(join(SITE, "webapp", "installer", "site.json"), "utf8"));
/* a host page: the shipped module, mounted as the one-page app will (two packages, onNavigate) */
writeFileSync(join(SITE, "webapp", "host.html"), `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>host</title>
<style>${readFileSync(join(HERE, "style", "optimist.css"), "utf8")}</style></head><body class="o-ui"><main class="o-page"><div id="tab"></div></main>
<script type="module">import { mountInstaller } from "./installer/installer.js";
const site = await fetch("./installer/site.json").then((r) => r.json());
const two = [...site.packages, { ...site.packages[0], id: "drums", title: "Drum machine", description: "A second flavour, for the test." }];
window.navs = []; window.inst = mountInstaller(document.getElementById("tab"), { ...site, packages: two.map((p) => ({ ...p, pkg: p.pkg.replace("../../", "../") })), onNavigate: (w) => window.navs.push(w) });
</script></body></html>`);

const TYPES = { ".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript", ".json": "application/json", ".css": "text/css",
  ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon" };
const server = createServer((q, r) => {
  let url = decodeURIComponent(q.url.split("?")[0]), base = SITE;
  if (url.startsWith("/src/")) { base = HERE; url = url.slice(4); }   /* the sources: the mock device */
  if (url.endsWith("/")) url += "index.html";
  const f = normalize(join(base, url));
  if (!f.startsWith(normalize(base)) || !existsSync(f)) { r.writeHead(404); r.end(); return; }
  r.writeHead(200, { "Content-Type": TYPES[extname(f)] || "application/octet-stream" });
  r.end(readFileSync(f));
});
const PORT0 = +(process.env.E2E_PORT || 8850);
let port = PORT0;
for (;; port++) {
  const up = await new Promise((res) => { server.once("error", () => res(false)); server.listen(port, "127.0.0.1", () => res(true)); });
  if (up) break;
  if (port > PORT0 + 49) throw new Error(`no free port in ${PORT0}..${PORT0 + 49}`);
}
const ORIGIN = `http://127.0.0.1:${port}`;
const cdpPort = 9400 + Math.floor(Math.random() * 400);
const prof = join(WORK, "chrome");
mkdirSync(prof, { recursive: true });
const proc = spawn(chrome, ["--headless=new", "--mute-audio", `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${prof}`, "--no-first-run",
  "--disable-background-timer-throttling", "--disable-renderer-backgrounding", "about:blank"], { stdio: "ignore" });
const closeAll = () => { try { proc.kill(); } catch (e) { /* already gone */ } server.close(); };
process.on("exit", closeAll);
for (const sig of ["SIGINT", "SIGTERM", "SIGHUP"]) process.on(sig, () => { closeAll(); process.exit(130); });
process.on("uncaughtException", (e) => { console.error(e); closeAll(); process.exit(1); });
let tab;
for (let i = 0; i < 50 && !tab; i++) {
  await sleep(200);
  tab = await fetch(`http://127.0.0.1:${cdpPort}/json/new?about:blank`, { method: "PUT" }).then((r) => r.json()).catch(() => null);
}
const ws = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener("open", r));
let id = 0;
const pending = new Map(), events = [];
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
ws.addEventListener("message", (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); return; }
  if (m.method) events.push(m);
});
const run = async (expr) => {
  const m = await send("Runtime.evaluate", { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true });
  if (m.result.exceptionDetails) throw new Error(JSON.stringify(m.result.exceptionDetails).slice(0, 600));
  return m.result.result.value;
};
const shot = async (name) => {
  if (!shots) return;
  mkdirSync(shots, { recursive: true });
  const s = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: true });
  writeFileSync(join(shots, name + ".png"), Buffer.from(s.result.data, "base64"));
};
const full = async (name, w) => {                    /* the whole page (its height), then the viewport back */
  const hgt = await run(`return Math.ceil(document.documentElement.scrollHeight);`);
  await send("Emulation.setDeviceMetricsOverride", { width: w, height: hgt, deviceScaleFactor: w < 500 ? 2 : 1, mobile: w < 500 });
  await sleep(150);
  await shot(name);
  await metrics(w, w < 500 ? 844 : 1000, w < 500);
};
const metrics = (width, height, mobile) => send("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: mobile ? 2 : 1, mobile });
const scheme = (s) => send("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: s }] });
const U = `const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const until = async (f, ms = 60000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) return false; await sleep(50); } return true; };
  const $ = (q) => document.querySelector(q); const vis = (q) => { const e = $(q); return !!e && !e.hidden && e.getClientRects().length > 0; };
  const D = () => window.__dev;`;
/* the mock behind requestMIDIAccess, before any page script (?nomidi: a browser without Web MIDI) */
const MOCK = `(() => {
  if (location.search.includes("nomidi")) { Object.defineProperty(Navigator.prototype, "requestMIDIAccess", { configurable: true, value: undefined }); return; }
  const ready = Promise.all([import("${ORIGIN}/src/installer/mock_fm1.js"), import("${ORIGIN}/src/fm1pkg.js")]).then(([m, p]) => {
    window.__M = m; window.__dev = new m.FakeFM1(p.logicalImage(m.fakePackage("${PRODUCT}")), { plugged: false, identity: "FM-1_015", finalIdentity: "${PRODUCT}", writeBlocks: 127 });
  });
  window.__midiCalls = 0;
  Object.defineProperty(Navigator.prototype, "requestMIDIAccess", { configurable: true, value: async function (o) {
    window.__midiCalls++; await ready; if (!o || !o.sysex) throw new Error("no sysex asked"); return window.__dev.access; } });
})();`;

await send("Page.enable");
await send("Runtime.enable");
await send("Browser.setDownloadBehavior", { behavior: "deny" });
await send("Browser.setPermission", { permission: { name: "midi", sysex: true }, setting: "denied", origin: ORIGIN }).catch(() => null);
await send("Page.addScriptToEvaluateOnNewDocument", { source: MOCK });
const open = async (path, w = 1440, s = "dark") => {
  await metrics(w, w < 500 ? 844 : 1000, w < 500);
  await scheme(s);
  await send("Page.navigate", { url: ORIGIN + path });
  return run(`${U} await until(() => document.readyState === "complete" && $(".oi") && (window.__dev || location.search.includes("nomidi"))); await until(() => $(".oi-pkg") && $(".oi-pkg").dataset.kind, 20000);
    return { kind: $(".oi-pkg").dataset.kind, text: $(".oi-pkg").textContent };`);
};

/* ---- 1 the page: four steps, the package checked, what it says */
const start = await open("/webapp/installer/");
const p0 = await run(`${U} return { steps: [...document.querySelectorAll(".oi-step .oi-h")].map((e) => e.textContent), h1: $("h1").textContent,
  radios: [...document.querySelectorAll(".oi-choice:not([hidden]) .oi-radio")].map((r) => [r.value, r.checked]), meta: $(".oi-meta").textContent, go: $(".oi-go").disabled,
  find: $(".oi-find, .oi-row .o-btn").disabled, tiles: document.querySelectorAll(".tile, .tiles").length, head: !!$(".o-head [aria-current=page]"),
  unlabeled: [...document.querySelectorAll(".oi input")].filter((i) => !(i.labels && i.labels.length) && !i.getAttribute("aria-label")).length,
  nameless: [...document.querySelectorAll(".oi button")].filter((b) => !b.textContent.trim() && !b.getAttribute("aria-label")).length,
  source: $(".oi-foot a").href, about: [...document.querySelectorAll(".oi-foot a")].map((a) => a.getAttribute("href")) };`);
ok(start.kind === "ok" && /^Ready: Synth 0\.1-e2e · FM-1_720$/.test(start.text), `step 2: the site's package downloaded and checked (${start.text})`);
ok(JSON.stringify(p0.steps) === JSON.stringify(["1Connect", "2Choose", "3Install", "4When something goes wrong"]) && p0.h1 === "Install Optimist" && p0.head && !p0.tiles,
  "the page: one column of four numbered steps, the shared header, no SLOOP tiles");
ok(JSON.stringify(p0.radios) === JSON.stringify([["0", true], ["file", false]]) && !p0.go && !p0.find, "the manifest's flavour chosen, a file of yours offered; Find and Install on");
ok(p0.meta.includes(`sha256 ${SHA}`) && p0.meta.includes("built from v0.1-e2e") && p0.source === "https://github.com/w0ts/optimist/tree/v0.1-e2e" && p0.about.includes("../../LICENSING.md"),
  "the version, the tag, the package's sha256 small at the end; Source at the tag; About and licences");
ok(!p0.unlabeled && !p0.nameless, "every field has a label, every button a name");
await full("installer-desktop-dark-start", 1440);

/* ---- 2 connect: nothing plugged -> not found and what to do; plugged -> found by itself (statechange) */
const nf = await run(`${U} $(".oi-step .o-btn").click(); await until(() => $(".oi-found").dataset.kind === "err");
  return { t: $(".oi-found").textContent, help: vis(".oi-help"), items: $(".oi-help").textContent, calls: window.__midiCalls };`);
ok(nf.t === "No FM-1 found." && nf.help && /OCT− alone/.test(nf.items) && nf.calls === 1, "step 1: no FM-1: said, with what to do (cable, hub, other apps, USB rescue)");
await shot("installer-desktop-dark-notfound");
const fd = await run(`${U} D().plug("FM-1_015", "FM-1"); await until(() => $(".oi-found").dataset.kind === "ok", 10000);
  return { t: $(".oi-found").textContent, help: vis(".oi-help") };`);
ok(fd.t === "Found: the official firmware V15 (FM-1_015)" && !fd.help, "step 1: plugged in: found by itself, in words (the official firmware V15)");

/* ---- 3 install: the question first, then the four phases to Done */
const ask = await run(`${U} $(".oi-go").click(); await until(() => $("dialog.oi-ask[open]"), 3000); const d = $("dialog.oi-ask");
  return { open: !!d && d.open, focusIn: d && d.contains(document.activeElement), text: d && d.textContent };`);
ok(ask.open && ask.focusIn && /Back up first\?/.test(ask.text) && /Open the Backup/.test(ask.text), "Install asks to back up first (a modal dialog, focus in it)");
await shot("installer-desktop-dark-ask");
const esc = await run(`${U} $("dialog.oi-ask").dispatchEvent(new Event("cancel", { cancelable: true })); await sleep(100); return { gone: !$("dialog.oi-ask"), served: D().served };`);
ok(esc.gone && esc.served === 0, "Escape (cancel) closes it: nothing sent");
const mid = await run(`${U} $(".oi-go").click(); await until(() => $("dialog.oi-ask[open]"), 3000);
  [...$("dialog.oi-ask").querySelectorAll("button")].find((b) => b.textContent === "Install now").click();
  await until(() => document.querySelector(".oi-phases li:nth-child(3)").dataset.st === "on" && $(".oi-bar").value > 40, 60000);
  return { st: [...document.querySelectorAll(".oi-phases li")].map((l) => l.dataset.st), bar: $(".oi-bar").value, status: $(".oi-status").textContent,
    go: $(".oi-go").disabled, radios: [...document.querySelectorAll(".oi-radio")].every((r) => r.disabled), busy: window.optimistInstaller.busy };`);
ok(JSON.stringify(mid.st) === JSON.stringify(["done", "done", "on", "wait"]) && mid.bar > 40 && mid.bar < 96 && mid.status === "Writing…" && mid.go && mid.radios && mid.busy,
  `writing: checked and switched done, Writing on, the bar at ${mid.bar} %, nothing to click meanwhile`);
await full("installer-desktop-dark-writing", 1440);
const done = await run(`${U} await until(() => vis(".oi-ok"), 90000);
  return { ok: $(".oi-ok").textContent, st: [...document.querySelectorAll(".oi-phases li")].map((l) => l.dataset.st), bar: $(".oi-bar").value, bad: D().bad,
    go: $(".oi-go").textContent, err: vis(".oi-err"), busy: window.optimistInstaller.busy, id: D().identity };`);
ok(/^Done\. The FM-1 runs Synth \(FM-1_720\)\./.test(done.ok) && done.st.every((s) => s === "done") && done.bar === 100 && done.bad === 0 && !done.err && !done.busy && done.id === PRODUCT,
  `done: the four phases ticked, the bar full, the identity checked, every read answered right (${done.ok})`);
await full("installer-desktop-dark-done", 1440);

/* ---- errors: the cable pulled while writing, then Install again resumes; no FM-1 at all */
const lost = await run(`${U} D().identity = "${PRODUCT}"; D().served = 0; D().unplugAfter = 24; $(".oi-go").click();
  await until(() => vis(".oi-err"), 90000);
  return { err: $(".oi-err").textContent, st: [...document.querySelectorAll(".oi-phases li")].map((l) => l.dataset.st), go: $(".oi-go").disabled };`);
ok(/^The USB connection was lost\. Replug the cable and press Install again/.test(lost.err) && lost.st[2] === "fail" && !lost.go,
  `cable pulled while writing: said plainly, the phase marked, Install on again (${lost.st.join(" ")})`);
await full("installer-desktop-dark-error", 1440);
const res = await run(`${U} D().unplugAfter = Infinity; D().served = 0; D().plug("ota-FM-1_900", "Felucca Update"); await sleep(800); $(".oi-go").click();
  await until(() => vis(".oi-ok") || ($(".oi-err") && !$(".oi-err").hidden && D().served > 0), 90000);
  return { ok: vis(".oi-ok") && $(".oi-ok").textContent, err: vis(".oi-err"), found: $(".oi-found").textContent, bad: D().bad };`);
ok(res.ok && /FM-1_720/.test(res.ok) && !res.err && res.bad === 0 && /update mode \(ota-FM-1_900\)/.test(res.found),
  "replugged in update mode: found as such, Install again finishes the write and checks the FM-1 comes back");
const none = await run(`${U} D().unplug(); await sleep(800); $(".oi-go").click(); await until(() => vis(".oi-err"), 20000); return $(".oi-err").textContent;`);
ok(/^No FM-1 found\. Check the cable \(step 1\)/.test(none), "no FM-1 at Install: said, pointing at step 1");

/* ---- a file of yours: not a package -> refused in words; a SLOOP package -> ready */
const doc = await send("DOM.getDocument", { depth: -1 });
const fileNode = await send("DOM.querySelector", { nodeId: doc.result.root.nodeId, selector: ".oi-choice[data-value=file] input[type=file]" });
await send("DOM.setFileInputFiles", { nodeId: fileNode.result.nodeId, files: [join(WORK, "bad.fwsc")] });
const bad = await run(`${U} await until(() => $(".oi-pkg").dataset.kind === "err", 5000);
  return { t: $(".oi-pkg").textContent, go: $(".oi-go").disabled, checked: $(".oi-radio[value=file]").checked };`);
ok(bad.checked && bad.go && /^This file is not a package this page can install/.test(bad.t), "a file that is not a package: refused in words, Install off");
await send("DOM.setFileInputFiles", { nodeId: fileNode.result.nodeId, files: [join(WORK, "sloop.fwsc")] });
const good = await run(`${U} await until(() => $(".oi-pkg").dataset.kind === "ok", 5000); return { t: $(".oi-pkg").textContent, go: $(".oi-go").disabled, meta: $(".oi-meta").textContent };`);
ok(/^Ready: sloop\.fwsc · FM-1_905$/.test(good.t) && !good.go && !/built from/.test(good.meta), "a SLOOP / Felucca package of yours: checked, ready (no 'built from': not the site's)");
const stockNode = await send("DOM.querySelector", { nodeId: doc.result.root.nodeId, selector: ".oi-stockfield input[type=file]" });
await send("DOM.setFileInputFiles", { nodeId: stockNode.result.nodeId, files: [join(WORK, "fake.fwsc")] });
const nst = await run(`${U} await until(() => $(".oi-stockmsg").dataset.kind, 5000); return { t: $(".oi-stockmsg").textContent, row: vis(".oi-choice[data-value=stock]") };`);
ok(/^Not the official file: /.test(nst.t) && !nst.row, "back to V15: any other file is refused (the SHA-256 of the official one)");
if (process.env.FM1_V15 && existsSync(process.env.FM1_V15)) {
  await send("DOM.setFileInputFiles", { nodeId: stockNode.result.nodeId, files: [process.env.FM1_V15] });
  const v = await run(`${U} await until(() => $(".oi-stockmsg").dataset.kind === "ok", 5000); return { go: $(".oi-go").textContent, warn: $(".oi-step:nth-child(1)") && vis(".o-msg.warn"), checked: $(".oi-radio[value=stock]").checked };`);
  ok(v.go === "Install the official V15" && v.warn && v.checked, "back to V15: the official file chosen, the button says so, the warning about the data");
}

/* ---- the keyboard: Tab reaches Find, the choices, Install, in order; focus is visible */
await open("/webapp/installer/");
const order = [];
await run(`document.activeElement && document.activeElement.blur(); window.scrollTo(0, 0); return true;`);
for (let i = 0; i < 24; i++) {
  await send("Input.dispatchKeyEvent", { type: "keyDown", key: "Tab", code: "Tab", windowsVirtualKeyCode: 9 });
  await send("Input.dispatchKeyEvent", { type: "keyUp", key: "Tab", code: "Tab", windowsVirtualKeyCode: 9 });
  order.push(await run(`const e = document.activeElement; return e ? (e.className || e.tagName) + "|" + (e.textContent || e.value || "").slice(0, 20) + "|" + getComputedStyle(e).outlineStyle : "";`));
}
const at = (re) => order.findIndex((o) => re.test(o));
ok(at(/^o-btn\|Find the FM-1/) >= 0 && at(/^oi-radio/) > at(/Find the FM-1/) && at(/oi-go/) > at(/^oi-radio/) && order.filter((o) => /oi-go|Find the FM-1/.test(o)).every((o) => !o.endsWith("|none")),
  `keyboard: Tab goes Find -> the choices -> Install, each with a visible focus ring`);

/* ---- the looks: 1440 and 390, dark and light; the phone has no sideways scroll and 16 px gutters */
const colors = [];
for (const [w, dev] of [[1440, "desktop"], [390, "phone"]]) {
  for (const s of ["dark", "light"]) {
    await open("/webapp/installer/", w, s);
    await sleep(200);
    colors.push(await run(`return getComputedStyle(document.body).backgroundColor;`));
    const g = await run(`const m = getComputedStyle(document.querySelector("main")); return { sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth, pl: m.paddingLeft, pr: m.paddingRight };`);
    if (w === 390) ok(g.sw <= g.cw && g.pl === "16px" && g.pr === "16px", `390 px phone, ${s}: no sideways scroll (${g.sw} <= ${g.cw}), 16 px gutters`);
    await full(`installer-${dev}-${s}`, w);
  }
}
ok(colors[0] !== colors[1] && colors[0] === colors[2] && colors[1] === colors[3], `light / dark follow the system (${colors[0]} / ${colors[1]})`);
const nm = await open("/webapp/installer/?nomidi", 390, "light");
const nmv = await run(`${U} return { msg: vis(".oi-browser") && $(".oi-browser").textContent, go: $(".oi-go").disabled, find: !vis(".oi-step .o-btn") };`);
ok(nm.kind === "ok" && /Safari and the browsers on iPhone and iPad do not have Web MIDI/.test(nmv.msg) && nmv.go && nmv.find,
  "no Web MIDI (iPhone, Safari): said plainly in step 1, no Find, Install off, the package still checked");
await full("installer-phone-light-nomidi", 390);
await scheme("dark");
await full("installer-phone-dark-nomidi", 390);

/* ---- the shipped module mounted by a host page: two flavours, the links go to the host */
await open("/webapp/host.html", 1440, "dark");
const host = await run(`${U} const r = { radios: document.querySelectorAll(".oi-choice:not([hidden]) .oi-radio").length, descs: [...document.querySelectorAll(".oi-desc")].map((d) => d.textContent),
    styles: document.querySelectorAll("#optimist-installer-style").length };
  [...document.querySelectorAll(".oi a")].find((a) => a.textContent === "open the builder").click();
  [...document.querySelectorAll(".oi a")].find((a) => a.textContent === "About and licences").click();
  r.navs = window.navs.slice(); r.url = location.pathname; r.busy = window.inst.busy;
  document.querySelector(".oi-radio[value='1']").click(); await until(() => $(".oi-pkg").dataset.kind === "ok" && /Drum machine/.test($(".oi-pkg").textContent), 5000);
  r.second = $(".oi-pkg").textContent; window.inst.unmount(); r.empty = document.getElementById("tab").children.length === 0; return r;`);
ok(host.radios === 3 && host.descs.includes("A second flavour, for the test.") && host.styles === 1, "mounted (webapp/installer/installer.js): two flavours with their descriptions, its style put in once");
ok(JSON.stringify(host.navs) === JSON.stringify(["build", "about"]) && host.url === "/webapp/host.html" && /Drum machine/.test(host.second) && host.empty && !host.busy,
  "mounted: Build and About go to the host (onNavigate), the second flavour loads, unmount() empties it");

/* ---- the site's editor page: its Install and Build tabs load the modules beside it (make_site.py editor_modules) */
const opI = await open("/webapp/editor/#install", 1440, "dark");
const edI = await run(`const until = async (f, ms = 8000) => { const t = Date.now(); while (Date.now() - t < ms) { const v = f(); if (v) return v; await new Promise((r) => setTimeout(r, 100)); } return null; };
  const h = await until(() => document.querySelector("#install-host .oi")); return { host: !!h, note: (document.getElementById("install-note") || {}).textContent || "", radios: document.querySelectorAll("#install-host .oi-radio").length };`);
ok(opI.kind === "ok" && edI.host && edI.radios >= 1, `editor page: the Install tab mounts the installer from ./installer/ with the site's packages (${edI.radios} flavours${edI.note ? ", " + edI.note : ""})`);
const opB = await open("/webapp/editor/#build", 1440, "dark");
const edB = await run(`const until = async (f, ms = 8000) => { const t = Date.now(); while (Date.now() - t < ms) { const v = f(); if (v) return v; await new Promise((r) => setTimeout(r, 100)); } return null; };
  const h = await until(() => document.getElementById("build-host") && document.getElementById("build-host").children.length); return { kids: h || 0, note: (document.getElementById("build-note") || {}).textContent || "" };`);
ok(opB.kind === "ok" && edB.kids > 0 && !/not found|missing|unavailable/i.test(edB.note), `editor page: the Build tab mounts the builder from ./builder/ (${edB.kids} blocks${edB.note ? ", " + edB.note : ""})`);

const errs = events.filter((e) => e.method === "Runtime.exceptionThrown");
ok(!errs.length, `no uncaught error in the pages${errs.length ? ": " + JSON.stringify(errs[0].params).slice(0, 300) : ""}`);
const real = events.filter((e) => e.method === "Page.downloadWillBegin");
ok(!real.length, "no download started");
console.log(failed ? `${failed} FAILED` : "all ok");
closeAll();
process.exit(failed ? 1 : 0);
