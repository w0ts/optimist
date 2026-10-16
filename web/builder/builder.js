// SPDX-License-Identifier: GPL-3.0-only
// The web builder as a module: mountBuilder(container, {onNavigate, pyodideBase}) builds the whole UI inside `container`
// (a root class `ob-root`, the shared o- classes, its own CSS in builder.css under that class) and returns a handle:
// activate() / deactivate() (the keyboard shortcuts and, the first time, Python), destroy(). Nothing here touches the
// page outside the container but the shortcuts while active and one <link> for builder.css. The list, the cards, the
// messages and the details are the menu's (tools/builder/menu.py); every rule runs in Python (py-worker.js ->
// tools/builder/web_api.py). docs/BUILDER-WEB.md.
import {
  PYODIDE_VERSION, REGIONS, SORTS, barModel, fmtK, built, commitOf, defaultPreset, defaults, deltaOf, fileName, fmt,
  changeText, gaugeModel, hasInfo, indexRegistry, kDelta, messagesOf, offFits, oneLine, overflowLines, presets, pyodideBase, treeOf,
} from "./logic.js";


const TEMPLATE = `
  <div id="loading" role="status" aria-live="polite"><span id="loadtext">Loading the builder</span>
    <progress id="loadbar" class="o-progress" max="100" aria-label="Loading Python"></progress>
    <div class="o-dim" id="loadnote">The builder's rules are its Python code, run in this page (Pyodide, about 13 MB the first time, then cached by the browser).</div>
  </div>

  <div class="cols">
    <div id="left">
    <div class="tools">
      <label class="o-field">Start from<select id="bpreset" aria-describedby="presetdesc"></select></label>
      <label class="o-field">Name<input id="name" type="text" maxlength="40" autocomplete="off" spellcheck="false"></label>
      <label class="o-field grow">Search<input id="search" type="search" placeholder="label, key, group" autocomplete="off"></label>
      <label class="o-field half">Sort<select id="sort"><option value="">default order</option><option value="flash">flash, largest first</option><option value="ram">ram, largest first</option><option value="pool">pool, largest first</option><option value="ramtext">ramtext, largest first</option></select></label>
      <button type="button" id="expand" class="icon" title="Expand all (x)" aria-label="Expand all">▾▾</button>
      <button type="button" id="collapse" class="icon" title="Collapse all (c)" aria-label="Collapse all">▸▸</button>
      <button type="button" id="reset" disabled>Reset to default</button>
    </div>
    <p id="presetdesc" class="o-small o-dim"></p>

      <section id="tree" aria-label="Items"></section>
    </div>
    <aside id="bside" aria-label="Size, configuration and details">
      <section id="gauges" class="o-panel" aria-label="Size estimate">
        <div class="o-panel-h"><h2>Size</h2><span class="o-chip accent" id="previewtag" hidden>preview</span><button type="button" id="how" class="o-info" aria-label="About the estimate" aria-haspopup="dialog">i</button></div>
        <div class="o-panel-b">
          <div id="bars"></div>
          <div class="side">
            <button type="button" id="fit" class="primary" hidden>Make it fit</button>
            <span id="undo" class="o-chip"></span>
            <button type="button" id="keychip" class="o-chip tip-end" data-tip="The cloud key of this configuration" aria-label="Copy the configuration's key">key -</button>
          </div>
        </div>
      </section>
      <section id="result" class="o-panel" aria-label="Your configuration">
        <div class="o-panel-h"><h2>Your configuration</h2></div>
        <div class="o-panel-b">
          <div class="row2">
            <button type="button" id="download" class="primary" disabled>Download .config</button>
            <button type="button" id="build" disabled aria-describedby="buildnote">Build in the cloud</button>
          </div>
          <p class="o-small">Hash <code id="hash">-</code> · cloud key <code id="key">-</code></p>
          <p class="o-small o-dim" id="keynote"></p>
          <details class="o-details" id="buildhow"><summary>Build it yourself</summary>
            <p id="buildnote" class="o-small o-dim">Cloud builds are not live yet. Build it yourself from a checkout: <code>python3 tools/optimist.py build --config FILE</code> (or open it in the menu: <code>python3 tools/optimist.py builder --config FILE</code>).</p>
          </details>
          <details class="o-details" id="import">
            <summary>Load a .config file (or paste one)</summary>
            <div>
              <div class="row2"><label class="btn" for="bfile">Choose a file</label><input id="bfile" type="file" accept=".config,.txt,text/plain" class="o-sr"><span id="filename" class="o-small o-dim"></span></div>
              <label for="paste" class="o-sr">.config text</label>
              <textarea id="paste" placeholder="ENG_FM6=1&#10;FX_REVERB=1&#10;..." spellcheck="false"></textarea>
              <div class="row2"><button type="button" id="pasteload">Load the pasted text</button><span id="importmsg" class="o-small" role="status"></span></div>
            </div>
          </details>
          <details class="o-details"><summary>The canonical configuration (what the key hashes)</summary><pre id="canon"></pre></details>
          <details class="o-details"><summary>The .config file</summary><pre id="dump"></pre></details>
        </div>
      </section>
      <section id="proposals" class="o-panel" aria-label="Ways to fit" hidden>
        <div class="o-panel-h"><h2>Ways to fit</h2><button type="button" id="propsclose" class="quiet sm" aria-label="Close the proposals">close</button></div>
        <div class="o-panel-b" id="propsb"></div>
      </section>
      <div id="alerts">
        <div id="msgs" aria-live="polite"></div>
        <details class="o-details" id="notes" hidden><summary id="notessum">Notes</summary><ul id="noteslist"></ul></details>
      </div>
      <section id="info" class="o-panel" aria-live="polite" aria-label="Details">
        <div class="o-panel-h"><h2>Info</h2></div>
        <div class="o-panel-b" id="infob"><p class="o-dim">Select an item to see what it does and what it costs.</p></div>
      </section>
    </aside>
  </div>


  <footer class="o-foot ob-foot">
    Optimist is GPL-3.0-only, no warranty. Source: <a href="https://github.com/w0ts/optimist" rel="noopener" target="_blank">github.com/w0ts/optimist</a>.
    Python in the page: Pyodide <span id="pyver"></span> (MPL-2.0), loaded from cdn.jsdelivr.net.
    Licences: <a href="../../LICENSE">LICENSE</a>, <a href="../../LICENSING.md">LICENSING.md</a>.
  </footer>
<div id="bpop" class="o-pop" popover role="dialog" aria-labelledby="poph"></div>
<div id="toasts" class="o-toasts" aria-live="polite"></div>

`;

