// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Henri David
// The editor in a real (headless) Chrome with its "fm1-base emulator" device: fm1-base's browser emulator running the
// trial (Optimist as an fm1-base app) in one tab, its "port" part (fm1.toml emu = ["port"]) giving it to the editor in
// another tab of the same site, as port_client.js's emuAccess(). The page connects, asks the capability query first,
// reads INFO (opti_cpfiles_1) and lists Optimist's backup through the companion's files on its Backup page, shows the
// companion's settings and files there, sets BPM from its slider (read back from the FM-1), and saves a backup file.
// Not part of `make test` (it needs Chrome and the trial's emulator, fm1-base tools/build.py optimist --web):
//   node web/e2e_fm1emu.mjs EMU_DIR      EMU_DIR: the trial's built emulator (its index.html, app.wasm, parts/)
//   node web/e2e_fm1emu.mjs EMU_DIR --generic   another fm1-base firmware's (fm1-base tests/companion_app, FM1B_EMU=port):
//                                        the Backup page with its companion services alone, a setting from the page
// CHROME=path overrides the browser. Downloads go to EMU_DIR/../e2e-downloads (deleted after).
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync } from "node:fs";
import { createServer } from "node:http";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const EMU = resolve(process.argv[2] || "");
if (!existsSync(join(EMU, "app.wasm")) || !existsSync(join(EMU, "parts", "port_client.js"))) {
  console.log(`e2e_fm1emu: no emulator with its port part at ${EMU} (fm1-base: tools/build.py optimist --web)`);
  process.exit(1);
}
const CHROMES = [process.env.CHROME, "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/usr/bin/google-chrome",
  "/usr/bin/chromium", "/usr/bin/chromium-browser"].filter(Boolean);
const chrome = CHROMES.find((p) => existsSync(p));
if (!chrome) { console.log("e2e_fm1emu: no Chrome found (CHROME=path)"); process.exit(0); }
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(76)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/* one site: the editor's web folder at /, the trial's emulator at /emu/ (where the editor looks for port_client.js) */
const TYPES = { html: "text/html", js: "text/javascript", mjs: "text/javascript", css: "text/css", json: "application/json", wasm: "application/wasm" };
const server = createServer((q, r) => {
  const u = decodeURIComponent(q.url.split("?")[0]);
  const [root, rest] = u.startsWith("/emu/") ? [EMU, u.slice(5)] : [HERE, u.replace(/^\/+/, "") || "editor.html"];
  const f = resolve(root, rest || "index.html");
  if (!f.startsWith(root) || !existsSync(f)) { r.writeHead(404); r.end(); return; }
  r.writeHead(200, { "Content-Type": TYPES[f.split(".").pop()] || "application/octet-stream" });
  r.end(readFileSync(f));
}).listen(0, "127.0.0.1");
await new Promise((r) => server.on("listening", r));
const port = server.address().port, cdpPort = 9400 + Math.floor(Math.random() * 400);
const work = join(dirname(EMU), "e2e-chrome"), dl = join(dirname(EMU), "e2e-downloads");
rmSync(work, { recursive: true, force: true });
rmSync(dl, { recursive: true, force: true });
mkdirSync(dl, { recursive: true });
const prof = mkdtempSync(work);
const proc = spawn(chrome, ["--headless=new", "--mute-audio", "--autoplay-policy=no-user-gesture-required", `--remote-debugging-port=${cdpPort}`,
  `--user-data-dir=${prof}`, "--no-first-run", "--disable-background-timer-throttling", "--disable-renderer-backgrounding",
  "--disable-backgrounding-occluded-windows", "about:blank"], { stdio: "ignore" });
const done = () => {
  try { proc.kill(); } catch (e) { /* gone */ }
  for (let k = 0; k < 20 && [prof, dl].some(existsSync); k++) {   /* (Chrome closing writes its files a moment longer) */
    for (const d of [prof, dl]) try { rmSync(d, { recursive: true, force: true }); } catch (e) { /* again below */ }
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 250);
  }
};
process.on("exit", done);
for (const sig of ["SIGINT", "SIGTERM"]) process.on(sig, () => process.exit(130));

