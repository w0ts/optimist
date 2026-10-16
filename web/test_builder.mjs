// SPDX-License-Identifier: GPL-3.0-only
// Node checks of the web builder (web/builder/, docs/BUILDER-WEB.md), no browser and no network. Run from the repo root:
//   node web/test_builder.mjs
// - drift: web/builder/data/ is what tools/export_builder_web.py makes from the builder now (registry, costs,
//   profiles, the Python files);
// - the exported Python alone: data/python.json's files written to a folder as the page writes them into Pyodide and
//   run by an isolated CPython (python3 -I, nothing of the repository on its path: the stand-in for Pyodide) give the
//   same registry as data/registry.json and, for every profile and seeded random configurations, the same evaluation,
//   savings, errors per item, fit and BLE room as the CLI builder (tests/builder_web_ref.py: configure.py, room.py);
// - logic.js: the bars, the sort, the search, the list, the presets, the names as the menu (tools/builder/menu.py).
// The same comparison in a real browser with Pyodide: node web/e2e_builder.mjs.
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import * as L from "./builder/logic.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(78)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const HERE = fileURLToPath(new URL(".", import.meta.url));
const ROOT = join(HERE, "..");
const PYTHON = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");
const run = (args, input) => execFileSync(PYTHON, args, { cwd: ROOT, input, maxBuffer: 1 << 28, encoding: "utf8", stdio: ["pipe", "pipe", "pipe"] });
const data = (f) => JSON.parse(readFileSync(join(HERE, "builder", "data", f), "utf8"));
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const sortObj = (v) => (Array.isArray(v) ? v.map(sortObj) : v && typeof v === "object" ? Object.fromEntries(Object.keys(v).sort().map((k) => [k, sortObj(v[k])])) : v);
const firstDiff = (a, b, path = "") => {
  if (same(a, b)) return null;
  if (a && b && typeof a === "object" && typeof b === "object") {
    for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) { const d = firstDiff(a[k], b[k], `${path}.${k}`); if (d) return d; }
  }
  return `${path}: ${JSON.stringify(a)?.slice(0, 120)} != ${JSON.stringify(b)?.slice(0, 120)}`;
};

/* ---- drift */
let drift = "";
try { drift = run(["tools/export_builder_web.py", "--check"]); } catch (e) { drift = String(e.stdout || e.message); }
ok(/up to date/.test(drift), `builder data: web/builder/data is the builder's (export --check)${/up to date/.test(drift) ? "" : ": " + drift.trim()}`);

/* ---- the cases: every profile, seeded random edits of them */
const reg = L.indexRegistry(data("registry.json"));
const prof = data("profiles.json");
let seed = 20261016;
const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
const valuesOf = (it) => (it.choices.length ? it.choices.map((c) => c[0]) : [0, 1]);
const texts = prof.profiles.map((p) => p.text);
const SHA = "0123456789abcdef0123456789abcdef01234567";

/* the exported files, as the page writes them (py-worker.js), in a folder of their own */
const FS = join(ROOT, "build", "web-builder-test", "fs");
rmSync(join(ROOT, "build", "web-builder-test"), { recursive: true, force: true });
const py = data("python.json");
for (const [p, text] of Object.entries(py.files)) { const f = join(FS, "optimist", p); mkdirSync(dirname(f), { recursive: true }); writeFileSync(f, text); }
const COSTS = join(HERE, "builder", "data", "costs.json");
const WEB = `import json, sys
sys.path.insert(0, sys.argv[1] + "/optimist/tools/builder")
import registry, backports, web_api
req = json.load(sys.stdin)
web_api.init(json.load(open(sys.argv[2])), req["needs_auto"], standalone=True)
out = {"registry": web_api.registry_view(), "texts": [web_api.parse_text(t) for t in req["texts"]]}
out["views"] = [dict(web_api.view(q["values"], q["name"], q["commit"]), fit=web_api.fit_view(q["values"])) for q in req["views"]]
out["toggles"] = [web_api.toggle(q["values"], q["key"], q["value"], q["room"]) for q in req["toggles"]]
out["repo"] = {"configure": getattr(sys.modules.get("configure"), "__file__", None), "shared": "shared" in sys.modules,
               "path": [p for p in sys.path if p.startswith(sys.argv[1]) or "optimist" not in p] == sys.path}
json.dump(out, sys.stdout)`;
const web0 = JSON.parse(run(["-I", "-c", WEB, FS, COSTS], JSON.stringify({ needs_auto: py.needs_auto, texts, views: [], toggles: [] })));
ok(same(web0.registry, data("registry.json")), "exported Python alone: its registry is data/registry.json (backports included)");
ok(web0.registry.registry.items.length === reg.items.length && reg.items.length > 100, `exported Python alone: ${reg.items.length} items`);
ok(same(web0.repo, { configure: null, shared: false, path: true }), "exported Python alone: nothing of the repository (configure is the stand-in)");