/** builder.css, once per document (a <link> next to this module, whatever page mounts it) */
function loadStyle() {
  const href = new URL("./builder.css", import.meta.url).href;
  if ([...document.querySelectorAll("link[data-ob-style]")].some((l) => l.href === href)) return;
  const link = document.createElement("link");
  link.rel = "stylesheet";
  link.href = href;
  link.dataset.obStyle = "";
  document.head.append(link);
}

const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (k === "class") e.className = v; else if (k === "text") e.textContent = v; else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v === true ? "" : v);
  }
  for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) e.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return e;
};

export function mountBuilder(container, opts = {}) {
const root = container;
root.classList.add("ob-root", "o-ui");
root.innerHTML = TEMPLATE;
loadStyle();
const $ = (id) => root.querySelector("#" + id);
let active = false, started = false;
const REMEMBER = "optimist-builder-last";        // (only a convenience: the page works without storage)
const remember = (text) => { try { localStorage.setItem(REMEMBER, JSON.stringify({ text })); } catch (e) { /* no storage: fine */ } };
const recall = () => { try { const v = JSON.parse(localStorage.getItem(REMEMBER) || "null"); return v && typeof v.text === "string" ? v.text : null; } catch (e) { return null; } };

const FOLDS = "optimist-builder-folds";       // (the folded sections, remembered by this browser: only a convenience)
const loadFolds = () => { try { const v = JSON.parse(localStorage.getItem(FOLDS) || "[]"); return new Set(Array.isArray(v) ? v.filter((x) => typeof x === "string") : []); } catch (e) { return new Set(); } };
const saveFolds = () => { try { localStorage.setItem(FOLDS, JSON.stringify([...S.collapsed])); } catch (e) { /* no storage: fine */ } };

const S = {
  reg: null, prof: null, presets: [], values: {}, name: "", room: null, sel: null, filter: "", sortBy: null,
  stages: [], collapsed: loadFolds(), proposals: null, rejected: new Set(), preview: null, fitOpen: false, pseq: 0, view: null, info: {}, commit: null, ready: false, seq: 0, shown: 0, fit: null,
};

/* ---- Python (the worker) */
let worker = null, nextId = 0;
const waiting = new Map();
let readyResolve, readyReject;
const pyReady = new Promise((res, rej) => { readyResolve = res; readyReject = rej; });
function call(fn, ...args) {
  return new Promise((res, rej) => { const id = ++nextId; waiting.set(id, { res, rej }); worker.postMessage({ id, fn, args }); });
}
function startPython() {
  worker = new Worker(new URL("./py-worker.js", import.meta.url), { type: "module" });
  worker.onmessage = (e) => {
    const m = e.data;
    if (m.progress) return showProgress(m.progress);
    if (m.ready) { readyResolve(m.ready); return; }
    if (m.failed) { readyReject(new Error(m.failed)); return; }
    const w = waiting.get(m.id);
    if (!w) return;
    waiting.delete(m.id);
    if ("error" in m) w.rej(new Error(m.error)); else w.res(m.result);
  };
  worker.onerror = (e) => readyReject(new Error(e.message || "the Python worker did not start"));
  worker.postMessage({ boot: { base: pyodideBase(opts.pyodideBase || window.OPTIMIST_PYODIDE_BASE), data: new URL("./data/", import.meta.url).href } });
}
function showProgress({ stage, loaded, total, step }) {
  if (!S.stages.length || S.stages.at(-1)[0] !== stage + (step || "")) S.stages.push([stage + (step || ""), Math.round(performance.now())]);
  const bar = $("loadbar");
  if (stage === "download") {
    $("loadtext").textContent = `Downloading Python (Pyodide ${PYODIDE_VERSION})` + (loaded ? `: ${(loaded / 1e6).toFixed(1)} MB` + (total ? ` of ${(total / 1e6).toFixed(1)} MB` : "") : "");
    if (total) { bar.max = total; bar.value = loaded; } else bar.removeAttribute("value");
  } else if (stage === "start") { $("loadtext").textContent = "Starting Python"; bar.removeAttribute("value"); }
  else if (stage === "rules") { $("loadtext").textContent = "Loading the builder's rules"; bar.removeAttribute("value"); }
  else if (stage === "import") { $("loadtext").textContent = "Reading the builder's rules" + (step ? ` (${step})` : ""); bar.removeAttribute("value"); }
}

/* ---- the configuration */
async function loadText(text, name, source) {
  const r = await call("parse_text", text);
  if (r.error) return { ok: false, message: `${source}: ${r.error}`, warnings: r.warnings };
  S.values = r.values;
  S.name = r.name || name || "custom";
  S.room = null;
  S.fit = null;
  $("name").value = S.name;
  await refresh();
  return { ok: true, warnings: r.warnings };
}

async function choosePreset(id) {
  const p = S.presets.find((x) => x.id === id);
  if (!p) return;
  $("bpreset").value = id;
  $("presetdesc").textContent = p.description;
  if (p.kind === "defaults") {
    S.values = defaults(S.reg); S.name = "default"; S.room = null; S.fit = null; $("name").value = S.name;
    await refresh();
    return;
  }
  const r = await loadText(p.text, p.name, p.label);
  if (!r.ok) say(r.message);
}

async function refresh() {
  const seq = ++S.seq;
  const v = await call("view", S.values, S.name, S.commit);
  if (seq < S.shown) return;                 // (an older reply after a newer one: dropped)
  S.shown = seq;
  S.view = v;
  remember(v.dump);
  render();
  await syncProposals();
}

async function setItem(key, value) {
  const r = await call("toggle", S.values, key, value, S.room);
  S.values = r.values;
  S.room = r.room;          // (its note shows on the BLE row; the item's notice is in its (i) and the notes)
  S.fit = null;
  await refresh();
}

/* ---- rendering */
function render() {
  renderBars();
  renderMsgs();
  renderTree();
  renderResult();
  if (S.sel) renderInfo(S.sel);
}

function howText() {
  const b = S.view && S.view.eval && S.view.eval.budget;
  if (!b) return "No costs.json in the export: no estimate.";
  return `${S.name}: an estimate from measured deltas` +
    (b.anchor ? ` (from the real build of ${b.anchor}, ${b.distance} item${b.distance === 1 ? "" : "s"} away)` : "") +
    "; a real build reports the exact sizes. The size is shown, never a reason the page refuses anything.";
}

function renderBars() {
  const v = S.view, ev = v && v.eval;
  const bars = $("bars");
  bars.textContent = "";
  if (!ev || !ev.fits) return;
  let over = false;
  const pv = S.preview && (S.proposals || []).find((p) => p.id === S.preview);
  $("gauges").toggleAttribute("data-preview", !!pv);
  $("previewtag").hidden = !pv;
  for (const r of REGIONS) {
    const f = pv ? pv.after[r] : ev.fits[r];
    const m = gaugeModel(r, f.used, f.capacity, f.over);
    over = over || m.state === "over";
    const bar = el("div", { class: "gb" }, el("div", { class: "gf", style: `width: ${(m.fill * 100).toFixed(1)}%` }));
    if (m.capAt !== null) bar.append(el("div", { class: "gx", style: `left: ${(m.capAt * 100).toFixed(1)}%; right: 0` }), el("div", { class: "gc", style: `left: calc(${(m.capAt * 100).toFixed(1)}% - 1px)` }));
    bars.append(el("div", { class: "o-gauge", "data-region": r, "data-state": m.state, title: m.tip, role: "img", "aria-label": m.tip },
      el("span", { class: "gl", text: r.toUpperCase() }),
      el("span", { class: "gn" }, el("span", { class: "gu", text: m.text }), m.overText ? el("span", { class: "go", text: m.overText }) : null), bar));
  }
  const overNow = Object.keys(v.over).length > 0;
  $("fit").hidden = !overNow;                 // (only when the configuration does not fit: nothing in its place otherwise)
  const u = v.undo, ud = $("undo");
  ud.className = "o-chip" + (u && u.short ? " short" : "");
  ud.textContent = u ? `undo ${fmtKB(u.ring)}` : "";
  ud.title = u ? `UNDO ring ${fmt(u.ring)} B` + (u.keep ? ` (keep at least ${fmt(u.keep)} B)` : "") : "";
  const key = ev.key || ev.cfg_hash || "";
  $("keychip").textContent = key ? `${ev.key ? "key" : "hash"} ${key.slice(0, 10)}` : "key -";
  $("keychip").dataset.tip = ev.key ? `Cloud key ${key} (click: copy)` : key ? `Configuration hash ${key} (the cloud key is computed at build time; click: copy)` : "No key yet";
}
const fmtKB = (n) => `${Math.round(n / 1024)}K`;

function line(cls, head, text, sub = []) {
  return el("p", { class: `o-msg ${cls}` }, head ? el("b", { text: head }) : null,
    el("span", {}, text, sub.map((s) => el("span", { class: "sub", text: s }))));
}

function say(text) { $("msgs").prepend(line("err", "Error", text)); }

function toast(text, cls = "") {
  const t = el("div", { class: "o-toast " + cls, role: "status", text });
  $("toasts").append(t);
  setTimeout(() => t.remove(), 6000);
}

function renderMsgs() {
  const v = S.view, box = $("msgs");
  const m = messagesOf(v, S.reg);
  box.textContent = "";
  for (const e of m.errors) {
    const p = line("err", e.head, e.text, e.sub);
    p.dataset.kind = e.kind;
    box.append(p);
  }
  for (const x of m.experimental) {
    const p = el("p", { class: "o-msg warn", "data-kind": "experimental", "data-key": x.key }, el("b", { text: "Experimental" }), el("span", { text: `${x.label} is experimental` }),
      el("button", { type: "button", class: "o-info", "aria-label": `About ${x.label}`, "aria-haspopup": "dialog", text: "i", onclick: (e) => openItemPop(x.key, e.currentTarget, S.reg.byKey[x.key].experimental ? "" : x.text) }));
    box.append(p);
  }
  const notes = $("notes"), list = $("noteslist");
  list.textContent = "";
  for (const n of m.notes) list.append(el("li", {}, n.labels.length ? el("b", { text: n.labels.join(", ") + ": " }) : null, n.text));
  $("notessum").textContent = `Notes (${m.notes.length})`;
  notes.hidden = !m.notes.length;
}

function itemRow(key, hasKids) {
  const it = S.reg.byKey[key], v = S.view, val = S.values[key] ?? it.default;
  const live = !it.parent || built(S.reg, S.values, it.parent);
  const id = `it-${key}`;
  let ctl;
  if (it.choices.length) {
    ctl = el("select", { id, "aria-labelledby": `lab-${key}`, "aria-describedby": `tags-${key}`, disabled: !S.ready, onchange: (e) => setItem(key, +e.target.value), onfocus: () => select(key) },
      it.choices.map(([cv, lab]) => el("option", { value: cv, selected: cv === val, text: lab })));
    if (!it.choices.some(([cv]) => cv === val)) ctl.prepend(el("option", { value: val, selected: true, text: `${val} (not a choice)` }));
  } else {
    ctl = el("input", { id, type: "checkbox", "aria-labelledby": `lab-${key}`, "aria-describedby": `tags-${key}`, checked: !!val, disabled: !S.ready, onchange: (e) => setItem(key, e.target.checked ? 1 : 0), onfocus: () => select(key) });
  }
  const tags = el("span", { class: "tags", id: `tags-${key}` });
  if (it.experimental) tags.append(el("span", { class: "o-tag " + (val && live ? "warn" : "outline"), text: "experimental" }));
  const over = v ? v.over : {};
  const deltas = v ? deltaOf(key, v.savings, over) : [];
  for (const [r, n] of deltas) {
    const hot = (over[r] || 0) > 0 && n > 0;
    tags.append(el("span", { class: "cost" + (hot ? " hot" : "") + (r === S.sortBy ? " sortby" : ""), text: `${r} ${kDelta(n)}` }));
  }
  if (v && offFits(deltas, over)) tags.append(el("span", { class: "fits", text: "◀ off = fits" }));
  if (S.reg.kits.includes(key) && live && v) {
    const sh = v.kit_shared && v.kit_shared.flash;
    tags.append(el("span", { class: "kit", text: `shares PERC samples${sh ? ` (${Math.round(sh / 1000)} KB)` : ""} with the other sampled kits ` }));
    if (v.last_kits.includes(key)) tags.append(el("b", { text: "last kit: off drops the samples" }));
  }
  const errs = v && v.conflicts[key];
  if (errs && errs.length) tags.append(el("span", { class: "xerr", text: `✗ ${errs[0]}` + (errs.length > 1 ? ` (+${errs.length - 1})` : "") }));
  const lab = el("span", { class: "lab", onclick: () => select(key) }, el("span", { id: `lab-${key}`, text: it.label }), el("span", { class: "k", text: key }), tags);
  if (key === "BLE" && S.room && S.room.note) tags.append(el("span", { class: "bleline", text: S.room.note }));
  const row = el("div", { class: "row" + (live ? "" : " dead") + (S.sel === key ? " sel" : ""), "data-key": key }, ctl, lab);
  if (hasInfo(it)) row.append(el("button", { type: "button", class: "o-info", "aria-label": `About ${it.label}`, "aria-haspopup": "dialog", text: "i", onclick: (e) => openItemPop(key, e.currentTarget) }));
  if (hasKids) {
    const open = !S.collapsed.has(key) || !!S.filter;
    row.classList.add("parent");
    row.prepend(el("button", { type: "button", class: "fold quiet", "data-fold": key, "aria-expanded": open ? "true" : "false", "aria-label": `${open ? "Hide" : "Show"} the options of ${it.label}`,
      text: open ? "▾" : "▸", onclick: (e) => { e.stopPropagation(); toggleFold(key); } }));
    row.addEventListener("click", (e) => { if (!e.target.closest("input, select, button, a")) toggleFold(key); });   // (the row's own space: the whole head opens and closes)
  }
  return row;
}

function toggleFold(id) {
  S.collapsed[S.collapsed.has(id) ? "delete" : "add"](id);
  saveFolds();
  renderTree();
}

function renderTree() {
  const tree = $("tree");
  const ae = document.activeElement, keepFocus = ae && ae.id, keepFold = ae && ae.dataset && ae.dataset.fold;
  tree.textContent = "";
  const v = S.view;
  for (const g of treeOf(S.reg, S.filter, v ? v.savings : {}, S.sortBy)) {
    const gid = "g:" + g.group, open = !S.collapsed.has(gid) || !!S.filter;
    const list = el("ul", { hidden: !open });
    for (const t of g.items) {
      const li = el("li", {}, itemRow(t.key, t.children.length > 0));
      if (t.children.length && (!S.collapsed.has(t.key) || S.filter)) li.append(el("ul", {}, t.children.map((c) => el("li", {}, itemRow(c, false)))));
      list.append(li);
    }
    const head = el("div", { class: "o-panel-h", "data-fold": gid, role: "button", tabindex: "0", "aria-expanded": open ? "true" : "false", "aria-label": `${open ? "Fold" : "Unfold"} ${g.group}`,
      onclick: () => toggleFold(gid), onkeydown: (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); toggleFold(gid); } } },
      el("span", { class: "chev", "aria-hidden": "true", text: open ? "▾" : "▸" }), el("h2", { text: g.group }));
    tree.append(el("div", { class: "group o-panel", "data-group": g.group }, head, list));
  }
  if (!tree.childNodes.length) tree.append(el("p", { class: "o-dim", text: `Nothing matches "${S.filter}".` }));
  if (keepFocus && $(keepFocus) && keepFocus !== "search") $(keepFocus).focus({ preventScroll: true });
  else if (keepFold) { const f = [...tree.querySelectorAll("[data-fold]")].find((x) => x.dataset.fold === keepFold); if (f) f.focus({ preventScroll: true }); }
}