async function tab(url) {                           /* a new tab at url: its run(expr) */
  let t;
  for (let i = 0; i < 50 && !t; i++) {
    await sleep(200);
    t = await fetch(`http://127.0.0.1:${cdpPort}/json/new?about:blank`, { method: "PUT" }).then((r) => r.json()).catch(() => null);
  }
  const ws = new WebSocket(t.webSocketDebuggerUrl);
  await new Promise((r) => ws.addEventListener("open", r));
  let id = 0;
  const pending = new Map(), seen = [];
  ws.addEventListener("message", (e) => {
    const m = JSON.parse(e.data);
    if (m.method === "Browser.downloadWillBegin") seen.push(m.params.suggestedFilename);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  });
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send("Page.enable");
  await send("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: dl, eventsEnabled: true });
  await send("Page.navigate", { url });
  const run = async (expr) => {
    const x = (await send("Runtime.evaluate", { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true })).result;
    return x.exceptionDetails ? { err: x.exceptionDetails.exception ? x.exceptionDetails.exception.description : x.exceptionDetails.text } : x.result.value;
  };
  return { run, seen };
}
const U = `const sleep = (ms) => new Promise((r) => setTimeout(r, ms)); const $ = (q) => document.querySelector(q);
  const until = async (f, ms = 60000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) return false; await sleep(100); } return true; };`;

/* the emulator, switched on */
const emu = await tab(`http://127.0.0.1:${port}/emu/index.html`);
const on = await emu.run(`${U} for (let k = 0; k < 15 && !($("#start") && $("#start").hidden); k++) { if ($("#go") && !$("#go").disabled) $("#go").click(); await sleep(2000); }
  return !!$("#start") && $("#start").hidden;`);
ok(on === true, `the firmware in fm1-base's browser emulator, switched on (${JSON.stringify(on)}: ${JSON.stringify(await emu.run(`return document.querySelector("#status") && document.querySelector("#status").textContent`))})`);
await sleep(3000);

const generic = process.argv.includes("--generic");   /* (another fm1-base firmware: tests/companion_app's, say) */
/* the editor: the fm1-base emulator as its device */
const ed = await tab(`http://127.0.0.1:${port}/editor.html?e2e=1&connect=0#backup`);
await ed.run(`${U} await until(() => $("#devsrc")); const s = $("#devsrc"); s.value = "emu"; s.dispatchEvent(new Event("change")); $("#connect").click(); return true;`);
if (generic) {                                      /* its companion services alone, on the Backup page */
  const g = await ed.run(`${U} if (!(await until(() => window.fm1E2E && window.fm1E2E.dev() && window.fm1E2E.dev().info && window.fm1E2E.dev().info.cponly && $("#comp").textContent.length > 0, 60000))) return { status: $("#status").textContent };
    const d = window.fm1E2E.dev(), q = d.cpParams && d.cpParams.find((x) => x.type === 0 && !(x.flags & 1));
    let set = null;
    if (q) { const c = $("#cp" + q.id), want = Math.min(q.max, q.min + 1); c.value = String(want); c.dispatchEvent(new Event("input"));
      await until(() => d.cpVals.get(q.id) === want, 10000); set = { name: q.name, want, back: (await d.comp.get([q.id])).get(q.id) }; }
    return { name: d.hello.name, params: d.cpParams ? d.cpParams.length : 0, files: d.cpFiles ? d.cpFiles.map((f) => f.name) : [], tab: document.documentElement.dataset.page,
      rows: document.querySelectorAll("#comp table tr").length, backup: $("#backup").textContent.length, set };`);
  ok(g && g.name && g.files.length && g.params && g.rows > 3 && g.backup === 0 && g.tab === "backup",
    `another fm1-base firmware: its companion services alone (${JSON.stringify(g && { name: g.name, files: g.files, params: g.params })})`);
  ok(g && g.set && g.set.back === g.set.want, `one of its settings from the page: ${JSON.stringify(g && g.set)}`);
  server.close();
  console.log(failed ? `e2e_fm1emu: ${failed} FAILED` : "e2e_fm1emu: all ok");
  process.exit(failed ? 1 : 0);
}
const conn = await ed.run(`${U} if (!(await until(() => window.fm1E2E && window.fm1E2E.dev() && window.fm1E2E.dev().bk && window.fm1E2E.dev().cpParams && $("#backup").textContent.length > 0, 60000))) return { status: $("#status").textContent };
  const d = window.fm1E2E.dev();
  return { name: d.hello.name, cpfiles: d.cpfiles, objs: d.bk.objs.length, tags: d.bk.objs.filter((o) => o.hasData).map((o) => o.tag.trim()), comp: !!d.bk.comp,
    params: d.cpParams.length, files: d.cpFiles.length, page: $("#backup").textContent.length, compText: $("#comp").textContent };`);
ok(conn && conn.name === "OPTIMIST" && conn.cpfiles && conn.comp && conn.objs >= 60, `the editor connects to the emulator's port: Optimist's backup as companion files (${JSON.stringify(conn && (conn.status != null ? conn : { objs: conn.objs, tags: conn.tags }))})`);
ok(conn && conn.params === 22 && conn.files === conn.objs + 2 && conn.page > 0 && /OPTIMIST/.test(conn.compText),
  `the Backup page: the backup and the companion's part (${conn && conn.params} settings, ${conn && conn.files} files; ${conn && conn.page} ${JSON.stringify(conn && conn.compText.slice(0, 120))})`);

/* a setting from the page: BPM 140, the FM-1's answer back on the page */
const bpm = await ed.run(`${U} const d = window.fm1E2E.dev(), q = d.cpParams.find((x) => x.name === "BPM"), c = $("#cp" + q.id);
  c.value = "140"; c.dispatchEvent(new Event("input"));
  await until(() => d.cpVals.get(q.id) === 140, 10000);
  const back = (await d.comp.get([q.id])).get(q.id);
  return { shown: $("#cp" + q.id + "v").textContent, back };`);
ok(bpm && bpm.shown === "140" && bpm.back === 140, `BPM from the page's slider: the FM-1 holds ${bpm && bpm.back}`);

/* the backup: the work saved by the FM-1 once it stayed the same 10 s (project.c), then the page's button (its list read
   again: bkReadAll), a file saved */
await sleep(12000);
/* the backup: the page's button, a file saved */
const saved = await ed.run(`${U} const b = [...document.querySelectorAll("#backup button")].find((x) => /Back up|Backup/i.test(x.textContent) && !x.disabled);
  if (!b) return { none: [...document.querySelectorAll("#backup button")].map((x) => x.textContent) };
  b.click(); await until(() => /Backup saved/.test($("#status").textContent), 60000); return { status: $("#status").textContent };`);
await sleep(1500);
const files = readdirSync(dl);
ok(saved && /Backup saved: [1-9]/.test(saved.status) && files.some((f) => f.endsWith(".optimist-backup")),
  `a backup saved from the page (${saved && (saved.status || JSON.stringify(saved))}; ${files.join(" ")})`);
server.close();
console.log(failed ? `e2e_fm1emu: ${failed} FAILED` : "e2e_fm1emu: all ok");
process.exit(failed ? 1 : 0);