const views = [], toggles = [];
for (const [i, p] of prof.profiles.entries()) {
  const base = web0.texts[i].values;
  views.push({ values: base, name: p.name, commit: SHA });
  for (let n = 0; n < 6; n++) {           // seeded random edits: 1 to 8 items each
    const v = { ...base };
    for (let k = 1 + rnd(8); k > 0; k--) { const it = reg.items[rnd(reg.items.length)]; const vals = valuesOf(it); v[it.key] = vals[rnd(vals.length)]; }
    views.push({ values: v, name: `${p.name}-r${n}`, commit: n % 2 ? null : SHA });
  }
  toggles.push({ values: base, key: "BLE", value: base.BLE ? 0 : 1, room: null });
  const it = reg.items[rnd(reg.items.length)];
  toggles.push({ values: base, key: it.key, value: valuesOf(it)[rnd(valuesOf(it).length)], room: null });
}
/* everything on (the largest value of each item): an overflow in every region the items fill */
views.push({ values: Object.fromEntries(reg.items.map((it) => [it.key, Math.max(...valuesOf(it))])), name: "all-on", commit: SHA });
/* errors: no synth engine, patterns with 4 sections */
views.push({ values: { ...web0.texts[0].values, ...Object.fromEntries(reg.top["Synth engines"].map((k) => [k, 0])), PATTERNS: 1, SECTIONS: 4 }, name: "errors", commit: SHA });
const req = { needs_auto: py.needs_auto, texts, views, toggles };
const web = JSON.parse(run(["-I", "-c", WEB, FS, COSTS], JSON.stringify(req)));
const cli = JSON.parse(run(["tests/builder_web_ref.py"], JSON.stringify(req)));
ok(same(web.texts, cli.texts), `profiles read alike (${texts.length}: values, names, warnings)`);
let bad = [];
for (const [i, q] of views.entries()) {
  const w = web.views[i], c = cli.views[i];
  for (const f of ["eval", "savings", "conflicts", "over", "fit", "dump"]) if (!same(sortObj(w[f]), sortObj(c[f]))) bad.push(`${q.name} ${f}: ${firstDiff(sortObj(w[f]), sortObj(c[f]))}`);
}
ok(!bad.length, `${views.length} configurations: evaluate, budget, fits, key, savings, errors, fit = the CLI's${bad.length ? "\n  " + bad.slice(0, 5).join("\n  ") : ""}`);
ok(views.some((q, i) => web.views[i].eval.errors.length) && views.some((q, i) => Object.keys(web.views[i].over).length) && views.some((q, i) => web.views[i].eval.key),
  "the cases include errors, an overflow and a cloud key");
bad = toggles.map((q, i) => (same(web.toggles[i].values, cli.toggles[i].values) && same(web.toggles[i].room, cli.toggles[i].room) ? null : `${q.key}=${q.value}`)).filter(Boolean);
ok(!bad.length, `${toggles.length} toggles (BLE making room included) = room.after_toggle's${bad.length ? ": " + bad.join(", ") : ""}`);
ok(toggles.some((q, i) => q.key === "BLE" && web.toggles[i].room.dropped.length), "BLE removed an item to make room in at least one profile");

/* ---- logic.js against the menu's rules */
const unit = (o) => { let u = 1024; while (o > u * 20) u *= 2; return u; };
ok([1, 1024, 20480, 20481, 100000, 3e6].every((o) => L.overUnit(o) === unit(o)), "bars: the overflow's cell size (menu.py over_unit)");
const bo = L.barModel("flash", 600000, 581564, 18436), bf = L.barModel("ram", 63660, 98304, -34644);
ok(bo.state === "over" && bo.text === "OVER by 18,436 B" && bo.detail === "(600,000 / 581,564; one red cell = 1 KB)" && bo.cells === 19,
  "bars: an overflow reads OVER by N B (used / capacity; one red cell = K KB)");
ok(bf.state === "ok" && bf.text === "63,660 / 98,304 B" && bf.detail === "34,644 free" && L.barModel("x", 98, 100, -2).state === "tight", "bars: used / capacity B, N free; over 97 % tight");
ok(L.kDelta(1229) === "+1.2K" && L.kDelta(-512) === "-0.5K" && L.kDelta(0) === "+0.0K", "costs: +1.2K as the menu");
const sav = { A: { flash: 100, ram: 10, pool: 0, ramtext: 0 }, B: { flash: 900, ram: 0, pool: 70, ramtext: 0 }, C: { flash: 100, ram: 0, pool: 0, ramtext: 0 } };
ok(same(L.sortedKeys(["A", "D", "B", "C"], sav, "flash"), ["B", "A", "C", "D"]) && same(L.sortedKeys(["A", "D", "B"], sav, null), ["A", "D", "B"]),
  "sort: largest first, ties in order, unmeasured last; none: the registry order");