function select(key) {
  if (S.sel === key) return;
  S.sel = key;
  root.querySelectorAll(".row.sel").forEach((r) => r.classList.remove("sel"));
  const row = root.querySelector(`.row[data-key="${key}"]`);
  if (row) row.classList.add("sel");
  renderInfo(key);
}

/** an item's notes (experimental and how it was tested, its notice, where it comes from, what off loses): the (i)
    popover and the details panel both show them, never in red */
function itemNotes(it) {
  const out = [];
  if (it.experimental) out.push(el("p", {}, el("span", { class: "o-tag " + (built(S.reg, S.values, it.key) ? "warn" : "outline"), text: "experimental" }), ` ${it.tested}`));
  if (it.notice) out.push(el("p", { text: it.notice }));
  if (it.provenance_line) {
    out.push(el("p", { class: "src", text: "From " + it.provenance_line }));
    if (it.provenance && it.provenance.url) out.push(el("p", { class: "src" }, el("a", { href: it.provenance.url, target: "_blank", rel: "noopener", text: it.provenance.url })));
  }
  if (it.off_warning) out.push(el("p", { class: "src", text: "Off: " + it.off_warning }));
  return out;
}

const hasPopover = typeof HTMLElement !== "undefined" && "showPopover" in HTMLElement.prototype;
function openItemPop(key, anchor, extra = "") {
  const it = S.reg.byKey[key];
  showPop(key, [el("h3", { id: "poph", text: it.label }), it.desc ? el("p", { class: "src", text: it.desc }) : null,
    extra ? el("p", {}, el("span", { class: "o-tag warn", text: "experimental" }), ` ${extra}`) : null, ...itemNotes(it)], anchor);
}
function openPop(title, paras, anchor) {
  showPop("", [el("h3", { id: "poph", text: title }), ...paras.map((t) => el("p", { text: t }))], anchor);
}
/** the one popover (the browser's top layer: light dismiss, Esc), placed by its anchor, below it or above it */
function showPop(key, kids, anchor) {
  const pop = $("bpop");
  pop.replaceChildren(...kids.filter(Boolean));
  pop.dataset.key = key;
  if (hasPopover) { if (pop.matches(":popover-open")) pop.hidePopover(); pop.showPopover(); } else pop.hidden = false;
  const a = anchor.getBoundingClientRect(), w = pop.offsetWidth, h = pop.offsetHeight;
  const left = Math.max(16, Math.min(a.right - w + 12, window.innerWidth - w - 16));
  const below = a.bottom + 6 + h <= window.innerHeight - 8 || a.top - 6 - h < 8;
  pop.style.left = `${left}px`;
  pop.style.top = `${below ? a.bottom + 6 : a.top - 6 - h}px`;
}

