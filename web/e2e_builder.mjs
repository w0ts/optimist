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
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { createServer } from "node:http";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";
import { PYODIDE_VERSION } from "./builder/logic.js";

const HERE = fileURLToPath(new URL(".", import.meta.url));

/* downloads never reach ~/Downloads: Chrome saves them under the repo's work area, and the run asserts ~/Downloads got nothing new */
const DL_HOME = join(homedir(), "Downloads"), DL_DIR = join(homedir(), "GitHub", "optimist-work", "e2e-downloads", String(process.pid));
const dlList = () => { try { return readdirSync(DL_HOME); } catch (e) { return []; } };
const dlBefore = new Set(dlList());
mkdirSync(DL_DIR, { recursive: true });
const dlCheck = () => {
  const fresh = dlList().filter((f) => !dlBefore.has(f) && /\.(json|fwsc|config|syx|wav|zip|mid|txt|bin|cfg|fw|sf2|pkg|csv|ota|optimist-backup|optimist-snap)$/i.test(f));   /* (the types the editor and builder export; the user's own files are not ours) */
  const mine = readdirSync(DL_DIR);
  rmSync(DL_DIR, { recursive: true, force: true });
  return { fresh, mine };
};
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
/* STANDIN_DEFAULT=FILE.config: the test site (only it: build/) gets that file as profile "synth" and a flavour "Synth"
   marked default, as the manifest will have it once config/profiles/synth.config lands; the page must open with it */
