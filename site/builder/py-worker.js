// SPDX-License-Identifier: GPL-3.0-only
// The web builder's Python (a module worker, so the page never stalls while the rules run): Pyodide, pinned, from the
// CDN (logic.js PYODIDE_VERSION), with the builder's own Python files (data/python.json: core.py, registry.py,
// backports.py, room.py, web_api.py) written into its file system as the repository has them, then
// tools/builder/web_api.py answers the page's calls. Messages:
//   page -> worker  {boot: {base, data}}              base: the Pyodide files' URL; data: the data/ folder's URL
//                   {id, fn, args}                    a web_api call, args a JSON-able array
//   worker -> page  {progress: {stage, loaded, total}} while it starts
//                   {ready: {python, items}}          or {failed: message}
//                   {id, result} or {id, error}
let py = null;

const post = (m) => self.postMessage(m);
const progress = (stage, loaded = 0, total = 0, step = "") => post({ progress: { stage, loaded, total, step } });

/** fetch the big files first with a byte count (Pyodide's own fetch then finds them in the HTTP cache) */
async function prefetch(base) {
  const files = ["pyodide.asm.wasm", "python_stdlib.zip", "pyodide.asm.mjs"];
  const sizes = files.map(() => 0), seen = files.map(() => 0);
  const report = () => progress("download", seen.reduce((a, b) => a + b, 0),
    sizes.every((n) => n > 0) ? sizes.reduce((a, b) => a + b, 0) : 0);   // (0: the total is not known)
  await Promise.all(files.map(async (f, i) => {
    const r = await fetch(base + f);
    if (!r.ok) throw new Error(`${f}: HTTP ${r.status}`);
    const n = +(r.headers.get("content-length") || 0);
    sizes[i] = r.headers.get("content-encoding") ? 0 : n;    // (compressed: the length is not what is read)
    if (!r.body) { await r.arrayBuffer(); return; }
    const rd = r.body.getReader();
    for (;;) {
      const { done, value } = await rd.read();
      if (done) break;
      seen[i] += value.length;
      report();
    }
  }));
}

async function boot(base, data) {
  progress("download");
  await prefetch(base);
  progress("start");
  const { loadPyodide } = await import(base + "pyodide.mjs");
  py = await loadPyodide({ indexURL: base, stdout: () => {}, stderr: (s) => console.warn("python:", s) });
  progress("rules");
  const [pyFiles, costs] = await Promise.all([fetch(data + "python.json").then((r) => r.json()),
    fetch(data + "costs.json").then((r) => r.text())]);
  progress("import");
  for (const [path, text] of Object.entries(pyFiles.files)) {
    const full = "/optimist/" + path;
    py.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
    py.FS.writeFile(full, text);
  }
  py.globals.set("_costs", costs);
  py.globals.set("_needs", JSON.stringify(pyFiles.needs_auto));
  const steps = [["import json, sys", ""], ['sys.path.insert(0, "/optimist/tools/builder")', ""], ["import registry", "registry"],
    ["import backports", "backports"], ["import core, web_api", "core"],
    ["_init = web_api.init(json.loads(_costs), json.loads(_needs), standalone=True)", "init"]];
  for (const [code, what] of steps) {        // (one at a time: a step that fails is named)
    if (what) progress("import", 0, 0, what);
    py.runPython(code);
  }
  post({ ready: { python: py.runPython("sys.version.split()[0]"), items: py.runPython("_init['items']"),
    fetched: performance.getEntriesByType("resource").map((r) => r.name) } });   // (what it loaded: the tests check the origin)
}

function call(fn, args) {
  py.globals.set("_fn", fn);
  py.globals.set("_args", JSON.stringify(args || []));
  return JSON.parse(py.runPython(`json.dumps((lambda f, a: f(*a))(getattr(web_api, _fn), json.loads(_args)))`));
}

const ALLOWED = new Set(["registry_view", "view", "toggle", "item_info", "fit_view", "proposals_view", "parse_text"]);

self.onmessage = async (e) => {
  const m = e.data || {};
  if (m.boot) {
    try { await boot(m.boot.base, m.boot.data); } catch (err) { post({ failed: String(err && err.message || err) }); }
    return;
  }
  if (!py) { post({ id: m.id, error: "Python is not ready" }); return; }
  if (!ALLOWED.has(m.fn)) { post({ id: m.id, error: `${m.fn}: not a builder call` }); return; }
  try { post({ id: m.id, result: call(m.fn, m.args) }); } catch (err) {
    const msg = String(err && err.message || err);
    const last = msg.trim().split("\n").pop();      // (a Python traceback: its last line, "ConfigError: ...")
    post({ id: m.id, error: last.replace(/^\w*(Error|Exception): /, "") });
  }
};