async function renderInfo(key) {
  const it = S.reg.byKey[key], info = $("infob");
  if (S.ready && !S.info[key]) S.info[key] = await call("item_info", key);
  if (S.sel !== key) return;
  const d = S.info[key];
  info.textContent = "";
  info.append(el("h3", { text: it.label }),
    el("p", { class: "meta", text: `${key}` + (it.flag ? `  (${it.flag})` : "") + `  bit ${it.bit}` + (it.parent ? ` · option of ${S.reg.byKey[it.parent].label}` : "") }));
  if (it.desc) info.append(el("p", { text: it.desc }));
  for (const e of (S.view && S.view.conflicts[key]) || []) info.append(line("err", "Error", e));
  if (d) {
    const rows = d.values.map(([lab, , dd]) => dd && el("tr", {}, el("td", { text: lab }), REGIONS.map((r) => el("td", { text: (dd[r] >= 0 ? "+" : "") + fmt(dd[r]) }))));
    const pairs = d.pairs.map(([txt, dd]) => el("tr", {}, el("td", { text: `${txt} adds` }), REGIONS.map((r) => el("td", { text: (dd[r] >= 0 ? "+" : "") + fmt(dd[r]) }))));
    if (rows.some(Boolean) || pairs.length) {
      info.append(el("div", { class: "tablewrap" }, el("table", {}, el("caption", { class: "o-sr", text: "bytes against the default build" }),
        el("thead", {}, el("tr", {}, el("th", { text: "alone vs the defaults" }), REGIONS.map((r) => el("th", { text: r })))),
        el("tbody", {}, rows.filter(Boolean), pairs))));
    }
  }
  const notes = itemNotes(it);
  if (notes.length) info.append(el("div", { class: "notes" }, notes));
}

