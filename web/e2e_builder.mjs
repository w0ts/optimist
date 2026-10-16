// SPDX-License-Identifier: GPL-3.0-only
// The web builder in a real (headless) Chrome with Pyodide: the page's answers (its Python, py-worker.js ->
// tools/builder/web_api.py) against the CLI builder's (tests/builder_web_ref.py and `tools/optimist.py cloud-key --json`)
// for every profile and seeded random edits: identical. Then the page itself: the list, a toggle, the search, the sort,
// the download, light / dark and a 390 px phone with no sideways scroll. docs/BUILDER-WEB.md.
// Not part of `make test` (it needs Chrome and, by default, the network: Pyodide comes from cdn.jsdelivr.net). Run from
// the repo root:
//   node web/e2e_builder.mjs [--shots DIR]
// PYODIDE_DIR=path: a local copy of the Pyodide release instead of the CDN (the same version, logic.js PYODIDE_VERSION;
// e.g. the npm package pyodide@VERSION), served by this test at /pyodide/. CHROME=path overrides the browser.
import { execFileSync, spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";
import { PYODIDE_VERSION } from "./builder/logic.js";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const ROOT = join(HERE, "..");
const shots = process.argv.includes("--shots") ? process.argv[process.argv.indexOf("--shots") + 1] : null;
const CHROMES = [process.env.CHROME, "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/usr/bin/google-chrome",
  "/usr/bin/chromium", "/usr/bin/chromium-browser", "C:/Program Files/Google/Chrome/Application/chrome.exe"].filter(Boolean);
const chrome = CHROMES.find((p) => existsSync(p));
if (!chrome) { console.log("e2e: no Chrome found (CHROME=path)"); process.exit(0); }
const PYTHON = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");
const py = (args, input) => execFileSync(PYTHON, args, { cwd: ROOT, input, maxBuffer: 1 << 28, encoding: "utf8", stdio: ["pipe", "pipe", "pipe"] });
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const sortObj = (v) => (Array.isArray(v) ? v.map(sortObj) : v && typeof v === "object" ? Object.fromEntries(Object.keys(v).sort().map((k) => [k, sortObj(v[k])])) : v);

/* the site's builder, made as make_site.py makes it, with a commit so the page computes a key */
const WORK = join(ROOT, "build", "web-builder-e2e");
rmSync(WORK, { recursive: true, force: true });
const SITE = join(WORK, "site");
py(["web/make_site.py", "--builder", SITE]);
const SHA = "89abcdef0123456789abcdef0123456789abcdef";
writeFileSync(join(SITE, "webapp", "builder", "build-info.json"), JSON.stringify({ commit: SHA }) + "\n");
const PYDIR = process.env.PYODIDE_DIR || null;
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript", ".json": "application/json", ".wasm": "application/wasm",
  ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon", ".zip": "application/zip" };
const server = createServer((q, r) => {
  let url = decodeURIComponent(q.url.split("?")[0]);
  let base = SITE;
  if (PYDIR && url.startsWith("/pyodide/")) { base = PYDIR; url = url.slice("/pyodide".length); }
  if (url.endsWith("/")) url += "index.html";
  const f = normalize(join(base, url));
  if (!f.startsWith(normalize(base)) || !existsSync(f)) { r.writeHead(404); r.end(); return; }
  r.writeHead(200, { "Content-Type": TYPES[extname(f)] || "application/octet-stream" });
  r.end(readFileSync(f));
});
let port = 8800;                                     // (this test's server: the first free port from 8800)
for (;; port++) {
  const up = await new Promise((res) => { server.once("error", () => res(false)); server.listen(port, "127.0.0.1", () => res(true)); });
  if (up) break;
  if (port > 8899) throw new Error("no free port in 8800..8899");
}
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
const pending = new Map();
const events = [];
const send = (method, params = {}, sessionId) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params, sessionId })); });
ws.addEventListener("message", (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); return; }
  if (!m.method) return;
  events.push(m);
  if (m.method === "Target.attachedToTarget") {        /* the Python worker: its requests and its console too */
    const sid = m.params.sessionId;
    send("Network.enable", {}, sid); send("Runtime.enable", {}, sid); send("Runtime.runIfWaitingForDebugger", {}, sid);
  }
});
const run = async (expr) => {
  const m = await send("Runtime.evaluate", { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true });
  if (m.result.exceptionDetails) throw new Error(JSON.stringify(m.result.exceptionDetails).slice(0, 600));
  return m.result.result.value;
};
const shot = async (name) => {
  if (!shots) return;
  mkdirSync(shots, { recursive: true });
  const s = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
  writeFileSync(join(shots, name + ".png"), Buffer.from(s.result.data, "base64"));
};
const U = `const B = window.optimistBuilder; const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const until = async (f, ms = 180000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) return false; await sleep(100); } return true; };
  const $ = (q) => document.querySelector(q);`;