ok(same(L.deltaOf("A", sav, { ram: 5 }), [["flash", 100], ["ram", 10]]) && same(L.deltaOf("B", sav, {}), [["flash", 900], ["pool", 70]]) && same(L.deltaOf("Z", sav, {}), []),
  "costs shown: the regions it moves by 64 B or more and the overflowing ones it takes");
ok(L.offFits([["flash", 900]], { flash: 800 }) && !L.offFits([["flash", 900]], { flash: 800, ram: 1 }) && !L.offFits([["flash", 1]], {}), "◀ off = fits: alone it covers every overflow");
const t0 = L.treeOf(reg, "", {}, null);
ok(same(t0.map((g) => g.group), reg.groups.filter((g) => (reg.top[g] || []).length)) && t0.reduce((n, g) => n + g.items.length + g.items.reduce((m, i) => m + i.children.length, 0), 0) === reg.items.length,
  "list: every group with items, every item once (top level, then its options)");
const tq = L.treeOf(reg, "reverb", {}, null);
ok(tq.length > 0 && tq.every((g) => g.items.every((i) => L.matches(reg.byKey[i.key], "reverb") || i.children.some((c) => L.matches(reg.byKey[c], "reverb")))), "search: what matches label, key, group or description, with its parent");
const ps = L.presets(prof);
ok(ps[0].kind === "flavour" && ps.filter((p) => p.kind === "profile").length === prof.profiles.length && ps.at(-1).kind === "defaults", "presets: the flavours, then every profile, then the registry defaults");
{
  const P = { profiles: [{ name: "a", text: "" }, { name: "synth", text: "ENG_FM6=1\n" }], flavours: [{ id: "x", title: "X", profile: "a" }, { id: "synth", title: "Synth", profile: "synth", default: true }] };
  const P2 = { profiles: P.profiles, flavours: [{ id: "x", title: "X", profile: "a" }, { id: "synth", title: "Synth", profile: "synth" }] };
  ok(L.defaultPreset(P).id === "flavour:synth" && L.defaultPreset(P2).id === "flavour:x" && L.defaultPreset({ profiles: P.profiles }).id === "profile:a"
    && L.defaultPreset(prof).kind === "flavour", "default preset: the flavour marked default, else the first flavour, else the first profile");
}
ok(L.fileName("user default") === "user-default.config" && L.fileName("../x") === "x.config" && L.fileName("") === "optimist.config" && L.firmwareName("a<b>c d+e.f_g-h12345678") === "abc d+e.f_g-h123", "names: the file's, the firmware's (16 characters)");
ok(L.commitOf({ commit: SHA }) === SHA && L.commitOf({ commit: "HEAD" }) === null && L.commitOf(null) === null, "build-info: only a full commit SHA makes a key");
ok(L.pyodideBase("/pyodide/") === "/pyodide/" && L.pyodideBase("https://evil.example/") === L.PYODIDE_CDN && L.pyodideBase("/../x/") === L.PYODIDE_CDN
  && /^https:\/\/cdn\.jsdelivr\.net\/pyodide\/v\d+\.\d+\.\d+\/full\/$/.test(L.PYODIDE_CDN), "Pyodide: the pinned CDN version; a test may only point at this site");
/* the gauges (the sticky bar): used / limit in KiB, the overflow only when over, the limit's mark past it */
const go = L.gaugeModel("flash", 600000, 581564, 18436), gf = L.gaugeModel("ram", 63660, 98304, -34644);
ok(go.state === "over" && go.text === "585.9 / 567.9K" && go.overText === "+18.0K" && Math.abs(go.capAt - 581564 / 600000) < 1e-9 && go.fill === go.capAt
  && /over by 18,436 B/.test(go.tip), "gauges: over: used / limit, +N K in the alert colour, the limit marked, the exact bytes in the tooltip");
ok(gf.state === "ok" && gf.text === "62.2 / 96.0K" && gf.overText === "" && gf.capAt === null && Math.abs(gf.fill - 63660 / 98304) < 1e-9 && /34,644 B free/.test(gf.tip)
  && L.gaugeModel("x", 98, 100, -2).state === "tight", "gauges: fitting: used / limit, no overflow text; over 97 % tight");
