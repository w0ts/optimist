// SPDX-License-Identifier: GPL-3.0-only
// The web builder's page: the list, the bars, the messages and the details as the menu (tools/builder/menu.py) shows
// them; every rule runs in Python (py-worker.js -> tools/builder/web_api.py). docs/BUILDER-WEB.md.
import {
  PYODIDE_VERSION, REGIONS, SORTS, barModel, built, commitOf, defaultPreset, defaults, deltaOf, fileName, fmt,
  gaugeModel, hasInfo, indexRegistry, kDelta, messagesOf, offFits, overflowLines, presets, pyodideBase, treeOf,
} from "./logic.js";

const $ = (id) => document.getElementById(id);
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
const REMEMBER = "optimist-builder-last";        // (only a convenience: the page works without storage)
const remember = (text) => { try { localStorage.setItem(REMEMBER, JSON.stringify({ text })); } catch (e) { /* no storage: fine */ } };
const recall = () => { try { const v = JSON.parse(localStorage.getItem(REMEMBER) || "null"); return v && typeof v.text === "string" ? v.text : null; } catch (e) { return null; } };

const S = {
  reg: null, prof: null, presets: [], values: {}, name: "", room: null, sel: null, filter: "", sortBy: null,
  stages: [], collapsed: new Set(), view: null, info: {}, commit: null, ready: false, seq: 0, shown: 0, fit: null,
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
  worker.postMessage({ boot: { base: pyodideBase(window.OPTIMIST_PYODIDE_BASE), data: new URL("./data/", import.meta.url).href } });
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
  $("preset").value = id;
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
  for (const r of REGIONS) {
    const f = ev.fits[r];
    const m = gaugeModel(r, f.used, f.capacity, f.over);
    over = over || m.state === "over";
    const bar = el("div", { class: "gb" }, el("div", { class: "gf", style: `width: ${(m.fill * 100).toFixed(1)}%` }));
    if (m.capAt !== null) bar.append(el("div", { class: "gx", style: `left: ${(m.capAt * 100).toFixed(1)}%; right: 0` }), el("div", { class: "gc", style: `left: calc(${(m.capAt * 100).toFixed(1)}% - 1px)` }));
    bars.append(el("div", { class: "o-gauge", "data-region": r, "data-state": m.state, title: m.tip, role: "img", "aria-label": m.tip },
      el("span", { class: "gl", text: r.toUpperCase() }),
      el("span", { class: "gn" }, el("span", { class: "gu", text: m.text }), m.overText ? el("span", { class: "go", text: m.overText }) : null), bar));
  }
  $("fit").classList.toggle("primary", over);
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
    row.append(el("button", { type: "button", class: "fold quiet", "aria-expanded": open ? "true" : "false", "aria-label": `${open ? "Hide" : "Show"} the options of ${it.label}`,
      text: open ? "−" : "+", onclick: () => { S.collapsed[open ? "add" : "delete"](key); renderTree(); } }));
  }
  return row;
}

function renderTree() {
  const tree = $("tree");
  const keepFocus = document.activeElement && document.activeElement.id;
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
    tree.append(el("div", { class: "group o-panel", "data-group": g.group },
      el("div", { class: "o-panel-h" }, el("h2", { text: g.group }),
        el("button", { type: "button", class: "quiet sm", "aria-expanded": open ? "true" : "false", "aria-label": `${open ? "Fold" : "Unfold"} ${g.group}`, text: open ? "fold" : "unfold",
          onclick: () => { S.collapsed[open ? "add" : "delete"](gid); renderTree(); } })), list));
  }
  if (!tree.childNodes.length) tree.append(el("p", { class: "o-dim", text: `Nothing matches "${S.filter}".` }));
  if (keepFocus && $(keepFocus) && keepFocus !== "search") $(keepFocus).focus({ preventScroll: true });
  placeInfo();
}

const narrow = window.matchMedia("(max-width: 860px)");
function placeInfo() {
  const info = $("info");
  const row = S.sel && document.querySelector(`.row[data-key="${S.sel}"]`);
  if (narrow.matches && row) row.after(info);
  else if (info.parentElement !== document.querySelector(".cols")) document.querySelector(".cols").append(info);
}
narrow.addEventListener("change", placeInfo);

function select(key) {
  if (S.sel === key) return;
  S.sel = key;
  document.querySelectorAll(".row.sel").forEach((r) => r.classList.remove("sel"));
  const row = document.querySelector(`.row[data-key="${key}"]`);
  if (row) row.classList.add("sel");
  placeInfo();
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
  const pop = $("pop");
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
  document.body.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}

