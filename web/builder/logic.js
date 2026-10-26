// SPDX-License-Identifier: GPL-3.0-only
// The web builder's pure functions (no DOM, no Python): what the page lists and how, as the menu (tools/builder/menu.py)
// does it. The rules themselves (validate, the budget, fit, the key) run in Python: py-worker.js, tools/builder/web_api.py.
// node web/test_builder.mjs tests these.

export const PYODIDE_VERSION = "314.0.7";            // pinned (released 2026-09-14); docs/WEB.md
export const PYODIDE_CDN = `https://cdn.jsdelivr.net/pyodide/v${PYODIDE_VERSION}/full/`;
export const REGIONS = ["flash", "ram", "pool", "ramtext"];
export const SORTS = [null, ...REGIONS];             // the menu's o: default order, then each region, largest first
export const OVER_W = 20;                             // menu.py: cells for the overflow, past the capacity mark
export const SHOW_MIN = 64;                           // menu.py delta_of: the regions an item moves by 64 B or more

/** the Pyodide files' base: the pinned CDN, or a same-origin path a test sets (window.OPTIMIST_PYODIDE_BASE) */
export function pyodideBase(override) {
  if (typeof override === "string" && /^\.?\/[\w./-]*\/$/.test(override) && !override.includes("..")) return override;
  return PYODIDE_CDN;
}

/** 1234567 -> "1,234,567" (the menu's {:,}) */
export const fmt = (n) => Math.round(n).toLocaleString("en-US");

/** bytes per overflow cell: 1 KiB, or coarser so that the overflow fits OVER_W cells (menu.py over_unit) */
export function overUnit(over) {
  let unit = 1024;
  while (over > unit * OVER_W) unit *= 2;
  return unit;
}

/** one region's bar as the menu draws it: {region, used, cap, over, frac, state: ok | tight | over, text} */
export function barModel(region, used, cap, over) {
  if (over > 0) {
    const unit = overUnit(over);
    return { region, used, cap, over, frac: 1, state: "over", cells: Math.max(1, Math.ceil(over / unit)), unit,
      text: `OVER by ${fmt(over)} B`, detail: `(${fmt(used)} / ${fmt(cap)}; one red cell = ${unit / 1024} KB)` };
  }
  const frac = cap ? used / cap : 0;
  return { region, used, cap, over, frac, state: frac > 0.97 ? "tight" : "ok", cells: 0, unit: 0,
    text: `${fmt(used)} / ${fmt(cap)} B`, detail: `${fmt(-over)} free` };
}

/** "+1.2K" as the menu's {n / 1024:+.1f}K */
export function kDelta(n) {
  const v = n / 1024;
  return `${v >= 0 ? "+" : "-"}${Math.abs(v).toFixed(1)}K`;
}

/** what the item costs as set now, per region, as the menu lists it (delta_of): [[region, bytes]] for the regions it
    moves by 64 B or more and every overflowing one it takes; [] when not measured or not built */
export function deltaOf(key, savings, over) {
  const s = (savings || {})[key];
  if (!s) return [];
  return REGIONS.filter((r) => Math.abs(s[r]) >= SHOW_MIN || ((over || {})[r] > 0 && s[r] > 0)).map((r) => [r, s[r]]);
}

/** switching the item off alone makes every overflowing region fit (the menu's "◀ off = fits") */
export function offFits(deltas, over) {
  const o = Object.entries(over || {});
  if (!o.length) return false;
  const took = Object.fromEntries(deltas);
  return o.every(([r, n]) => (took[r] || 0) >= n);
}

/** the search: label, key, group and description (menu.py matches) */
export function matches(item, filter) {
  if (!filter) return true;
  const hay = `${item.key} ${item.label} ${item.group} ${item.desc}`.toLowerCase();
  return hay.includes(filter.toLowerCase());
}

/** keys in the active sort: the largest use of the region first (what the item costs as set now), the unmeasured
    last, ties and the default sort in the registry order (menu.py sorted_keys) */
export function sortedKeys(keys, savings, sortBy) {
  const list = [...keys];
  if (!sortBy) return list;
  const cost = (k) => { const s = (savings || {})[k]; return s && s[sortBy] !== undefined ? s[sortBy] : null; };
  return list.map((k, i) => [k, i]).sort((a, b) => {
    const ca = cost(a[0]), cb = cost(b[0]);
    if ((ca === null) !== (cb === null)) return ca === null ? 1 : -1;
    return (-(ca || 0)) - (-(cb || 0)) || a[1] - b[1];
  }).map(([k]) => k);
}