function renderResult() {
  const ev = S.view.eval;
  $("hash").textContent = ev.cfg_hash || "-";
  $("key").textContent = ev.key || "-";
  $("key").title = ev.key || "";
  $("keynote").textContent = ev.key ? `The key of this configuration at the site's commit ${S.commit.slice(0, 12)} (sha256 of "${S.reg.key_tag}", the commit and the canonical form).`
    : "The cloud key will be computed at build time (this page does not know the commit it would be built from).";
  $("canon").textContent = ev.canonical || "";
  $("dump").textContent = S.view.dump;
  $("download").disabled = !S.ready;
  $("download").title = ev.errors.length ? "This configuration has errors: a build refuses it (download it anyway to fix it in the menu)" : "";
}

/* ---- the controls */
async function copyKey() {
  const ev = S.view && S.view.eval, key = ev && (ev.key || ev.cfg_hash);
  if (!key) return;
  try { await navigator.clipboard.writeText(key); toast(`Copied the ${ev.key ? "cloud key" : "configuration hash"}`, "ok"); }
  catch (e) { toast(`${ev.key ? "Cloud key" : "Configuration hash"}: ${key}`); }   // (no clipboard here: shown instead)
}

function download() {
  const blob = new Blob([S.view.dump], { type: "text/plain" });
  const a = el("a", { href: URL.createObjectURL(blob), download: fileName(S.name) });
  root.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}