/* the notices policy: errors and the experimental items that are on stay on the page; the NOTICE texts go to the notes */
const reg1 = { byKey: { A: { key: "A", label: "alpha", experimental: true, tested: "emulator-tested only" }, B: { key: "B", label: "beta" }, C: { key: "C", label: "gamma" },
  U: { key: "U", label: "USB" }, X: { key: "X", label: "off exp", experimental: true, tested: "t" } } };
const view1 = { built: ["A", "B", "C", "U"], over: {}, savings: {}, undo: null, eval: {
  errors: [], warnings: [{ text: "alpha: EXPERIMENTAL (emulator-tested only)", keys: ["A"] }, { text: "USB audio: EXPERIMENTAL, not checked", keys: ["U"] }, { text: "DUCK follows the kick", keys: ["B"] }],
  notices: [{ text: "beta: Tested in the host tests only, not on a device.", keys: ["B"] }, { text: "gamma: Tested in the host tests only, not on a device.", keys: ["C"] }],
  budget: { unmeasured: [] } } };
const m1 = L.messagesOf(view1, reg1);
ok(!m1.errors.length && same(m1.experimental.map((x) => x.key), ["A", "U"]) && m1.notes.length === 2
  && same(m1.notes[1], { text: "Tested in the host tests only, not on a device.", labels: ["beta", "gamma"] }) && m1.notes[0].text === "DUCK follows the kick",
  "notices: no error; one line per experimental item that is on (the off one not); a NOTICE text once, with its items; hints in the notes");
const view2 = { ...view1, built: [], over: { flash: 4096, pool: 100 }, savings: { B: { flash: 5000, pool: 200 } },
  eval: { ...view1.eval, warnings: [], notices: [], errors: [{ text: "at least one synth engine", keys: ["B"] }] } };
const m2 = L.messagesOf(view2, reg1);
ok(m2.errors.length === 2 && m2.errors.filter((e) => e.kind === "over").length === 1 && m2.errors[0].text === "FLASH +4.0K, POOL +0.1K"
  && m2.errors[0].sub.some((s) => /◀ off = fits/.test(s)) && m2.errors[1].kind === "rule" && !m2.notes.length && !m2.experimental.length,
  "notices: an overflow is ONE error line (every region, the biggest items, the legend), a broken rule another");
ok(L.hasInfo(reg1.byKey.A) && !L.hasInfo(reg1.byKey.B) && L.hasInfo({ notice: "n" }) && L.hasInfo({ provenance_line: "p" }), "notices: an (i) on the items with notes");
const page = readFileSync(join(HERE, "builder", "index.html"), "utf8");
ok(page.includes("<!--OPTIMIST-HEADER-->") && page.includes("/*OPTIMIST-CSS*/") && /Cloud builds are not live yet/.test(page) && /id="gauges"/.test(page)
  && /#gauges \{ position: sticky; top: 0/.test(page) && !/localStorage/.test(page), "page: the shared header and style, the sticky gauges, the build note");
const css = readFileSync(join(HERE, "style", "optimist.css"), "utf8");
ok(/@media \(prefers-color-scheme: light\)/.test(css) && ["--surface-0", "--surface-3", "--text-dim", "--accent", "--ok", "--warn", "--err", "--t1", "--t4"].every((t) => css.includes(t + ":"))
  && !/data-theme/.test(css), "style: the tokens, light / dark from the system (no switch)");
const inst = readFileSync(join(HERE, "installer", "index.html"), "utf8"), instJs = readFileSync(join(HERE, "installer", "installer.js"), "utf8");
ok(/build: "\.\.\/builder\/"/.test(instJs) && /link\("build", TEXT\.buildLink\)/.test(instJs) && inst.includes("<!--OPTIMIST-HEADER-->") && inst.includes("/*OPTIMIST-CSS*/"),
  "installer: the shared header and style, links to the builder");
const hdr = JSON.parse(execFileSync(process.env.PYTHON || "python3", ["-c", "import json, sys; sys.path.insert(0, 'web'); import make_site; print(json.dumps([make_site.shared_header(a) for a in ('installer', 'editor', 'builder')]))"], { cwd: join(HERE, ".."), encoding: "utf8" }));
ok(hdr.every((h, i) => (h.match(/aria-current="page"/g) || []).length === 1 && h.includes(`href="../${["installer", "editor", "builder"][i]}/" aria-current="page"`)
  && /href="\.\.\/installer\/"/.test(h) && /href="\.\.\/editor\/"/.test(h) && /href="\.\.\/builder\/"/.test(h) && /<svg/.test(h)), "header: the dinghy, the three apps linked, the current one marked");

console.log(failed ? `${failed} FAILED` : "all ok");
process.exit(failed ? 1 : 0);