async function runFit() {
  const r = await call("fit_view", S.values);
  const box = $("fitmsg");
  box.textContent = "";
  $("fitbox").hidden = false;
  if (!r.values) { box.append(`Leaving out the builder's fixed order is not enough: switch more off by hand${r.changes.length ? ` (it tried ${r.changes.join(", ")})` : ""}.`); return; }
  if (!r.changes.length) { box.append("It fits by the estimate, with the builder's margin: nothing to change."); return; }
  box.append(`To fit: ${r.changes.join(", ")} `, el("button", { type: "button", id: "fitapply", class: "primary sm", text: "Apply", onclick: async () => { S.values = r.values; S.room = null; box.textContent = "Applied: " + r.changes.join(", "); await refresh(); } }));
}

async function importText(text, source) {
  const msg = $("importmsg");
  msg.textContent = "";
  const r = await loadText(text, "custom", source);
  msg.textContent = r.ok ? `Loaded ${source}` + (r.warnings.length ? ` (${r.warnings.length} warning${r.warnings.length > 1 ? "s" : ""}: ${r.warnings.join("; ")})` : "") : r.message;
  msg.style.color = r.ok ? "" : "var(--err)";
  if (r.ok) { $("preset").value = ""; $("presetdesc").textContent = ""; }
}

function wire() {
  $("preset").addEventListener("change", (e) => choosePreset(e.target.value));
  $("name").addEventListener("change", async (e) => { S.name = e.target.value.trim() || "custom"; await refresh(); });
  $("search").addEventListener("input", (e) => { S.filter = e.target.value.trim(); renderTree(); });
  $("search").addEventListener("keydown", (e) => { if (e.key === "Escape") { e.target.value = ""; S.filter = ""; renderTree(); } });
  $("sort").addEventListener("change", (e) => { S.sortBy = e.target.value || null; renderTree(); });
  $("expand").addEventListener("click", () => { S.collapsed.clear(); renderTree(); });
  $("collapse").addEventListener("click", () => { for (const it of S.reg.items) if (it.children.length) S.collapsed.add(it.key); renderTree(); });
  $("download").addEventListener("click", download);
  $("reset").addEventListener("click", () => choosePreset(S.default.id));
  $("fitclose").addEventListener("click", () => { $("fitmsg").textContent = ""; $("fitbox").hidden = true; });
  $("keychip").addEventListener("click", copyKey);
  $("how").addEventListener("click", (e) => openPop("The size estimate", [howText(), "Each gauge: used / limit in KiB (1K = 1,024 B), the exact bytes on hover; past the limit the bar turns red and the overflow shows after the numbers."], e.currentTarget));
  if (!hasPopover) {         // (an older browser: the popover by hand, closed by a click elsewhere or Esc)
    $("pop").hidden = true;
    document.addEventListener("click", (e) => { if (!e.target.closest("#pop, .o-info")) $("pop").hidden = true; });
    document.addEventListener("keydown", (e) => { if (e.key === "Escape") $("pop").hidden = true; });
  }
  new ResizeObserver(() => document.documentElement.style.setProperty("--gauges-h", `${$("gauges").offsetHeight}px`)).observe($("gauges"));
  $("fit").addEventListener("click", runFit);
  $("file").addEventListener("change", async (e) => {
    const f = e.target.files[0];
    if (!f) return;
    $("filename").textContent = f.name;
    await importText(await f.text(), f.name);
    e.target.value = "";
  });
  $("pasteload").addEventListener("click", () => importText($("paste").value, "the pasted text"));
  document.addEventListener("keydown", (e) => {     // (the menu's keys, outside the text fields)
    if (e.ctrlKey || e.metaKey || e.altKey || /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
    if (e.key === "/") { e.preventDefault(); $("search").focus(); }
    else if (e.key === "o") { const i = SORTS.indexOf(S.sortBy); S.sortBy = SORTS[(i + 1) % SORTS.length]; $("sort").value = S.sortBy || ""; renderTree(); }
    else if (e.key === "x") $("expand").click();
    else if (e.key === "c") $("collapse").click();
  });
}

async function main() {
  const [desc, prof, info] = await Promise.all([fetch("data/registry.json").then((r) => r.json()), fetch("data/profiles.json").then((r) => r.json()),
    fetch("build-info.json").then((r) => (r.ok ? r.json() : null)).catch(() => null)]);
  S.reg = indexRegistry(desc);
  S.prof = prof;
  S.commit = commitOf(info);
  S.presets = presets(prof);
  S.values = defaults(S.reg);
  const sel = $("preset");
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
  window.optimistBuilder.readyFlag = true;
}

window.optimistBuilder = { call: (...a) => pyReady.then(() => call(...a)), state: S, ready: pyReady, readyFlag: false, choosePreset, importText, setItem };
main().catch((e) => { $("loadtext").textContent = `The builder did not load: ${e.message}`; $("loading").classList.add("failed"); });