/* ---- ways to fit: up to 4 proposals (core.proposals), each Accept / Reject, a Preview of its result on the gauges */
async function syncProposals() {
  const over = Object.keys(S.view.over).length > 0;
  if (!over) { Object.assign(S, { fitOpen: false, proposals: null, preview: null }); S.rejected.clear(); renderProposals(); renderBars(); return; }
  if (S.fitOpen) await loadProposals();
  else renderProposals();
}

async function loadProposals() {
  const seq = ++S.pseq;
  const list = await call("proposals_view", S.values);
  if (seq !== S.pseq) return;
  S.proposals = list;
  if (S.preview && !list.some((p) => p.id === S.preview)) S.preview = null;
  renderProposals();
  renderBars();
}

function setPreview(id) {
  if (S.preview === id) return;
  S.preview = id;
  renderBars();
  root.querySelectorAll(".prop .acts [data-act=preview]").forEach((b) => b.setAttribute("aria-pressed", String(b.closest(".prop").dataset.id === id)));
}

function renderProposals() {
  const box = $("proposals"), body = $("propsb");
  box.hidden = !(S.fitOpen && S.view && Object.keys(S.view.over).length);
  if (box.hidden) { body.textContent = ""; return; }
  body.textContent = "";
  const all = S.proposals;
  if (all === null) { body.append(el("p", { class: "o-dim", text: "Looking for ways to fit..." })); return; }
  const list = all.filter((p) => !S.rejected.has(p.id));
  if (!all.length) body.append(el("p", { class: "o-msg", text: "Nothing to propose: the estimate shows nothing over." }));
  else if (!list.length) body.append(el("p", { class: "o-dim", text: "All the proposals are dismissed. Make it fit shows them again." }));
  else if (!list.some((p) => p.fits)) body.append(el("p", { class: "o-msg" }, el("b", { text: "No fit" }), el("span", { text: "Nothing fits by dropping what the builder may drop: these come closest. Switch more off by hand." })));
  for (const p of list) body.append(proposalCard(p));
}