/** the list as the menu builds it (rebuild): [{group, items: [{key, children: [key]}]}], groups without a match
    left out, the children of a matching parent all shown */
export function treeOf(reg, filter, savings, sortBy) {
  const items = reg.byKey;
  const out = [];
  for (const g of reg.groups) {
    const top = (reg.top[g] || []).filter((k) => matches(items[k], filter) || items[k].children.some((c) => matches(items[c], filter)));
    if (!top.length) continue;
    out.push({ group: g, items: sortedKeys(top, savings, sortBy).map((k) => ({
      key: k,
      children: sortedKeys(items[k].children, savings, sortBy).filter((c) => matches(items[c], filter) || matches(items[k], filter)),
    })) });
  }
  return out;
}

/** registry.json -> the registry with byKey */
export function indexRegistry(desc) {
  const byKey = {};
  for (const it of desc.registry.items) byKey[it.key] = it;
  return { groups: desc.registry.groups, top: desc.top, byKey, items: desc.registry.items, limits: desc.limits,
    spare: desc.spare, kits: desc.kits || [] };
}

/** the item is in the build: on (a choice: non-zero) and its parent on (core.built) */
export function built(reg, values, key) {
  const it = reg.byKey[key];
  if (it.parent && !built(reg, values, it.parent)) return false;
  return (values[key] ?? it.default) !== 0;
}

/** the defaults of every item */
export const defaults = (reg) => Object.fromEntries(reg.items.map((it) => [it.key, it.default]));

/** a file name for the download: the configuration's name, plain */
export function fileName(name) {
  const s = String(name || "").trim().replace(/[^A-Za-z0-9._-]+/g, "-").replace(/^[-.]+|[-.]+$/g, "").slice(0, 40);
  return (s || "optimist") + ".config";
}

/** the name the firmware keeps (core.header: [A-Za-z0-9 +._-], 16 characters) */
export const firmwareName = (name) => String(name || "").replace(/[^A-Za-z0-9 +._-]/g, "").slice(0, 16);

/** a commit the cloud key can take (core.cloud_key: a full lower-case hex SHA), else null */
export const commitOf = (info) => (info && typeof info.commit === "string" && /^([0-9a-f]{40}|[0-9a-f]{64})$/.test(info.commit) ? info.commit : null);

/** the GitHub repository cloud builds are asked of (build-info.json "repo", which the site's workflow writes) */
export const DEFAULT_REPO = "w0ts/optimist";
/** the .config of a cloud build on the cloud-builds branch (tools/cloud_build.py shelf; the installer links here with
 * ?cloud=<stem>), else null for anything that is not a build's stem */
export const cloudConfigUrl = (repo, stem) => (/^optimist-[a-z0-9-]{1,32}-[0-9a-f]{16}$/.test(stem || "")
  ? `https://raw.githubusercontent.com/${repo}/cloud-builds/${stem}.config` : null);
/** the version the builder reads ("Build from"): MAIN, the site's own copy (main as deployed), or a commit */
export const MAIN_REF = "main";
/** site.json's latest published release ({tag, sha, date}, web/make_site.py latest_of), well formed, else null */
export function latestOf(site) {
  const l = site && site.latest;
  return l && typeof l.tag === "string" && /^v[0-9A-Za-z._-]{1,32}$/.test(l.tag) && typeof l.sha === "string" && /^[0-9a-f]{40}$/.test(l.sha)
    ? { tag: l.tag, sha: l.sha, date: typeof l.date === "string" ? l.date : "" } : null;
}
/** where the builder starts (the user, 2026-10-26: "by default it should pick the last release tag, not main"): asked, the
    page's ?ref (or opts.ref): "main" or a commit; else the latest release; else main. -> {ref: the commit to read the data
    of (null: the site's own copy), value: the "Build from" choice (MAIN_REF or the commit)} */
export function pickRef(asked, latest) {
  if (asked === MAIN_REF) return { ref: null, value: MAIN_REF };
  if (/^[0-9a-f]{40}$/.test(asked || "")) return { ref: asked, value: asked };
  return latest ? { ref: latest.sha, value: latest.sha } : { ref: null, value: MAIN_REF };
}
/** the page's URL for another "Build from" choice: ?ref=main, ?ref=<commit>, or none for the latest release (the default) */
export function refUrl(href, value, latest) {
  const u = new URL(href);
  if (latest && value === latest.sha) u.searchParams.delete("ref"); else u.searchParams.set("ref", value);
  return u.href;
}
/** the commit a cloud build asks for: the one chosen; main: the site's own commit (main as deployed; "" unknown: the relay's main) */
export const buildCommit = (value, siteCommit) => (value === MAIN_REF ? siteCommit || "" : /^[0-9a-f]{40}$/.test(value || "") ? value : "");