const STANDIN = process.env.STANDIN_DEFAULT || null;
if (STANDIN) {
  const pf = join(SITE, "webapp", "builder", "data", "profiles.json");
  const pj = JSON.parse(readFileSync(pf, "utf8"));
  if (!pj.profiles.some((p) => p.name === "synth")) pj.profiles.push({ name: "synth", text: readFileSync(STANDIN, "utf8"), published: true });
  pj.flavours = [{ id: "synth", title: "Synth", description: "stand-in: " + STANDIN, profile: "synth", default: true },
    ...pj.flavours.filter((f) => f.id !== "synth").map((f) => ({ ...f, default: false }))];
  writeFileSync(pf, JSON.stringify(pj, null, 1) + "\n");
}
const PYDIR = process.env.PYODIDE_DIR || null;
const TYPES = { ".html": "text/html", ".css": "text/css", ".ttf": "font/ttf", ".js": "text/javascript", ".mjs": "text/javascript", ".json": "application/json", ".wasm": "application/wasm",
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
const PORT0 = +(process.env.E2E_PORT || 8800);
let port = PORT0;                                    // (this test's server: the first free port from E2E_PORT, default 8800)
for (;; port++) {
  const up = await new Promise((res) => { server.once("error", () => res(false)); server.listen(port, "127.0.0.1", () => res(true)); });
  if (up) break;
  if (port > PORT0 + 99) throw new Error(`no free port in ${PORT0}..${PORT0 + 99}`);
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
  if (process.env.E2E_TRACE) console.log("  run:", (expr.startsWith(U) ? expr.slice(U.length) : expr).replace(/\s+/g, " ").slice(0, 100));
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
await send("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: DL_DIR });
await send("Network.enable");
await send("Runtime.enable");
await send("Target.setAutoAttach", { autoAttach: true, waitForDebuggerOnStart: false, flatten: true });
if (PYDIR) await send("Page.addScriptToEvaluateOnNewDocument", { source: "window.OPTIMIST_PYODIDE_BASE = '/pyodide/';" });
await metrics(1440, 1000, false);
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
const DFLT = (prof0.flavours.find((f) => f.default === true) || prof0.flavours[0]).profile;   // (the page opens on the default flavour's profile: Synth since 2026-10-16)
const reg = JSON.parse(readFileSync(join(SITE, "webapp", "builder", "data", "registry.json"), "utf8"));
const items = reg.registry.items;
const pageReg = await run(`${U} return await B.call("registry_view");`);
/* the page opens with the manifest's default flavour (marked "default", else the first), and it validates */
{
  const dflt = prof0.flavours.find((f) => f.default === true) || prof0.flavours[0];
  const text = prof0.profiles.find((p) => p.name === dflt.profile).text;
  const init = await run(`${U} const p = await B.call("parse_text", ${JSON.stringify(text)});
    return { same: JSON.stringify(Object.entries(p.values).sort()) === JSON.stringify(Object.entries(B.state.values).sort()), preset: $("#bpreset").value,
      errors: B.state.view.eval.errors.map((e) => e.text), reset: $("#reset").textContent };`);
  ok(init.same && init.preset === `flavour:${dflt.id}` && !init.errors.length && init.reset.includes(dflt.title),
    `page: opens with the manifest's default flavour ${dflt.title} (${dflt.profile}), no errors${init.errors.length ? ": " + init.errors.join("; ") : ""}`);
  const rs = await run(`${U} const before = JSON.stringify(B.state.values); await B.setItem("ENG_FM6", B.state.values.ENG_FM6 ? 0 : 1);
    const changed = JSON.stringify(B.state.values) !== before; $("#reset").click(); await until(() => JSON.stringify(B.state.values) === before, 20000);
    return { changed, back: JSON.stringify(B.state.values) === before, preset: $("#bpreset").value };`);
  ok(rs.changed && rs.back && rs.preset === `flavour:${dflt.id}`, "page: Reset to default restores it");
}
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
  const views = []; for (const v of q.views) views.push(Object.assign(await B.call("view", v.values, v.name, v.commit), { fit: await B.call("fit_view", v.values), proposals: await B.call("proposals_view", v.values) }));
  const toggles = []; for (const t of q.toggles) toggles.push(await B.call("toggle", t.values, t.key, t.value, t.room));
  return { views, toggles };`);
const cli = JSON.parse(py(["tests/builder_web_ref.py"], JSON.stringify({ views, toggles, texts })));
ok(same(parsed, cli.texts), `${texts.length} profiles parsed alike`);
const bad = [];
for (const [i, q] of views.entries()) {
  for (const f of ["eval", "savings", "conflicts", "over", "fit", "proposals", "dump"]) if (!same(sortObj(pv.views[i][f]), sortObj(cli.views[i][f]))) bad.push(`${q.name} ${f}`);
}
ok(!bad.length, `${views.length} configurations: evaluate, budget, fits, key, savings, fit, proposals = the CLI's${bad.length ? ": " + bad.slice(0, 6).join(", ") : ""}`);
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
const ui = await run(`${U} await B.choosePreset("flavour:default"); await until(() => B.state.view && B.state.name.toLowerCase() === "${DFLT}".toLowerCase());
  const ev = B.state.view.eval;
  return { key: $("#key").textContent, keyIs: ev.key, bars: [...document.querySelectorAll("#gauges .o-gauge")].map((b) => b.dataset.region), build: $("#build").disabled,
    note: $("#buildnote").textContent, dl: $("#download").disabled, hash: $("#hash").textContent, msgs: $("#msgs").textContent.slice(0, 200) };`);
ok(ui.key === ui.keyIs && /^[0-9a-f]{64}$/.test(ui.key) && same(ui.bars, ["flash", "ram", "pool", "ramtext"]) && ui.build && /Cloud builds are not live yet/.test(ui.note) && !ui.dl,
  "page: the key shown, four gauges (FLASH RAM POOL RAMTEXT), Build disabled with its note, Download on");
const tg = await run(`${U} const before = B.state.view.eval.budget.total.flash; const c = $("#it-ENG_FM6"); const was = c.checked; c.click();
  await until(() => B.state.values.ENG_FM6 === (was ? 0 : 1) && B.state.view.eval.config.ENG_FM6 === (was ? 0 : 1));
  const after = B.state.view.eval.budget.total.flash; const shown = $(".o-gauge[data-region=flash]").title;
  $("#it-ENG_FM6").click(); await until(() => B.state.view.eval.config.ENG_FM6 === (was ? 1 : 0));
  return { before, after, shown, back: B.state.view.eval.budget.total.flash };`);
ok(tg.before !== tg.after && tg.shown.includes(tg.after.toLocaleString("en-US")) && tg.back === tg.before, `page: ticking FM6 moves the flash estimate (${tg.before} -> ${tg.after}) and back`);
const er = await run(`${U} for (const k of B.state.reg.top["Synth engines"]) B.state.values[k] = 0; await B.setItem("ENG_ANALOG", 0);
  await until(() => B.state.view.eval.errors.length > 0);
  const out = { msgs: [...document.querySelectorAll("#msgs .o-msg.err[data-kind=rule]")].map((p) => p.textContent), row: ($(".row[data-key=ENG_ANALOG] .xerr") || {}).textContent };
  await B.choosePreset("flavour:default"); await until(() => !B.state.view.eval.errors.length); return out;`);
ok(er.msgs.some((m) => /^Errorat least one synth engine/.test(m)) && /✗ at least one synth engine/.test(er.row || ""), "page: an error in plain words, in the messages and on the item (validate)");
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
const dlEarly = events.filter((e) => e.method === "Page.downloadWillBegin").length;
const dl = await run(`${U} let got = null; const orig = URL.createObjectURL; URL.createObjectURL = (b) => { got = b; return orig(b); };
  $("#download").click(); await until(() => got, 3000); URL.createObjectURL = orig; return { text: await got.text(), name: document.querySelector("a[download]") ? document.querySelector("a[download]").download : "gone" };`);
await sleep(1500);
const dlFiles = readdirSync(DL);
ok(dlEarly === 0 && events.filter((e) => e.method === "Page.downloadWillBegin").length === 1 && dlFiles.length === 1 && /\.config$/.test(dlFiles[0]),
  `downloads: none before the click (load, presets, edits); the click makes exactly one, ${dlFiles[0] || "-"}, into the test's own folder`);
const reparsed = JSON.parse(py(["tests/builder_web_ref.py"], JSON.stringify({ texts: [dl.text] }))).texts[0];
ok(reparsed.error === null && same(sortObj(reparsed.values), sortObj(parsed[prof0.profiles.findIndex((p) => p.name === DFLT)].values)), "page: Download .config gives the configuration back to the CLI (parse) unchanged");
const imp = await run(`${U} $("#import").open = true; $("#paste").value = "# name: pasted\\nENG_FM6=0\\nNOT_A_KEY=1\\n"; $("#pasteload").click();
  await until(() => B.state.name === "pasted" && $("#importmsg").textContent); return { msg: $("#importmsg").textContent, fm6: B.state.values.ENG_FM6, name: $("#name").value };`);
ok(imp.fm6 === 0 && imp.name === "pasted" && /NOT_A_KEY is not an item/.test(imp.msg), "page: a pasted .config loads, an unknown key warned as the CLI does");
const inf = await run(`${U} $("#import").open = false; await B.choosePreset("flavour:default"); await until(() => B.state.name.toLowerCase() === "${DFLT}".toLowerCase()); const was = B.state.values.FX_REVERB;
  document.querySelector('.row[data-key="FX_REVERB"] .lab').click(); await until(() => $("#info h3") && $("#info table"), 10000);
  return { h: $("#info h3").textContent, rows: document.querySelectorAll("#info tbody tr").length, same: B.state.values.FX_REVERB === was, preset: $("#bpreset").value };`);
ok(inf.h === (reg.registry.items || reg.registry).find((it) => it.key === "FX_REVERB").label && inf.rows >= 2 && inf.same && inf.preset === "flavour:default", `page: a click on an item's text shows its details and costs (${inf.rows} rows), without toggling it`);
const kb = await run(`${U} const s = $("#sort").value; document.body.focus(); document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true })); await sleep(50);
  const v = $("#sort").value; document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true })); document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true }));
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true })); document.dispatchEvent(new KeyboardEvent("keydown", { key: "o", bubbles: true })); await sleep(50);
  return { s, v, back: $("#sort").value };`);
ok(kb.s === "" && kb.v === "flash" && kb.back === "", "page: o cycles the sort as in the menu (default, flash, ram, pool, ramtext)");

/* ---- the notices policy: on the page only what must be acted on; the NOTICE texts in the (i) and the closed notes */
const np = await run(`${U} await B.choosePreset("flavour:default"); await until(() => B.state.name.toLowerCase() === "${DFLT}".toLowerCase()); await sleep(100);
  const v = B.state.view, ms = [...document.querySelectorAll("#msgs .o-msg")];
  const lab = (n) => { const k = (n.keys || [])[0], it = k && B.state.reg.byKey[k]; return it && n.text.startsWith(it.label + ": ") ? n.text.slice(it.label.length + 2) : n.text; };
  return { kinds: ms.map((p) => p.dataset.kind), text: $("#msgs").textContent, open: $("#notes").open, hidden: $("#notes").hidden, sum: $("#notessum").textContent,
    notes: $("#noteslist").textContent, notices: v.eval.notices.map(lab), expOn: v.built.filter((k) => B.state.reg.byKey[k].experimental),
    expLines: [...document.querySelectorAll('#msgs [data-kind="experimental"]')].map((p) => p.dataset.key), red: document.querySelectorAll("#msgs .o-msg.err").length,
    tags: [...document.querySelectorAll(".row .tags")].filter((t) => /NOTICE/.test(t.textContent)).length,
    warnColour: [...document.querySelectorAll(".row .o-tag.warn")].every((t) => B.state.view.built.includes(t.closest(".row").dataset.key)) };`);
ok(np.red === 0 && !/NOTICE/i.test(np.text) && np.kinds.every((k) => k === "experimental") && np.tags === 0,
  `notices, the default flavour: no red line, no NOTICE on the page or in the list (${np.kinds.length} experimental line${np.kinds.length === 1 ? "" : "s"})`);
ok(np.expOn.every((k) => np.expLines.includes(k)) && new Set(np.expLines).size === np.expLines.length && np.warnColour,
  `notices: one line per experimental item that is on (${np.expOn.join(", ") || "none"}), amber only on the items that are on`);
ok(np.notices.length === 0 ? np.hidden : (!np.open && !np.hidden && np.notices.every((t) => np.notes.includes(t)) && /^Notes \(\d+\)$/.test(np.sum)),
  `notices: the ${np.notices.length} NOTICE texts in "${np.sum}", closed by default`);
const pop = await run(`${U} const it = B.state.reg.items.find((x) => x.notice && document.querySelector('.row[data-key="' + x.key + '"] .o-info'));
  const b = document.querySelector('.row[data-key="' + it.key + '"] .o-info'); b.scrollIntoView({ block: "center" }); b.click(); await sleep(150);
  const p = $("#bpop"), r = p.getBoundingClientRect(); const out = { key: it.key, open: p.matches(":popover-open"), text: p.textContent, notice: it.notice, inView: r.top >= 0 && r.bottom <= innerHeight && r.left >= 0 && r.right <= innerWidth };
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true })); p.hidePopover(); return out;`);
ok(pop.open && pop.text.includes(pop.notice) && pop.inView, `notices: an item's (i) opens its notes in a popover, in view (${pop.key})`);
const ble = await run(`${U} const was = B.state.values.BLE; await B.setItem("BLE", was ? 0 : 1); await sleep(100);
  const note = B.state.room && B.state.room.note, line = $('.row[data-key="BLE"] .bleline');
  const r = { note, line: line ? line.textContent : null, inMsgs: !!note && $("#msgs").textContent.includes(note) };
  await B.choosePreset("flavour:default"); await until(() => B.state.name.toLowerCase() === "${DFLT}".toLowerCase() && !B.state.room); return r;`);
ok(ble.note ? ble.line === ble.note && !ble.inMsgs : ble.line === null, `BLE: its room note on the Bluetooth row only${ble.note ? ` ("${ble.note.slice(0, 50)}...")` : " (none: it fits)"}`);


const scrollW = () => run(`return { sw: document.documentElement.scrollWidth, cw: document.documentElement.clientWidth };`);
const colors = () => run(`return { bg: getComputedStyle(document.body).backgroundColor, fg: getComputedStyle(document.body).color };`);
const top0 = () => run(`window.scrollTo(0, 0); await new Promise((r) => setTimeout(r, 150)); return true;`);
const looks = async (name) => {
  for (const [w, h, mobile, dev] of [[1440, 1000, false, "desktop"], [390, 844, true, "phone"]]) {
    await metrics(w, h, mobile);
    for (const s of ["dark", "light"]) { await scheme(s); await sleep(250); await top0(); await shot(`builder-${dev}-${s}-${name}`); }
  }
  await metrics(1440, 1000, false);
  await scheme("dark");
};

/* ---- Make it fit: only when the configuration does not fit; then up to 4 proposals, each Accept / Reject / Preview */
const nofit = await run(`${U} const f = $("#fit"); return { shown: getComputedStyle(f).display !== "none", focusable: f.offsetParent !== null, props: getComputedStyle($("#proposals")).display !== "none",
  over: Object.keys(B.state.view.over).length };`);
ok(!nofit.shown && !nofit.focusable && !nofit.props && nofit.over === 0, "Make it fit: the default Synth fits: no button (display none, not focusable), no proposals panel");
const fp = await run(`${U} const before = JSON.stringify(B.state.values);
  for (const k of ["ENG_PHYS", "ENG_GRAIN", "FX_REVERB", "DRUM_SAMPLED"]) await B.setItem(k, 1);
  const over0 = Object.keys(B.state.view.over).length; const shown = getComputedStyle($("#fit")).display !== "none"; const startVals = JSON.stringify(B.state.values);
  $("#fit").click(); await until(() => document.querySelectorAll("#proposals .prop").length > 0, 60000);
  const cards = [...document.querySelectorAll("#proposals .prop")];
  const info = cards.map((c) => ({ id: c.dataset.id, fits: c.dataset.fits, title: c.querySelector("h3").textContent, changes: c.querySelectorAll("li").length, res: c.querySelectorAll(".res > span").length,
    btns: [...c.querySelectorAll(".acts button")].map((b) => b.dataset.act) }));
  const fitting = cards.find((c) => c.dataset.fits === "true");
  const gaugesBefore = [...document.querySelectorAll("#gauges .o-gauge")].map((g) => g.dataset.state).join();
  fitting.dispatchEvent(new MouseEvent("mouseenter")); await sleep(100);
  const prev = { tag: !$("#previewtag").hidden, states: [...document.querySelectorAll("#gauges .o-gauge")].map((g) => g.dataset.state).join() };
  fitting.dispatchEvent(new MouseEvent("mouseleave")); await sleep(100);
  const back = [...document.querySelectorAll("#gauges .o-gauge")].map((g) => g.dataset.state).join();
  const rej = cards.find((c) => c !== fitting); const rid = rej && rej.dataset.id;
  if (rej) rej.querySelector('[data-act=reject]').click(); await sleep(100);
  const afterReject = document.querySelectorAll("#proposals .prop").length;
  const r = $("#result").getBoundingClientRect(), g = $("#proposals").getBoundingClientRect();
  return { before, over0, shown, startVals, info, gaugesBefore, prev, back, rid, afterReject, n: cards.length, vals: JSON.stringify(B.state.values), rejGone: !document.querySelector('.prop[data-id="' + rid + '"]') };`);
ok(fp.over0 > 0 && fp.shown, "Make it fit: Synth + PHYS + GRAIN + reverb + sampled kit does not fit: the button is there");
ok(fp.n >= 1 && fp.n <= 4 && fp.info.every((c) => c.title && c.changes >= 1 && c.res === 4 && same(c.btns, ["accept", "preview", "reject"])) && fp.info.some((c) => c.fits === "true"),
  `Make it fit: ${fp.n} proposals (${fp.info.map((c) => c.id).join(", ")}), each with a title, its changes, the four results and Accept / Preview / Reject`);
ok(fp.prev.tag && !/over/.test(fp.prev.states) && /over/.test(fp.gaugesBefore) && fp.back === fp.gaugesBefore && fp.vals === fp.startVals, "Make it fit: the preview shows the result on the gauges (none over) and back, without applying");
ok(fp.rid ? fp.rejGone && fp.afterReject === fp.n - 1 : true, "Make it fit: Reject removes that card");
await looks("proposals");
const acc = await run(`${U} const c = [...document.querySelectorAll("#proposals .prop")].find((x) => x.dataset.fits === "true"); const start = ${JSON.stringify(fp.startVals)};
  c.querySelector("[data-act=accept]").click(); await until(() => JSON.stringify(B.state.values) !== start && !Object.keys(B.state.view.over).length, 60000);
  await sleep(200);
  const out = { over: Object.keys(B.state.view.over).length, fitHidden: getComputedStyle($("#fit")).display === "none", propsHidden: getComputedStyle($("#proposals")).display === "none", undo: !!$('.o-toast [data-act=undo]') };
  $('.o-toast [data-act=undo]').click(); await until(() => JSON.stringify(B.state.values) === start, 60000); await sleep(200);
  out.restored = JSON.stringify(B.state.values) === start; out.overAgain = Object.keys(B.state.view.over).length > 0; out.fitAgain = getComputedStyle($("#fit")).display !== "none";
  return out;`);
ok(acc.over === 0 && acc.fitHidden && acc.propsHidden && acc.undo, "Make it fit: Accept applies the changes, the gauges fit, the button and the panel are gone");
ok(acc.restored && acc.overAgain && acc.fitAgain, "Make it fit: Undo (on the toast) restores the configuration and the button");
await run(`${U} await B.choosePreset("flavour:default"); await until(() => B.state.name.toLowerCase() === "${DFLT}".toLowerCase() && !B.state.room); return true;`);

/* ---- the item tree: a section opens and closes by its head (no fold button), kept by this browser */
const fold = await run(`${U} const g = document.querySelector('.group[data-group="Synth engines"]'); const h = g.querySelector(".o-panel-h"); const ul = () => g.querySelector(":scope > ul");
  const words = [...document.querySelectorAll("#tree button")].filter((b) => /fold/i.test(b.textContent)).length;
  const open0 = h.getAttribute("aria-expanded"); h.click(); const closed = document.querySelector('.group[data-group="Synth engines"] .o-panel-h');
  const out = { words, open0, a1: closed.getAttribute("aria-expanded"), hidden: document.querySelector('.group[data-group="Synth engines"] > ul').hidden, chev: closed.querySelector(".chev").textContent,
    saved: localStorage.getItem("optimist-builder-folds"), role: closed.getAttribute("role"), tab: closed.tabIndex };
  closed.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true })); const reopened = document.querySelector('.group[data-group="Synth engines"] .o-panel-h');
  out.a2 = reopened.getAttribute("aria-expanded"); out.chev2 = reopened.querySelector(".chev").textContent; out.focusKept = document.activeElement === reopened || true;
  const par = document.querySelector(".row.parent"); out.parent = par ? { chev: par.querySelector(".fold").textContent, exp: par.querySelector(".fold").getAttribute("aria-expanded") } : null;
  if (par) { const k = par.dataset.key; par.click(); await sleep(50); out.parentFolded = document.querySelector('.row[data-key="' + k + '"] .fold').getAttribute("aria-expanded"); document.querySelector('.row[data-key="' + k + '"] .fold').click(); }
  out.expandIcons = [$("#expand").textContent, $("#collapse").textContent];
  return out;`);
ok(fold.words === 0 && fold.open0 === "true" && fold.a1 === "false" && fold.hidden && fold.chev === "▸" && fold.a2 === "true" && fold.chev2 === "▾" && fold.role === "button" && fold.tab === 0,
  "tree: no fold button; the section head (chevron, Enter / click, aria-expanded) closes and opens the section");
ok(/Synth engines/.test(fold.saved || "") && fold.parent && fold.parent.chev === "▾" && fold.parentFolded === "false" && !/fold|expand|collapse/i.test(fold.expandIcons.join("")),
  "tree: the fold is remembered in localStorage; a parent row's click closes its options; expand / collapse all are icons, no words");

/* ---- the layout: on a desktop the right column holds the cards, the gauges card above the info card, beside the list; on a
   phone they stack above the list with the gauges card sticky at the top */
const sticky = (what, test, maxH) => run(`${U} window.scrollTo(0, 2500); await sleep(200); const g = $("#gauges").getBoundingClientRect();
  const hit = document.elementFromPoint(Math.min(g.left + 40, innerWidth - 1), g.top + 8);
  return { y: scrollY, top: g.top, h: g.height, inside: !!(hit && hit.closest("#gauges")), n: document.querySelectorAll("#gauges .o-gauge").length,
    fit: $("#fit").getBoundingClientRect().bottom <= g.bottom, key: $("#keychip").getBoundingClientRect().bottom <= g.bottom };`).then((s) => {
  ok(s.y > 1000 && test(s.top) && s.inside && s.n === 4 && s.fit && s.key && s.h < maxH, `${what}: the gauges card, Make it fit and the key stay in view while scrolling (${Math.round(s.h)} px high, at ${Math.round(s.top)})`);
});
const boxes = () => run(`const r = (s) => { const b = document.querySelector(s).getBoundingClientRect(); return { l: b.left, r: b.right, t: b.top, b: b.bottom }; };
  window.scrollTo(0, 0); await new Promise((q) => setTimeout(q, 150));
  return { tree: r("#tree"), gauges: r("#gauges"), info: r("#info"), top: scrollY };`);
await metrics(1440, 1000, false);
const bx = await boxes();
ok(bx.gauges.l >= bx.tree.r - 1 && bx.info.l >= bx.tree.r - 1 && bx.gauges.b <= bx.info.t + 1 && Math.abs(bx.gauges.l - bx.info.l) < 2,
  "1440 px: the gauges card is right of the list and above the info card (same column)");
const follow = await run(`${U} const rows = [...document.querySelectorAll(".row[data-key]")]; const out = [];
  for (const k of ["FX_REVERB", "ENG_FM6"]) { const r = document.querySelector('.row[data-key="' + k + '"] .lab'); if (!r) continue; r.click();
    await until(() => $("#info h3") && $("#info h3").textContent === B.state.reg.byKey[k].label, 10000); out.push($("#info h3").textContent); }
  const ib = $("#info").getBoundingClientRect(), gb = $("#gauges").getBoundingClientRect(), tb = $("#tree").getBoundingClientRect();
  return { out, right: ib.left >= tb.right - 1 && ib.top >= gb.bottom - 1 };`);
ok(follow.out.length === 2 && follow.out[0] !== follow.out[1] && follow.right, `1440 px: the info card follows the selection (${follow.out.join(" > ")}) and stays under the gauges card`);
await sticky("desktop", (top) => top >= 0 && top <= 60, 460);
const stk = await run(`${U} const i = $("#info").getBoundingClientRect(), c = $("#result").getBoundingClientRect(), g = $("#gauges").getBoundingClientRect();
  return { info: i.top < innerHeight - 40, order: g.bottom <= c.top + 1 && c.bottom <= i.top + 1, cfgSeen: c.bottom <= innerHeight, dl: $("#download").getBoundingClientRect().bottom <= innerHeight, sy: scrollY };`);
ok(stk.info && stk.dl && stk.cfgSeen, "1440 px: the info card and Your configuration (Download .config) stay on the screen while the list scrolls");
ok(stk.order, "1440 px: Your configuration is right under the gauges card, above the info card (no scrolling to the bottom)");

/* ---- an overflow: what does not fit, and by how much (shown, never refused) */
const allOn = items.map((it) => `${it.key}=${Math.max(...valuesOf(it))}`).join("\n") + "\n";
const ov = await run(`${U} await B.importText(${JSON.stringify("# name: everything\n" + allOn)}, "all on"); await until(() => B.state.name === "everything");
  window.scrollTo(0, 0); await sleep(200);
  return { over: [...document.querySelectorAll(".o-gauge[data-state=over] .go")].map((e) => e.textContent), msg: [...document.querySelectorAll('#msgs [data-kind="over"]')].map((p) => p.textContent),
    hot: document.querySelectorAll(".cost.hot").length, dl: $("#download").disabled, regions: Object.keys(B.state.view.over).length, red: document.querySelectorAll("#msgs .o-msg.err").length,
    rules: B.state.view.eval.errors.length + (B.state.view.undo && B.state.view.undo.short ? 1 : 0), fitPrimary: getComputedStyle($("#fit")).display !== "none" };`);
ok(ov.over.length === ov.regions && ov.over.every((t) => /^\+[\d,]+\.\dK$/.test(t)) && ov.msg.length === 1 && /^Over budget/.test(ov.msg[0]) && /Biggest .* items: /.test(ov.msg[0])
  && /◀ off = fits/.test(ov.msg[0]) && ov.hot > 0 && !ov.dl && ov.fitPrimary,
  `page: everything on: ${ov.over.length} gauges +N K over, ONE overflow line (the biggest items, the legend), red costs; Make it fit appears; Download still on`);
ok(ov.red === 1 + ov.rules, `page: the red lines are the overflow and the broken rules only (${ov.red} = 1 + ${ov.rules})`);

/* ---- the looks: dark and light, desktop (1440) and a 390 px phone, over budget and fitting */
await looks("over");
await run(`${U} await B.choosePreset("flavour:default"); await until(() => B.state.name.toLowerCase() === "${DFLT}".toLowerCase()); document.querySelector('.row[data-key="FX_REVERB"] .lab').click(); await sleep(300); return true;`);
await looks("fits");
await top0();
const dark = await colors();
await scheme("light");
await sleep(200);
const light = await colors();
ok(dark.bg === "rgb(0, 0, 0)" && light.bg === "rgb(244, 244, 242)" && light.fg !== dark.fg, `light / dark follow the system (${dark.bg} / ${light.bg})`);
await run(`${U} const r = document.querySelector('.row[data-key="FX_REVERB"]'); r.scrollIntoView({ block: "center" }); await sleep(200); return true;`);
await shot("builder-desktop-light-scrolled");
await metrics(390, 844, true);
await sleep(400);
const ph = await scrollW();
ok(ph.sw <= ph.cw, `390 px phone: no sideways scroll (${ph.sw} <= ${ph.cw})`);
const gut = await run(`const r = document.querySelector("main").getBoundingClientRect(); const p = getComputedStyle(document.querySelector("main")); return { pl: p.paddingLeft, pr: p.paddingRight, w: r.width };`);
ok(gut.pl === "16px" && gut.pr === "16px", "390 px phone: 16 px gutters");
await sticky("390 px phone", (top) => top === 0, 190);
const pb = await boxes();
ok(pb.gauges.t < pb.info.t && pb.info.b <= pb.tree.t + 1, "390 px: the cards stack above the list, the gauges card first");
await run(`${U} document.querySelector('.row[data-key="FX_REVERB"]').scrollIntoView({ block: "center" }); await sleep(200); return true;`);
await shot("builder-phone-light-scrolled");
await scheme("dark");
await sleep(200);
await top0();
const ph2 = await scrollW();
ok(ph2.sw <= ph2.cw, `390 px phone, dark: no sideways scroll (${ph2.sw} <= ${ph2.cw})`);
const errs = events.filter((e) => e.method === "Runtime.exceptionThrown");
ok(!errs.length, `no uncaught error in the page${errs.length ? ": " + JSON.stringify(errs[0].params).slice(0, 300) : ""}`);

{ const d = dlCheck(); ok(d.fresh.length === 0, `no file reached ~/Downloads during the run${d.fresh.length ? " (new: " + d.fresh.join(", ") + ")" : ""}`); }
console.log(failed ? `${failed} FAILED` : "all ok");
closeAll();
process.exit(failed ? 1 : 0);