function proposalCard(p) {
  const rows = REGIONS.map((r) => {
    const f = p.after[r], m = gaugeModel(r, f.used, f.capacity, f.over);
    return el("span", { "data-region": r, "data-state": m.state, title: m.tip }, `${r.toUpperCase()} ${m.text}`, m.overText ? el("span", { class: "go", text: ` ${m.overText}` }) : null);
  });
  const left = REGIONS.filter((r) => p.after[r].over > 0).map((r) => `${r.toUpperCase()} +${fmtK(p.after[r].over)}`);
  const card = el("article", { class: "prop", "data-id": p.id, "data-fits": String(p.fits), onmouseenter: () => setPreview(p.id), onmouseleave: () => { if (!card.dataset.pinned) setPreview(null); } },
    el("h3", { text: p.title }),
    el("p", { class: "o-small o-dim", text: p.text }),
    el("ul", { class: "chg" }, p.changes.map((c) => el("li", { "data-key": c.key }, el("b", { text: changeText(c, S.reg.byKey) }), c.desc ? el("span", { class: "o-dim", text: `: ${oneLine(c.desc)}` }) : null))),
    el("p", { class: "verdict" + (p.fits ? " ok" : ""), text: p.fits ? "Fits by the estimate." : `Still over: ${left.join(", ")}.` }),
    el("div", { class: "res" }, rows),
    el("div", { class: "acts" },
      el("button", { type: "button", class: "primary sm", "data-act": "accept", text: "Accept", onclick: () => acceptProposal(p) }),
      el("button", { type: "button", class: "sm", "data-act": "preview", "aria-pressed": String(S.preview === p.id), text: "Preview",
        onclick: () => { if (card.dataset.pinned) { delete card.dataset.pinned; setPreview(null); } else { root.querySelectorAll(".prop[data-pinned]").forEach((c) => delete c.dataset.pinned); card.dataset.pinned = "1"; setPreview(p.id); } } }),
      el("button", { type: "button", class: "quiet sm", "data-act": "reject", text: "Reject", onclick: () => { S.rejected.add(p.id); if (S.preview === p.id) S.preview = null; renderProposals(); renderBars(); } })));
  return card;
}

async function acceptProposal(p) {
  const before = { values: S.values, room: S.room };
  S.values = p.values;
  S.room = null;
  S.fit = null;
  S.preview = null;
  await refresh();
  const t = el("div", { class: "o-toast ok", role: "status" }, `Applied "${p.title}": ${p.changes.length} change${p.changes.length === 1 ? "" : "s"}. `,
    el("button", { type: "button", class: "quiet sm", "data-act": "undo", text: "Undo", onclick: async () => { t.remove(); S.values = before.values; S.room = before.room; S.fit = null; await refresh(); } }));
  $("toasts").append(t);
  setTimeout(() => t.remove(), 15000);
}

async function showProposals() {
  S.fitOpen = true;
  S.rejected.clear();
  S.proposals = null;
  renderProposals();
  await loadProposals();
}

async function importText(text, source) {
  const msg = $("importmsg");
  msg.textContent = "";
  const r = await loadText(text, "custom", source);
  msg.textContent = r.ok ? `Loaded ${source}` + (r.warnings.length ? ` (${r.warnings.length} warning${r.warnings.length > 1 ? "s" : ""}: ${r.warnings.join("; ")})` : "") : r.message;
  msg.style.color = r.ok ? "" : "var(--err)";
  if (r.ok) { $("bpreset").value = ""; $("presetdesc").textContent = ""; }
}

function wire() {
  $("bpreset").addEventListener("change", (e) => choosePreset(e.target.value));
  $("name").addEventListener("change", async (e) => { S.name = e.target.value.trim() || "custom"; await refresh(); });
  $("search").addEventListener("input", (e) => { S.filter = e.target.value.trim(); renderTree(); });
  $("search").addEventListener("keydown", (e) => { if (e.key === "Escape") { e.target.value = ""; S.filter = ""; renderTree(); } });
  $("sort").addEventListener("change", (e) => { S.sortBy = e.target.value || null; renderTree(); });
  $("expand").addEventListener("click", () => { S.collapsed.clear(); saveFolds(); renderTree(); });
  $("collapse").addEventListener("click", () => { for (const it of S.reg.items) if (it.children.length) S.collapsed.add(it.key); saveFolds(); renderTree(); });
  $("download").addEventListener("click", download);
  $("reset").addEventListener("click", () => choosePreset(S.default.id));
  $("propsclose").addEventListener("click", () => { S.fitOpen = false; S.preview = null; renderProposals(); renderBars(); });
  $("keychip").addEventListener("click", copyKey);
  $("how").addEventListener("click", (e) => openPop("The size estimate", [howText(), "Each gauge: used / limit in KiB (1K = 1,024 B), the exact bytes on hover; past the limit the bar turns red and the overflow shows after the numbers."], e.currentTarget));
  if (!hasPopover) $("bpop").hidden = true;   // (an older browser: the popover by hand, closed by a click elsewhere or Esc: onDocClick / onKey, while active)
  $("fit").addEventListener("click", showProposals);
  $("bfile").addEventListener("change", async (e) => {
    const f = e.target.files[0];
    if (!f) return;
    $("filename").textContent = f.name;
    await importText(await f.text(), f.name);
    e.target.value = "";
  });
  $("pasteload").addEventListener("click", () => importText($("paste").value, "the pasted text"));
  root.addEventListener("click", (e) => {      // (a link to another tab of the host: data-nav="installer")
    const nav = e.target.closest("[data-nav]");
    if (nav && opts.onNavigate) { e.preventDefault(); opts.onNavigate(nav.dataset.nav); }
  });
}

