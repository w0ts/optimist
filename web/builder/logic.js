// SPDX-License-Identifier: GPL-3.0-only
// The web builder's pure functions (no DOM, no Python): what the page lists and how, as the menu (tools/builder/menu.py)
// does it. The rules themselves (validate, the budget, fit, the key) run in Python: py-worker.js, tools/builder/web_api.py.
// node web/test_builder.mjs tests these.

export const PYODIDE_VERSION = "314.0.7";            // pinned (released 2026-09-14); docs/BUILDER-WEB.md
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

/** the presets: the flavours first (each its profile), then every shipped profile */
export function presets(prof) {
  const byName = Object.fromEntries((prof.profiles || []).map((p) => [p.name, p]));
  const out = [];
  for (const f of prof.flavours || []) {
    if (byName[f.profile]) out.push({ id: `flavour:${f.id}`, label: `${f.title} (${f.profile})`, kind: "flavour", text: byName[f.profile].text, name: f.profile, description: f.description || "" });
  }
  for (const p of prof.profiles || []) out.push({ id: `profile:${p.name}`, label: p.name + (p.published ? " (published)" : ""), kind: "profile", text: p.text, name: p.name, description: "" });
  out.push({ id: "defaults", label: "(registry defaults)", kind: "defaults", text: "", name: "default", description: "" });
  return out;
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
