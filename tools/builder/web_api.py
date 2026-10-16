# SPDX-License-Identifier: GPL-3.0-only
"""The web builder's calls (web/builder/, in Pyodide): the core's rules (core.py) with the menu's views of them, JSON in,
JSON out. Pure like the core: no file, git or subprocess; the page hands in costs.json and the needs_auto list it
was exported with (tools/export_builder_web.py).

  init(costs, needs_auto)            once, after the registry and the backports are imported
  registry_view()                    the items as the menu lists them (groups, top-level order, children, flags)
  view(values, name, commit)         everything the page shows of a configuration: core.evaluate plus the menu's
                                     per-item savings, the validate() errors per item, the overflow, the undo ring
  toggle(values, key, value, room)   set one item as the menu's space does, BLE making room (room.py) as there
  item_info(key)                     the details panel: each value's cost alone, the pairs that name the item
  fit_view(values)                   core.fit: what the fixed order of least loss would change to fit
  proposals_view(values)             core.proposals: several ways to fit, each with its changes and its result
  parse_text(text)                   a .config file read as the CLI reads it (warnings, errors)

The budget is the estimate only (core.budget without real builds' sizes: there is no build/ in a browser), as
configure.budget(..., use_exact=False). room.py imports configure: outside the repository (the page) a stand-in
module with the core's names and this budget is registered under that name (init(..., standalone=True))."""
import sys
import types

import core as K
import registry as R

COSTS = None


def _configure_shim():
    """a `configure` module for room.py where the real one cannot load (no repository: the browser): the core's
    names, the costs given to init, the budget without real builds' sizes"""
    m = types.ModuleType("configure")
    for n in dir(K):
        if not n.startswith("__"):
            setattr(m, n, getattr(K, n))
    m.load_costs = lambda path=None: COSTS
    m.budget = lambda cfg, costs=None, use_exact=False, anchor=True, exclude=(): K.budget(
        cfg, costs or COSTS, anchor=anchor, exclude=exclude)
    m.over_any = lambda cfg, costs, margin=K.FIT_MARGIN: K.over_any(cfg, costs, margin)
    m.fit = lambda cfg, costs=None, keep=(), order=K.FIT_ORDER: K.fit(cfg, costs or COSTS, keep, order)
    return m


def init(costs, needs_auto=(), standalone=False):
    """the data the core cannot read itself (core.setup) and the costs every view uses; standalone: no repository
    around (the page), so room.py gets the stand-in configure module"""
    global COSTS
    COSTS = costs
    K.setup(needs_auto=needs_auto)
    if standalone and "configure" not in sys.modules:
        sys.modules["configure"] = _configure_shim()
    return {"items": len(R.ITEMS), "costs": bool(COSTS)}


def _room():
    import room as RM            # (after init: room imports configure)
    return RM


def registry_view():
    """-> core.describe() plus what the menu lists: per group its top-level items in order, each item's children in
    order (backports.py AFTER), what an EXPERIMENTAL item was tested on, its provenance as one line"""
    d = K.describe()
    extra = {k: {"children": list(it.children), "tested": it.tested,
                 "provenance_line": it.provenance.line() if it.provenance else ""} for k, it in R.ITEMS.items()}
    for item in d["registry"]["items"]:
        item.update(extra[item["key"]])
    d["top"] = {g: [it.key for it in R.top_level(g)] for g in R.GROUPS}
    d["kits"] = K.kit_keys()
    d["fit_margin"] = dict(K.FIT_MARGIN)
    return d


def _cfg(values):
    """a full configuration from the page's values (unknown keys ignored, missing ones at their default)"""
    cfg = K.defaults()
    for k, v in (values or {}).items():
        if k in cfg and isinstance(v, int) and not isinstance(v, bool):
            cfg[k] = v
    return cfg


def view(values, name="", commit=None):
    """-> {"eval": core.evaluate (with the costs), "savings", "conflicts", "over", "undo", "kit_shared",
    "last_kits", "dump"} of a configuration"""
    cfg = _cfg(values)
    ev = K.evaluate({"config": cfg, "costs": COSTS, "commit": commit})
    b = ev.get("budget")
    over = {r: o for r, (_, _, o) in K.fits(b["total"], cfg).items() if o > 0} if b else {}
    undo = None
    if b and K.built(cfg, "UNDO_HISTORY"):
        undo = {"ring": K.undo_ring(b["total"]), "short": K.undo_short(cfg, b["total"]), "keep": K.reserve_undo(cfg)}
    return {"eval": ev,
            "savings": K.savings_of(cfg, COSTS) if COSTS else {},
            "conflicts": {k: [str(e) for e in v] for k, v in K.conflicts(cfg).items()},
            "over": over, "undo": undo,
            "kit_shared": K.kit_shared(COSTS) if COSTS else None,
            "last_kits": [k for k in K.kit_keys() if K.is_last_kit(cfg, k)],
            "built": [k for k in R.ITEMS if K.built(cfg, k)],
            "dump": K.dump(cfg, name)}


def toggle(values, key, value, room=None):
    """the user set `key` to `value` (the menu's space / enter): -> {"values", "room": {"dropped", "note"},
    "notice"}; BLE ticked makes room as in the menu (room.after_toggle). room: the last call's "room" (or None)"""
    if key not in R.ITEMS:
        raise K.ConfigError(f"{key}: not an item")
    RM = _room()
    before = _cfg(values)
    after = dict(before, **{key: int(value)})
    old = RM.Room(tuple(tuple(x) for x in (room or {}).get("dropped", ())), (room or {}).get("note", ""))
    cfg, new = RM.after_toggle(before, after, key, old, COSTS)
    it = R.ITEMS[key]
    return {"values": cfg, "room": {"dropped": [list(x) for x in new.dropped], "note": new.note},
            "notice": it.notice if it.notice and cfg[key] else ""}


def item_info(key):
    """-> {"values": [[label, {region: bytes} | None]], "pairs": [[text, {region: bytes}]]}: what each value of the
    item costs alone against the default build (core.item_delta_alone, the menu's details panel) and the pairs
    (costs.json) whose cost depends on it"""
    it = R.ITEMS[key]
    rows = []
    for v in ([c[0] for c in it.choices] if it.is_choice else [0, 1]):
        d = K.item_delta_alone(COSTS, key, v) if v != it.default else {r: 0 for r in K.REGIONS}
        rows.append([dict(it.choices).get(v, "on" if v else "off"), v, d])
    pairs = []
    for name, d in (COSTS or {}).get("pairs", {}).items():
        conds = K.pair_conds(name)
        if any(k == key for k, _ in conds):
            pairs.append([" + ".join(f"{k}={v}" for k, v in conds), {r: d.get(r, 0) for r in K.REGIONS}])
    return {"values": rows, "pairs": pairs}


def fit_view(values):
    """-> {"values": the configuration that fits by the estimate (None: dropping the fixed order is not enough),
    "changes": ["KEY=value"]}"""
    cfg, changes = K.fit(_cfg(values), COSTS)
    return {"values": cfg, "changes": changes}


def proposals_view(values):
    """-> core.proposals: up to 4 ways to fit ([] when it fits), each {id, title, text, values, changes, after, fits}"""
    return K.proposals(_cfg(values), COSTS)


def parse_text(text):
    """a .config file: -> {"values", "name", "warnings", "error"} (core.parse, unknown keys warned and ignored)"""
    said = []
    try:
        cfg, name = K.parse(text, warn=said.append)
    except K.ConfigError as e:
        return {"values": None, "name": "", "warnings": said, "error": str(e)}
    return {"values": cfg, "name": name, "warnings": said, "error": None}