/** the menu's keys, outside the text fields: only while the tab is active (activate / deactivate) */
function onKey(e) {
  if (!hasPopover && e.key === "Escape") $("bpop").hidden = true;
  {
    if (e.ctrlKey || e.metaKey || e.altKey || /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
    if (e.key === "/") { e.preventDefault(); $("search").focus(); }
    else if (e.key === "o") { const i = SORTS.indexOf(S.sortBy); S.sortBy = SORTS[(i + 1) % SORTS.length]; $("sort").value = S.sortBy || ""; renderTree(); }
    else if (e.key === "x") $("expand").click();
    else if (e.key === "c") $("collapse").click();
  }
}
function onDocClick(e) { if (!hasPopover && !e.target.closest("#bpop, .o-info")) $("bpop").hidden = true; }

async function start() {
  const [desc, prof, info] = await Promise.all([fetch(new URL("./data/registry.json", import.meta.url)).then((r) => r.json()), fetch(new URL("./data/profiles.json", import.meta.url)).then((r) => r.json()),
    fetch(new URL("./build-info.json", import.meta.url)).then((r) => (r.ok ? r.json() : null)).catch(() => null)]);
  S.reg = indexRegistry(desc);
  S.prof = prof;
  S.commit = commitOf(info);
  S.presets = presets(prof);
  S.values = defaults(S.reg);
  const sel = $("bpreset");
  const flav = S.presets.filter((p) => p.kind === "flavour"), profs = S.presets.filter((p) => p.kind !== "flavour");
  sel.append(el("option", { value: "", text: "(your own)" }));
  const last = recall();
  if (last) {
    S.presets.unshift({ id: "last", label: "your last configuration (this browser)", kind: "last", text: last, name: "custom", description: "" });
    sel.append(el("option", { value: "last", text: "your last configuration (this browser)" }));
  }
  S.default = defaultPreset(prof, S.presets);
  $("reset").textContent = `Reset to default (${S.default.label})`;
  if (flav.length) sel.append(el("optgroup", { label: "Flavours (the site's packages)" }, flav.map((p) => el("option", { value: p.id, text: p.label }))));
  sel.append(el("optgroup", { label: "Profiles (config/profiles)" }, profs.map((p) => el("option", { value: p.id, text: p.label }))));
  wire();
  renderTree();
  startPython();
  try {
    const ready = await pyReady;
    $("pyver").textContent = `${PYODIDE_VERSION} (Python ${ready.python})`;
    S.python = ready;
  } catch (e) {
    const box = $("loading");
    box.classList.add("failed");
    $("loadtext").textContent = `Python did not start: ${e.message}. The list is shown, but nothing can be checked. Reload the page, or use the builder from a checkout (make builder).`;
    $("loadbar").hidden = true;
    return;
  }
  $("loading").hidden = true;
  S.ready = true;
  $("fit").disabled = false;
  $("reset").disabled = false;
  await choosePreset(S.default.id);           // (always the manifest's default: the last configuration is a choice)
  handle.readyFlag = true;
}

const handle = {
  root, state: S, ready: pyReady, readyFlag: false, choosePreset, importText, setItem,
  call: (...a) => pyReady.then(() => call(...a)),
  /** the tab is shown: keyboard shortcuts on, Python started (the first time) */
  activate() {
    if (!active) { active = true; document.addEventListener("keydown", onKey); document.addEventListener("click", onDocClick); }
    if (!started) { started = true; start().catch((e) => { $("loadtext").textContent = `The builder did not load: ${e.message}`; $("loading").classList.add("failed"); }); }
  },
  /** the tab is hidden: the shortcuts off (the page, the worker and the state stay) */
  deactivate() { active = false; document.removeEventListener("keydown", onKey); document.removeEventListener("click", onDocClick); },
  /** gone: the shortcuts off, the worker ended, the container emptied */
  destroy() { handle.deactivate(); if (worker) worker.terminate(); root.replaceChildren(); root.classList.remove("ob-root", "o-ui"); },
};
return handle;
}
