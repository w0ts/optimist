// SPDX-License-Identifier: GPL-3.0-only
// The mirrored screen is sharp in a real browser: a known checkerboard frame (every panel pixel black or white) goes through the
// editor's own mirror drawing code (the part of web/editor.html between mirror-sharp:begin and :end, and its #mirror CSS), is shown at
// several box widths and device pixel ratios, and the browser's screenshot is read back pixel by pixel: only black and white may
// appear (no smoothing), and every block of the checkerboard is the same width. Run:  node web/e2e_mirror_sharp.mjs [--shots DIR] [--before]
// (--before draws the former way, putImageData into a 240 canvas scaled by CSS, to measure what was). Chrome as e2e_daw.mjs (CHROME=path).
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n) => (process.argv.includes(n) ? process.argv[process.argv.indexOf(n) + 1] : null);
const shots = arg("--shots"), BEFORE = process.argv.includes("--before");
if (shots) mkdirSync(shots, { recursive: true });
const CHROMES = [process.env.CHROME, "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser"].filter(Boolean);
const chrome = CHROMES.find((p) => existsSync(p));
if (!chrome) { console.log("e2e_mirror_sharp: no Chrome found (CHROME=path)"); process.exit(0); }
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const html = readFileSync(join(HERE, "editor.html"), "utf8");
const css = (html.match(/^\s*#mirror \{[^\n]*\}/m) || [""])[0];
const code = (html.match(/\/\* mirror-sharp:begin[\s\S]*?mirror-sharp:end \*\//) || [""])[0];
ok(css.includes("image-rendering") && code.includes("mirPaint"), "editor.html: the #mirror CSS and the mirror-sharp block are found");
const page = `<!doctype html><meta charset=utf-8><style>body{margin:0;background:#000} ${css} #box{margin:0}</style>
<div id=box class=mirscreen><canvas id=mirror width=240 height=240></canvas></div>
<script>
const $ = (i) => document.getElementById(i);
const mir = { img: null };
${BEFORE ? "const mirSrc = $('mirror'); function mirPaint(c) { c.getContext('2d').putImageData(mir.img, 0, 0); }" : code}
mir.img = mirSrc.getContext('2d').createImageData(240, 240);
for (let y = 0; y < 240; y++) for (let x = 0; x < 240; x++) { const v = (x + y) % 2 ? 0 : 255, i = (y * 240 + x) * 4; mir.img.data[i] = mir.img.data[i + 1] = mir.img.data[i + 2] = v; mir.img.data[i + 3] = 255; }
mirSrc.getContext('2d').putImageData(mir.img, 0, 0);
window.show = (w) => { $('box').style.width = w + 'px'; mirPaint($('mirror')); };
</script>`;
const server = createServer((q, r) => { r.writeHead(200, { "Content-Type": "text/html" }); r.end(page); }).listen(0, "127.0.0.1");
await new Promise((r) => server.on("listening", r));
const port = server.address().port, cdpPort = 9400 + Math.floor(Math.random() * 400);
const prof = mkdtempSync(join(tmpdir(), "e2e-mirsharp-"));
const proc = spawn(chrome, ["--headless=new", "--mute-audio", `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${prof}`, "--no-first-run", "about:blank"], { stdio: "ignore" });
const done = (code) => { try { proc.kill(); } catch (e) { /* gone */ } server.close(); try { rmSync(prof, { recursive: true, force: true }); } catch (e) { /* busy */ } process.exit(code); };
process.on("uncaughtException", (e) => { console.error(e); done(1); });
let tab;
for (let i = 0; i < 50 && !tab; i++) { await sleep(200); tab = await fetch(`http://127.0.0.1:${cdpPort}/json/new?about:blank`, { method: "PUT" }).then((r) => r.json()).catch(() => null); }
const ws = new WebSocket(tab.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener("open", r));
let id = 0; const pending = new Map();
ws.addEventListener("message", (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } });
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const run = async (expr) => { const x = (await send("Runtime.evaluate", { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true })).result; return x.exceptionDetails ? { err: String(x.exceptionDetails.exception ? x.exceptionDetails.exception.description : x.exceptionDetails.text) } : x.result.value; };
await send("Page.enable");
await send("Page.navigate", { url: `http://127.0.0.1:${port}/` });
await sleep(500);

/* the screenshot's pixels: colours, and the widths of the checkerboard's blocks along the canvas's middle row */
const analyse = async (b64, rc, dpr) => run(`const bm = await createImageBitmap(await (await fetch("data:image/png;base64,${b64}")).blob());
  const X = Math.round(${rc.x} * ${dpr}), Y = Math.round(${rc.y} * ${dpr}), W = Math.round(${rc.w} * ${dpr}), H = Math.round(${rc.h} * ${dpr});
  const c = document.createElement("canvas"); c.width = W; c.height = H; const x = c.getContext("2d"); x.drawImage(bm, X, Y, W, H, 0, 0, W, H);
  const d = x.getImageData(0, 0, c.width, c.height).data, cols = new Set();
  for (let i = 0; i < d.length; i += 4) cols.add(d[i] << 16 | d[i + 1] << 8 | d[i + 2]);
  const row = (c.height >> 1) * c.width * 4, runs = {}; let r = 1;
  let first = true;   /* (the first run is cut by the crop's edge: left out) */
  for (let k = 1; k < c.width; k++) { if (d[row + k * 4] === d[row + (k - 1) * 4]) r++; else { if (!first) runs[r] = (runs[r] || 0) + 1; first = false; r = 1; } }
  return { w: c.width, cols: [...cols].length, runs };`);

for (const dpr of [1, 1.5, 2, 3]) {
  for (const w of [301, 413, 500, 640]) {
    await send("Emulation.setDeviceMetricsOverride", { width: 800, height: 800, deviceScaleFactor: dpr, mobile: false });
    await run(`window.show(${w}); await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));`);
    const rc = await run(`const r = $("mirror").getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height, k: $("mirror").dataset.k || "-" };`);
    const s = await send("Page.captureScreenshot", { format: "png" });
    if (shots) writeFileSync(join(shots, `${BEFORE ? "before" : "after"}-dpr${dpr}-w${w}.png`), Buffer.from(s.result.data, "base64"));
    const a = await analyse(s.result.data, rc, dpr);
    const widths = Object.keys(a.runs).map(Number).sort((p, q) => p - q);
    const inner = widths.length;   /* (distinct block widths among the runs: one when every block is the same) */
    console.log(`  dpr ${dpr}, box ${w}px: shot ${a.w}px, colours ${a.cols}, block widths ${JSON.stringify(a.runs)}, k ${rc.k}`);
    if (!BEFORE) {
      ok(a.cols === 2, `dpr ${dpr}, box ${w}px: only black and white (${a.cols} colours)`);
      ok(inner <= 2 && widths.every((p) => p === widths[0] || p === widths[0] + 0), `dpr ${dpr}, box ${w}px: every block one width (${widths.join(",")})`);
    }
  }
}
done(failed ? 1 : 0);