const metrics = (width, height, mobile) => send("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: mobile ? 2 : 1, mobile });
const scheme = (s) => send("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: s }] });

await send("Page.enable");
await send("Network.enable");
await send("Runtime.enable");
await send("Target.setAutoAttach", { autoAttach: true, waitForDebuggerOnStart: false, flatten: true });
if (PYDIR) await send("Page.addScriptToEvaluateOnNewDocument", { source: "window.OPTIMIST_PYODIDE_BASE = '/pyodide/';" });
await metrics(1400, 1000, false);
await scheme("dark");
const START_MS = +(process.env.E2E_START_S || 600) * 1000;   // (a busy machine: Python starts slowly)
const t0 = Date.now();
await send("Page.navigate", { url: `http://127.0.0.1:${port}/webapp/builder/` });
const started = await run(`${U} const W = () => window.optimistBuilder; return (await until(() => (W() && W().readyFlag) || (W() && document.querySelector("#loading").classList.contains("failed")), ${START_MS}) && W().readyFlag) && { stages: W().state.stages, ver: $("#pyver").textContent, rows: document.querySelectorAll(".row").length, loading: $("#loading").hidden };`);
const loadS = ((Date.now() - t0) / 1000).toFixed(1);
if (started) console.log("stages (ms since the page started):", JSON.stringify(started.stages));
ok(started && started.rows > 100 && started.loading, `Pyodide ${PYODIDE_VERSION} from ${PYDIR ? "PYODIDE_DIR" : "the CDN"} started, the list shown (${loadS} s; ${started && started.ver})`);
if (!started) { console.log(events.filter((e) => /console|exception/i.test(e.method)).map((e) => JSON.stringify(e.params).slice(0, 400)).join("\n")); console.log(events.filter((e) => /^Network\.(requestWillBeSent|loadingFinished|loadingFailed|responseReceived)$/.test(e.method)).map((e) => `${e.method} ${e.params.requestId} ${(e.params.request || e.params.response || {}).url || e.params.errorText || ""}`).join("\n")); console.log("loading:", await run(`return document.querySelector("#loadtext").textContent;`)); console.log(events.filter((e) => e.method === "Runtime.exceptionThrown" || e.method === "Runtime.consoleAPICalled").map((e) => JSON.stringify(e.params).slice(0, 300)).join("\n")); process.exit(1); }
const pyUrls = events.filter((e) => e.method === "Network.requestWillBeSent").map((e) => e.params.request.url).filter((u) => /pyodide/.test(u));
const wasm = events.filter((e) => e.method === "Network.responseReceived" && /pyodide\.asm\.wasm$/.test(e.params.response.url)).map((e) => !!(e.params.response.fromDiskCache || e.params.response.fromPrefetchCache || e.params.response.fromServiceWorker));
ok(PYDIR || (wasm.length === 2 && !wasm[0] && wasm[1]), `the wasm is downloaded once: the progress fetch, then Pyodide from the HTTP cache (${JSON.stringify(wasm)})`);
ok(PYDIR || (pyUrls.length && pyUrls.every((u) => u.startsWith(`https://cdn.jsdelivr.net/pyodide/v${PYODIDE_VERSION}/full/`))), `every Pyodide file from the pinned version (${pyUrls.length} requests)`);

/* ---- the page's Python against the CLI */
const prof0 = JSON.parse(readFileSync(join(SITE, "webapp", "builder", "data", "profiles.json"), "utf8"));
const reg = JSON.parse(readFileSync(join(SITE, "webapp", "builder", "data", "registry.json"), "utf8"));
const items = reg.registry.items;
const pageReg = await run(`${U} return await B.call("registry_view");`);
ok(same(pageReg, reg), "the page's registry (Pyodide: registry + backports) = the CLI's export");
let seed = 1016;
const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
const valuesOf = (it) => (it.choices.length ? it.choices.map((c) => c[0]) : [0, 1]);
const texts = prof0.profiles.map((p) => p.text);
const parsed = await run(`${U} return await Promise.all(${JSON.stringify(texts)}.map((t) => B.call("parse_text", t)));`);
const views = [], toggles = [];
for (const [i, p] of prof0.profiles.entries()) {
  const base = parsed[i].values;
  views.push({ values: base, name: p.name, commit: SHA });
  for (let n = 0; n < 4; n++) {
    const v = { ...base };
    for (let k = 1 + rnd(10); k > 0; k--) { const it = items[rnd(items.length)]; const vals = valuesOf(it); v[it.key] = vals[rnd(vals.length)]; }
    views.push({ values: v, name: `${p.name}-r${n}`, commit: SHA });
  }
  toggles.push({ values: base, key: "BLE", value: base.BLE ? 0 : 1, room: null });
}
const pv = await run(`${U} const q = ${JSON.stringify({ views, toggles })};
  const views = []; for (const v of q.views) views.push(Object.assign(await B.call("view", v.values, v.name, v.commit), { fit: await B.call("fit_view", v.values) }));
  const toggles = []; for (const t of q.toggles) toggles.push(await B.call("toggle", t.values, t.key, t.value, t.room));
  return { views, toggles };`);
const cli = JSON.parse(py(["tests/builder_web_ref.py"], JSON.stringify({ views, toggles, texts })));
ok(same(parsed, cli.texts), `${texts.length} profiles parsed alike`);
const bad = [];
for (const [i, q] of views.entries()) {
  for (const f of ["eval", "savings", "conflicts", "over", "fit", "dump"]) if (!same(sortObj(pv.views[i][f]), sortObj(cli.views[i][f]))) bad.push(`${q.name} ${f}`);
}
ok(!bad.length, `${views.length} configurations: evaluate, budget, fits, key, savings, fit = the CLI's${bad.length ? ": " + bad.slice(0, 6).join(", ") : ""}`);
ok(toggles.every((q, i) => same(pv.toggles[i].values, cli.toggles[i].values) && same(pv.toggles[i].room, cli.toggles[i].room)), `${toggles.length} BLE toggles (room.py): the values and the room = the CLIx27s`);
/* the CLI command itself: tools/optimist.py cloud-key FILE --commit SHA --json, on the page's own .config download */
const cfgDir = join(WORK, "configs");
mkdirSync(cfgDir, { recursive: true });
const keyBad = [];
for (const [i, q] of views.entries()) {
  if (i % 3) continue;                              // (a third of them: one CLI process each)
  const f = join(cfgDir, `${i}.config`);
  writeFileSync(f, pv.views[i].dump);
  let ev;
  try { ev = JSON.parse(py(["tools/optimist.py", "cloud-key", f, "--commit", SHA, "--json"])); } catch (e) { ev = JSON.parse(e.stdout); }
  const w = pv.views[i].eval;
  for (const k of ["key", "canonical", "cfg_hash", "errors", "warnings", "notices", "budget", "fits", "fit", "normalized"]) if (!same(sortObj(w[k]), sortObj(ev[k]))) keyBad.push(`${q.name} ${k}`);
}
ok(!keyBad.length, `tools/optimist.py cloud-key --json on the page's .config: the same key, budget, fits, issues${keyBad.length ? ": " + keyBad.join(", ") : ""}`);

/* ---- the page */
const ui = await run(`${U} await B.choosePreset("flavour:default"); await until(() => B.state.view && B.state.name === "user-default");
  const ev = B.state.view.eval;
  return { key: $("#key").textContent, keyIs: ev.key, bars: [...document.querySelectorAll(".bar")].map((b) => b.dataset.region), build: $("#build").disabled,
    note: $("#buildnote").textContent, dl: $("#download").disabled, hash: $("#hash").textContent, msgs: $("#msgs").textContent.slice(0, 200) };`);
ok(ui.key === ui.keyIs && /^[0-9a-f]{64}$/.test(ui.key) && same(ui.bars, ["flash", "ram", "pool", "ramtext"]) && ui.build && /Cloud builds are not live yet/.test(ui.note) && !ui.dl,
  "page: the key shown, four bars (FLASH RAM POOL RAMTEXT), Build disabled with its note, Download on");
const tg = await run(`${U} const before = B.state.view.eval.budget.total.flash; const c = $("#it-ENG_FM6"); const was = c.checked; c.click();
  await until(() => B.state.values.ENG_FM6 === (was ? 0 : 1) && B.state.view.eval.config.ENG_FM6 === (was ? 0 : 1));
  const after = B.state.view.eval.budget.total.flash; const shown = $(".bar[data-region=flash] .num").textContent;
  $("#it-ENG_FM6").click(); await until(() => B.state.view.eval.config.ENG_FM6 === (was ? 1 : 0));
  return { before, after, shown, back: B.state.view.eval.budget.total.flash };`);
ok(tg.before !== tg.after && tg.shown.includes(tg.after.toLocaleString("en-US")) && tg.back === tg.before, `page: ticking FM6 moves the flash estimate (${tg.before} -> ${tg.after}) and back`);
const er = await run(`${U} for (const k of B.state.reg.top["Synth engines"]) B.state.values[k] = 0; await B.setItem("ENG_ANALOG", 0);
  await until(() => B.state.view.eval.errors.length > 0);
  const out = { msgs: [...document.querySelectorAll("#msgs .err")].map((p) => p.textContent), row: ($(".row[data-key=ENG_ANALOG] .xerr") || {}).textContent };
  await B.choosePreset("flavour:default"); await until(() => !B.state.view.eval.errors.length); return out;`);
ok(er.msgs.some((m) => /ERROR at least one synth engine/.test(m)) && /✗ at least one synth engine/.test(er.row || ""), "page: an error in plain words, in the messages and on the item (validate)");
const se = await run(`${U} const s = $("#search"); s.value = "reverb"; s.dispatchEvent(new Event("input")); await sleep(50);
  const shownRows = [...document.querySelectorAll(".row")].map((r) => r.dataset.key);
  s.value = ""; s.dispatchEvent(new Event("input"));
  const so = $("#sort"); so.value = "flash"; so.dispatchEvent(new Event("change")); await sleep(50);
  const g = document.querySelector('.group[data-group="Sample sets"]'); const keys = [...g.querySelectorAll(":scope > ul > li > .row")].map((r) => r.dataset.key);
  const sav = B.state.view.savings; const fl = keys.map((k) => (sav[k] ? sav[k].flash : null));
  so.value = ""; so.dispatchEvent(new Event("change"));
  return { shownRows, fl };`);
const desc = (a) => a.filter((x) => x !== null).every((x, i, l) => i === 0 || l[i - 1] >= x);
ok(se.shownRows.length > 3 && se.shownRows.length < 40 && se.shownRows.includes("FX_REVERB"), `page: the search narrows the list (${se.shownRows.length} rows for "reverb")`);
ok(se.fl.length > 2 && desc(se.fl), "page: sort by flash, largest first within a group (the menu's o)");
const dl = await run(`${U} let got = null; const orig = URL.createObjectURL; URL.createObjectURL = (b) => { got = b; return orig(b); };
  $("#download").click(); await until(() => got, 3000); URL.createObjectURL = orig; return { text: await got.text(), name: document.querySelector("a[download]") ? document.querySelector("a[download]").download : "gone" };`);
const reparsed = JSON.parse(py(["tests/builder_web_ref.py"], JSON.stringify({ texts: [dl.text] }))).texts[0];
ok(reparsed.error === null && same(sortObj(reparsed.values), sortObj(parsed[prof0.profiles.findIndex((p) => p.name === "user-default")].values)), "page: Download .config gives the configuration back to the CLI (parse) unchanged");
const imp = await run(`${U} $("#import").open = true; $("#paste").value = "# name: pasted\\nENG_FM6=0\\nNOT_A_KEY=1\\n"; $("#pasteload").click();
  await until(() => B.state.name === "pasted" && $("#importmsg").textContent); return { msg: $("#importmsg").textContent, fm6: B.state.values.ENG_FM6, name: $("#name").value };`);
ok(imp.fm6 === 0 && imp.name === "pasted" && /NOT_A_KEY is not an item/.test(imp.msg), "page: a pasted .config loads, an unknown key warned as the CLI does");
const inf = await run(`${U} $("#import").open = false; await B.choosePreset("flavour:default"); await until(() => B.state.name === "user-default"); const was = B.state.values.FX_REVERB;
  document.querySelector('.row[data-key="FX_REVERB"] .lab').click(); await until(() => $("#info h3") && $("#info table"), 10000);
  return { h: $("#info h3").textContent, rows: document.querySelectorAll("#info tbody tr").length, same: B.state.values.FX_REVERB === was, preset: $("#preset").value };`);
ok(inf.h === "reverb send bus" && inf.rows >= 2 && inf.same && inf.preset === "flavour:default", `page: a click on an item's text shows its details and costs (${inf.rows} rows), without toggling it`);
const kb = await run(`${U} const s = $("#sort").value; document.body.focus(); document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true })); await sleep(50);
  const v = $("#sort").value; document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true })); document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true }));
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true })); document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true })); await sleep(50);
  return { s, v, back: $("#sort").value };`);
ok(kb.s === "" && kb.v === "flash" && kb.back === "", "page: o cycles the sort as in the menu (default, flash, ram, pool, ramtext)");

/* ---- an overflow: what does not fit, and by how much (shown, never refused) */
const allOn = items.map((it) => `${it.key}=${Math.max(...valuesOf(it))}`).join("\n") + "\n";
const ov = await run(`${U} await B.importText(${JSON.stringify("# name: everything\n" + allOn)}, "all on"); await until(() => B.state.name === "everything");
  window.scrollTo(0, 0); await sleep(200);
  return { over: [...document.querySelectorAll(".bar[data-state=over] .num")].map((e) => e.textContent), msg: [...document.querySelectorAll("#msgs .over")].map((p) => p.textContent),
    hot: document.querySelectorAll(".cost.hot").length, dl: $("#download").disabled };`);
await shot("builder-desktop-dark-overflow");
ok(ov.over.length && ov.over.every((t) => /^OVER by [\d,]+ B$/.test(t)) && ov.msg.some((m) => /overflows by [\d,]+ B\. Biggest/.test(m)) && ov.hot > 0 && !ov.dl,
  `page: everything on: ${ov.over.length} regions OVER by N B, the biggest items named, red costs; Download still on`);
await run(`${U} await B.choosePreset("flavour:default"); await until(() => B.state.name === "user-default"); document.querySelector('.row[data-key="FX_REVERB"] .lab').click(); await sleep(300); return true;`);

/* ---- the looks: dark and light, desktop and a 390 px phone */
const scrollW = () => run(`return { sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth };`);
const colors = () => run(`return { bg: getComputedStyle(document.body).backgroundColor, fg: getComputedStyle(document.body).color };`);
await run(`window.scrollTo(0, 0); return true;`);
await shot("builder-desktop-dark");
const dark = await colors();
await scheme("light");
await sleep(200);
const light = await colors();
await shot("builder-desktop-light");
ok(dark.bg === "rgb(0, 0, 0)" && light.bg !== dark.bg && light.fg !== dark.fg, `light / dark follow the system (${dark.bg} / ${light.bg})`);
await run(`${U} const r = document.querySelector('.row[data-key="FX_REVERB"]'); r.scrollIntoView({ block: "center" }); await sleep(200); return true;`);
await shot("builder-desktop-light-info");
await metrics(390, 844, true);
await sleep(400);
const ph = await scrollW();
ok(ph.sw <= ph.cw, `390 px phone: no sideways scroll (${ph.sw} <= ${ph.cw})`);
const gut = await run(`const r = document.querySelector("main").getBoundingClientRect(); const p = getComputedStyle(document.querySelector("main")); return { pl: p.paddingLeft, pr: p.paddingRight, w: r.width };`);
ok(gut.pl === "16px" && gut.pr === "16px", "390 px phone: 16 px gutters");
await run(`window.scrollTo(0, 0); return true;`);
await shot("builder-phone-light");
await run(`${U} document.querySelector('.row[data-key="FX_REVERB"]').scrollIntoView({ block: "start" }); await sleep(200); return true;`);
await shot("builder-phone-light-list");
await scheme("dark");
await sleep(200);
await run(`window.scrollTo(0, 0); return true;`);
await shot("builder-phone-dark");
const ph2 = await scrollW();
ok(ph2.sw <= ph2.cw, `390 px phone, dark: no sideways scroll (${ph2.sw} <= ${ph2.cw})`);
const errs = events.filter((e) => e.method === "Runtime.exceptionThrown");
ok(!errs.length, `no uncaught error in the page${errs.length ? ": " + JSON.stringify(errs[0].params).slice(0, 300) : ""}`);

console.log(failed ? `${failed} FAILED` : "all ok");
closeAll();
process.exit(failed ? 1 : 0);