export const repoOf =(info) => (info && typeof info.repo === "string" && /^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$/.test(info.repo) ? info.repo : DEFAULT_REPO);

/** a cloud build's release assets: <stem>.fwsc and <stem>-ui.zip (tools/cloud_build.py asset_stem says the same) */
export const CLOUD_RELEASE = "cloud-builds";
export function assetStem(name, key) {
  const s = String(name || "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 32).replace(/-+$/, "");
  return `optimist-${s || "custom"}-${key.slice(0, 16)}`;
}

/** the new-issue URL of the cloud-build form (.github/ISSUE_TEMPLATE/cloud-build.yml) filled with the commit (empty:
    the latest) and the .config text */
export function cloudIssueUrl(repo, commit, text, name) {
  const q = new URLSearchParams({ template: "cloud-build.yml", title: `Cloud build: ${firmwareName(name) || "custom"}`, commit: commit || "", config: text });
  return `https://github.com/${repo}/issues/new?${q}`;
}

/** the relay that opens the issue for anyone (tools/cloud_relay/cloud-build.php): no GitHub account needed. Its bot check
    is Cloudflare Turnstile: the site key below (public by design; empty = no relay, the issue form only) */
export const CLOUD_RELAY = "https://motscousus.com/optimist/cloud-build.php";
export const TURNSTILE_SITEKEY = "0x4AAAAAAFk1oyVlHqX0qHcQ";
export const TURNSTILE_JS = "https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit";
/** the relay's request: the same commit, configuration and title as the issue form, and the Turnstile token */
export const relayRequest = (token, commit, text, name) => ({ turnstile: token, commit: commit || "", config: text, name: firmwareName(name) || "custom" });

/** the presets: the flavours first (each its profile), then the published profiles (a development one, bt-debug or
    fonts-test, is not offered on the public page; nor the registry's defaults, no configuration of anyone's) */
export function presets(prof) {
  const byName = Object.fromEntries((prof.profiles || []).map((p) => [p.name, p]));
  const out = [];
  for (const f of prof.flavours || []) {
    if (byName[f.profile]) out.push({ id: `flavour:${f.id}`, label: `${f.title} (${f.profile})`, kind: "flavour", text: byName[f.profile].text, name: f.profile, description: f.description || "" });
  }
  for (const p of (prof.profiles || []).filter((x) => x.published === true)) out.push({ id: `profile:${p.name}`, label: p.name, kind: "profile", text: p.text, name: p.name, description: "" });
  return out;
}

/** the saved configurations as stored ({name: .config text}), the well-formed ones only: a name of 1-40 characters, a
    text of at most 20,000; 100 at most */
export function savedOf(v) {
  if (!v || typeof v !== "object" || Array.isArray(v)) return {};
  return Object.fromEntries(Object.entries(v).filter(([n, t]) => n.trim() && n.length <= 40 && typeof t === "string" && t.length <= 20000).slice(0, 100));
}

/** the preset the page opens with and "Reset to default" restores: the manifest's flavour marked "default": true, else
    its first flavour, else the first profile (presets() order) */
export function defaultPreset(prof, list = presets(prof)) {
  const flav = (prof.flavours || []).find((f) => f.default === true) || (prof.flavours || [])[0];
  return (flav && list.find((p) => p.id === `flavour:${flav.id}`)) || list.find((p) => p.kind === "profile") || list.at(-1);
}

/** the summary line of the messages (menu.py refresh_msgs): the overflow per region with the biggest items in it */
export function overflowLines(over, savings, byKey) {
  return Object.entries(over || {}).map(([r, o]) => {
    const best = Object.entries(savings || {}).filter(([, s]) => s[r] > 0).map(([k, s]) => [s[r], k])
      .sort((a, b) => b[0] - a[0] || (a[1] < b[1] ? 1 : -1)).slice(0, 6);
    return { region: r, over: o, text: `${r.toUpperCase()} overflows by ${fmt(o)} B.`,
      biggest: `Biggest ${r} items: ` + best.map(([n, k]) => `${byKey[k].label} ${fmt(n)}`).join(", ") };
  });
}

/** 1234567 -> "1,205.6K" (KiB, one decimal: the gauges' compact numbers; the exact bytes go in their tooltip) */
export const fmtK = (n) => (n / 1024).toLocaleString("en-US", { minimumFractionDigits: 1, maximumFractionDigits: 1 }) + "K";

/** one region's gauge in the sticky bar: {region, state: ok | tight | over, text: "used / capK", overText: "+NK" or "",
    fill: the used part (0..1 of the bar), capAt: the limit's mark when over (0..1, else null), tip: the exact bytes} */
export function gaugeModel(region, used, cap, over) {
  const m = barModel(region, used, cap, over);
  const isOver = over > 0;
  const scale = isOver ? used : cap;
  return { region, state: m.state, text: `${fmtK(used).slice(0, -1)} / ${fmtK(cap)}`, overText: isOver ? `+${fmtK(over)}` : "",
    fill: scale ? Math.min(1, (isOver ? cap : used) / scale) : 0, capAt: isOver && scale ? cap / scale : null,
    tip: `${region.toUpperCase()}: ${fmt(used)} of ${fmt(cap)} B` + (isOver ? `, over by ${fmt(over)} B` : `, ${fmt(-over)} B free`) };
}

/** the notices policy: what the page shows of a view (docs/WEB.md "What it does", Messages).
    -> {errors: [{kind, head, text, sub: [lines], keys}], experimental: [{key, label, text}], notes: [{text, labels}]}
    errors: what stops a build or what the user must change (a parse error, an overflow as ONE line, an
    unmet rule); experimental: one line per EXPERIMENTAL item that is on; notes: everything else (the NOTICE texts:
    provenance, "tested in the emulator only", credits; the hints; what is not measured), identical texts once. */
export function messagesOf(view, reg) {
  const ev = view.eval || {}, byKey = reg.byKey;
  const errors = [], experimental = [], notes = [];
  if (ev.error) errors.push({ kind: "parse", head: "Error", text: ev.error, sub: [], keys: [] });
  const ov = overflowLines(view.over, view.savings, byKey);
  if (ov.length) {
    errors.push({ kind: "over", head: "Over budget", text: ov.map((o) => `${o.region.toUpperCase()} +${fmtK(o.over)}`).join(", "),
      sub: [...ov.map((o) => o.biggest), "Red sizes in the list hold the overflow; ◀ off = fits marks an item whose removal alone fits."], keys: [] });
  }
  for (const e of ev.errors || []) errors.push({ kind: "rule", head: "Error", text: e.text, sub: [], keys: e.keys || [] });
  const seen = new Set();
  const addExp = (key, text) => {
    if (seen.has(key) || !byKey[key]) return;
    seen.add(key);
    experimental.push({ key, label: byKey[key].label, text });
  };
  for (const k of view.built || []) if (byKey[k] && byKey[k].experimental) addExp(k, byKey[k].tested || "");
  const groups = new Map();
  const note = (text, label) => {
    if (!groups.has(text)) groups.set(text, []);
    if (label && !groups.get(text).includes(label)) groups.get(text).push(label);
  };
  for (const w of ev.warnings || []) {
    if (/EXPERIMENTAL/.test(w.text)) { for (const k of w.keys || []) addExp(k, w.text); continue; }
    note(w.text, null);
  }
  for (const n of ev.notices || []) {
    const k = (n.keys || [])[0], label = k && byKey[k] ? byKey[k].label : null;
    const text = label && n.text.startsWith(label + ": ") ? n.text.slice(label.length + 2) : n.text;
    note(text, label);
  }
  if (ev.budget && ev.budget.unmeasured && ev.budget.unmeasured.length) note(`Not measured yet (no cost in the estimate): ${ev.budget.unmeasured.join(", ")}.`, null);
  for (const [text, labels] of groups) notes.push({ text, labels });
  return { errors, experimental, notes };
}

/** the item has notes beyond its description: an (i) button on its row opens them (EXPERIMENTAL and how it was tested,
    its notice, where it comes from, what turning it off loses) */
export const hasInfo = (it) => !!(it.experimental || it.notice || it.provenance_line || it.off_warning);

/** a proposal's change as a short line (docs/WEB.md "What it does", Make it fit): "FM6 off", "Delay length: 64K -> 32K", "keep 200 KB flash free -> 0 KB" */
export function changeText(c, byKey) {
  const it = byKey[c.key], name = (v) => (it && it.choices.length ? (it.choices.find((x) => x[0] === v) || [v, String(v)])[1] : v ? "on" : "off");
  if (c.to === 0) return `${c.label} off`;
  return `${c.label}: ${name(c.from)} -> ${name(c.to)}`;
}

/** the first sentence of a registry description, at most `max` characters: what the user loses, in one plain line */
export function oneLine(text, max = 110) {
  const s = String(text || "").replace(/\s+/g, " ").trim();
  const m = s.match(/^.*?[.!?](?=\s|$)/);
  const first = m ? m[0] : s;
  return first.length > max ? first.slice(0, max - 3).trimEnd() + "..." : first;
}
