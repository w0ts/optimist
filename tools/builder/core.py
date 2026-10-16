# SPDX-License-Identifier: GPL-3.0-only
"""The builder's core: one implementation of the rules for the menu, the CLI, CI and (later) a web page in Pyodide.

Pure: no file, git, subprocess or UI at import or in any call. What the runner reads from disk comes in as data:
the costs (tools/builder/costs.json) as a dict, the items the firmware drops without the automation store
(setup(needs_auto=...): configure.py reads them from firmware/src/core/*.h), a real build's sizes as `exact`.
The registry (registry.py) must be complete first: configure.py imports backports.py, which adds the backported
items from tools/backports.json.

  parse, validate, flags, header, budget, fits, fit ...   the rules (configure.py re-exports them unchanged)
  effective / canonical / cloud_key                       the image's values, their text and the cache key
  describe() / evaluate(request)                          the JSON API: dict in, dict out

The key (docs: optimist-work cloudbuild DESIGN.md, "Cache key and normalization"):
sha256("optimist-cloud/1\\n" + commit + "\\n" + canonical), canonical the sorted KEY=value lines of what the image
is built with (an option of an item left out at its default, the reserve items dropped, the retired items at the
value the image is still built as)."""
import functools
import hashlib
import re
import sys

import registry as R

KEY_TAG = "optimist-cloud/1"
NEEDS_AUTO = ()                 # (setup: the items the firmware switches off without the automation store)


def setup(needs_auto=None):
    """the data the core cannot read itself: needs_auto, the keys of configure._needs_auto (firmware headers)"""
    global NEEDS_AUTO
    if needs_auto is not None:
        NEEDS_AUTO = tuple(needs_auto)


def _stderr(msg):
    print(f"warning: {msg}", file=sys.stderr)


LIMITS = {"flash": 581564, "ram": 98304, "pool": 344064, "ramtext": 32512}
SPARE = {"flash": 0, "ram": 0, "pool": 8192, "ramtext": 0}   # build.py keeps 8 KiB of the pool spare
REGIONS = ("flash", "ram", "pool", "ramtext")
GRAIN_SETS = ("SET_PIANO", "SET_VIBES", "SET_FLUTE")       # the sets of GRAIN's presets (eng_grain.c)


class ConfigError(Exception):
    pass


# ---- reading and writing
def defaults():
    return {k: it.default for k, it in R.ITEMS.items()}


def parse(text, base=None, strict=True, warn=None, reject_unknown=False):
    """.config text -> (values, name); values start from base (default: the registry defaults). warn: called with
    each warning (default: printed on stderr); strict=False: an unknown key is ignored without a word;
    reject_unknown: an unknown key is a ConfigError (untrusted input: a cloud build request)"""
    say = warn or _stderr
    cfg = dict(base if base is not None else defaults())
    name = ""
    merged_on = {}                                      # (a merged item on in the text: its survivor is on, whatever order)
    for n, ln in enumerate(text.splitlines(), 1):
        s = ln.strip()
        m = re.match(r"#\s*name:\s*(.+)", s)
        if m:
            name = m.group(1).strip()
            continue
        if not s or s.startswith("#"):
            continue
        m = re.fullmatch(r"([A-Z0-9_]+)\s*=\s*(-?\d+)", s)
        if not m:
            raise ConfigError(f"line {n}: expected KEY=number, got {s!r}")
        k, v = m.group(1), int(m.group(2))
        if k in R.FORBIDDEN:
            raise ConfigError(f"{k}: never offered ({R.FORBIDDEN[k]})")
        if k in R.MERGED:                               # (merged into another: the survivor is on if either was)
            into, why = R.MERGED[k]
            say(f"line {n}: {k} was merged into {into} ({why})")
            merged_on[into] = merged_on.get(into, 0) | int(v != 0)
            continue
        if k in R.RETIRED:                              # (dropped: still loads, ignored, said so)
            say(f"line {n}: {k} was dropped and the setting is ignored ({R.RETIRED[k]})")
            continue
        if k in R.MIGRATE:                              # (an item that became others: registry.py MIGRATE)
            cfg.update(R.MIGRATE[k](v))
            continue
        if k not in R.ITEMS:                            # (a newer or an older builder's item: ignored, said so)
            if reject_unknown:
                raise ConfigError(f"line {n}: {k} is not an item of this builder")
            if strict:
                say(f"line {n}: {k} is not an item of this builder and the setting is ignored")
            continue
        cfg[k] = v
    for into, on in merged_on.items():
        cfg[into] = cfg[into] | on
    return cfg, name


def dump(cfg, name="", full=False):
    """values -> .config text (only what differs from the defaults unless full)"""
    out = [f"# name: {name}"] if name else []
    out.append("# firmware builder configuration (tools/builder/registry.py keys; others take their defaults)")
    for g in R.GROUPS:
        rows = []
        for k, it in R.ITEMS.items():
            if it.group == g and (full or cfg[k] != it.default):
                rows.append(f"{k}={cfg[k]}")
        if rows:
            out.append(f"# {g}")
            out += rows
    return "\n".join(out) + "\n"


# ---- meaning
def built(cfg, key):
    """the item is in the build: on (a choice: non-zero) and its parent on"""
    it = R.ITEMS[key]
    if it.parent and not built(cfg, it.parent):
        return False
    return cfg[key] != 0


class Issue(str):
    """a validate() message that names the registry items it concerns (.keys): the menu marks their lines. It is
    a str, so printing, joining and `in` work as on a plain message"""

    def __new__(cls, text, keys=()):
        s = super().__new__(cls, text)
        s.keys = tuple(k for k in keys if k in R.ITEMS)
        return s


def validate(cfg):
    """-> (errors, warnings, notices), each a list of Issue (a str with .keys: the items it concerns); errors stop
    a build"""
    err, warn, note = [], [], []
    for k, it in R.ITEMS.items():
        v = cfg.get(k, it.default)
        if it.is_choice and v not in [c[0] for c in it.choices]:
            err.append(Issue(f"{k}={v}: one of {[c[0] for c in it.choices]}", [k]))
        elif not it.is_choice and v not in (0, 1):
            err.append(Issue(f"{k}={v}: 0 or 1", [k]))
    engines = [k for k, it in R.ITEMS.items() if it.group == "Synth engines" and not it.parent]
    if not any(built(cfg, k) for k in engines):
        err.append(Issue("at least one synth engine", engines))
    fm6_modes = ("FM6_MARK1", "FM6_MODERN", "FM6_OPL")
    if built(cfg, "ENG_FM6"):
        if not any(cfg[k] for k in fm6_modes):
            err.append(Issue("FM6 needs at least one ENGINE mode: switch on MARK I, MODERN or OPL (or FM6 off)",
                             ("ENG_FM6",) + fm6_modes))
        if cfg.get("FM6_MKI_FLASH") and not cfg["FM6_MARK1"]:
            warn.append(Issue("MARK I tables in flash without ENGINE mode MARK I: nothing to move (no effect)",
                              ("FM6_MKI_FLASH", "FM6_MARK1")))
        if not cfg["FM6_KEYS"] and not cfg["FM6_SYSEX"]:
            warn.append(Issue("FM6 without the operator editor and without DX7 SysEx: preset-only (no voice editing)",
                              ("FM6_KEYS", "FM6_SYSEX")))
    sx_users = [k for k in ("MICRO", "FILLS", "PLOCK") if k in R.ITEMS and cfg.get(k)]   # (SLOOP 2.4's sequencer)
    if sx_users and "SL24_XSTEP" in R.ITEMS and not cfg.get("SL24_XSTEP"):
        err.append(Issue("micro timing, fills and parameter locks need their storage: switch on SL24_XSTEP",
                         ["SL24_XSTEP"] + sx_users))
    if cfg.get("PATTERNS") and cfg.get("SECTIONS") == 4:
        err.append(Issue("patterns live in the section log: SECTIONS 8 or 16 (or PATTERNS off)", ["PATTERNS", "SECTIONS"]))
    revs = [k for k in R.REV_ALGOS if k in R.ITEMS]
    if built(cfg, "FX_REVERB") and not any(cfg[k] for k in revs):
        err.append(Issue("the reverb bus needs an algorithm: tick ROOM, PLATE, FDN8, SPRING or VTINY "
                         "(or the reverb bus off)",
                         ["FX_REVERB"] + revs))
    kits = [k for k in R.ITEMS if k.startswith("KIT_")]
    if not built(cfg, "DRUM_SYNTH") and not any(built(cfg, k) for k in kits):
        err.append(Issue("the drum track needs a drum source: the drum synth or a sampled kit",
                         ["DRUM_SYNTH", "DRUM_SAMPLED"] + kits))
    sets = [k for k in R.ITEMS if k.startswith("SET_")]
    if any(cfg[k] for k in sets) and not built(cfg, "ENG_SAMPLE") and not built(cfg, "ENG_GRAIN"):
        warn.append(Issue("sample sets without SAMPLE or GRAIN: nothing plays them (only the drums use PERC)",
                          ["ENG_SAMPLE", "ENG_GRAIN"] + [k for k in sets if cfg[k]]))
    if (built(cfg, "ENG_SAMPLE") or built(cfg, "ENG_GRAIN")) and not any(cfg[k] for k in sets):
        warn.append(Issue("SAMPLE / GRAIN without a built-in set: they play the USR slots only (their PRESETS "
                          "entry: INIT, the engine's defaults; SAMPLE: GM KIT with the sampled kits)",
                          ["ENG_SAMPLE", "ENG_GRAIN"] + sets))
    elif built(cfg, "ENG_GRAIN") and not any(cfg[k] for k in GRAIN_SETS):
        warn.append(Issue("GRAIN's presets play PIANO, VIBES or FLUTE: without them it has GRAIN PAD on the first "
                          "melodic set built (none: INIT, its defaults)", ["ENG_GRAIN"] + list(GRAIN_SETS)))
    if built(cfg, "FX_DUCK") and not cfg["DRUM_SYNTH"] and not built(cfg, "DRUM_SAMPLED"):
        warn.append(Issue("DUCK follows the kick", ("FX_DUCK", "DRUM_SYNTH", "DRUM_SAMPLED")))
    for k, it in R.ITEMS.items():
        if it.off_warning and not cfg[k] and (not it.parent or built(cfg, it.parent)):
            warn.append(Issue(f"{it.label} off: {it.off_warning}", [k]))
        if built(cfg, k):
            if it.experimental:
                warn.append(Issue(f"{it.label}: EXPERIMENTAL ({it.tested})", [k]))
            if it.notice:
                note.append(Issue(f"{it.label}: {it.notice}", [k]))
    if reserve_undo(cfg) and not cfg.get("UNDO_HISTORY"):
        err.append(Issue("keeping undo history needs the undo / redo history (UNDO_HISTORY): without it there is a "
                         "single level", ["RESERVE_UNDO_KB", "UNDO_HISTORY"]))
    if cfg["USB_MODE"] == 2:
        warn.append(Issue("USB audio: EXPERIMENTAL, its audio quality on the FM-1 not checked yet (the CDC "
                          "console goes; +12 KB pool)", ["USB_MODE"]))
    return err, warn, note


def conflicts(cfg):
    """-> {key: [error messages]}: the items each validate() error concerns (an option's error marks its parent
    too: it may be folded away)"""
    out = {}
    for e in validate(cfg)[0]:
        for k in e.keys:
            out.setdefault(k, []).append(e)
            p = R.ITEMS[k].parent
            if p and e not in out.get(p, []):
                out.setdefault(p, []).append(e)
    return out


def flags(cfg):
    """-> ({C macro: value}, {generator env}) for this configuration"""
    out = {}
    for k, it in R.ITEMS.items():
        if not it.flag:
            continue
        v = cfg[k]
        if it.parent and not built(cfg, it.parent):
            v = it.default                              # (an option of an item left out: its default, ignored)
        out[it.flag] = v
    out["FELUCCA_USB_AUDIO"] = 1 if cfg["USB_MODE"] == 2 else 0
    out["FELUCCA_CDC"] = 1 if cfg["USB_MODE"] == 1 else 0
    out["FELUCCA_SLICE"] = cfg["ENG_SLICE"]
    if not built(cfg, "DRUM_SAMPLED") or not any(cfg[k] for k in R.ITEMS if k.startswith("KIT_")):
        out["FELUCCA_DRUM_SAMPLED"] = 0
    skip = [R.ITEMS[k].env for k in R.ITEMS if k.startswith("SET_") and not cfg[k]]
    if not out.get("FELUCCA_DRUM_SAMPLED", 1):
        skip.append("PERC")
    env = {"FELUCCA_SAMPLES_SKIP": ",".join(skip), "FELUCCA_SLICE": str(cfg["ENG_SLICE"])}
    return out, env


def fnv32(text):
    h = 0x811C9DC5
    for b in text.encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def cfg_hash(cfg):
    """of what the image holds: the reserve items (R.Item.no_image) change nothing in it, so not in the hash; a
    retired item the image is still built as (R.HASH_AS) counts at that value, so the hash of a configuration
    does not move when it is retired (the same image: its check builds in costs.json stay valid)"""
    full = dict(R.HASH_AS, **cfg)
    return fnv32("".join(f"{k}={full[k]}\n" for k in sorted(full)
                         if k in R.HASH_AS or not R.ITEMS[k].no_image)) & 0x7FFFFFFF


def cfg_bits(cfg):
    n = max(it.bit for it in R.ITEMS.values()) + 1
    bits = [0] * ((n + 6) // 7)
    for k, it in R.ITEMS.items():
        if built(cfg, k) and not it.no_image:
            bits[it.bit // 7] |= 1 << (it.bit % 7)
    return bits


def header(cfg, name="custom"):
    f, _ = flags(cfg)
    name = re.sub(r"[^A-Za-z0-9 +._-]", "", name or "custom")[:16]
    L = ["/* generated by tools/builder/configure.py: the build's configuration (firmware/src/core/registry.h) */",
         "#pragma once", f'#define FELUCCA_CFG_NAME "{name}"', f"#define FELUCCA_CFG_HASH {cfg_hash(cfg)}u",
         "#define FELUCCA_CFG_BITS {" + ", ".join(map(str, cfg_bits(cfg))) + "}"]
    target = {R.ITEMS[k].flag for k in R.ITEMS if R.ITEMS[k].target_only}
    for k in sorted(f):
        suffix = "u" if k in ("FELUCCA_DLY_LEN", "FELUCCA_PUNCH_N", "FELUCCA_SL_LEN", "LCD_BAUD") else ""
        body = [f"#ifndef {k}", f"#define {k} {f[k]}{suffix}", "#endif"]
        L += (["#ifdef __PI32V2__"] + body + ["#endif"]) if k in target else body
    return "\n".join(L) + "\n"


def item_delta(costs, key, value):
    """bytes this item at this value adds to the default build, per region (None: not measured)"""
    d = costs.get("deltas", {}).get(key, {}).get(str(value))
    return d


def item_delta_alone(costs, key, value):
    """what this item at this value adds to the default build, per region, the computed terms (model_terms: the
    reverb's line buffer, the automation store) included: the figure the menu shows (None: not measured; a value that is not valid
    alone, e.g. the last reverb algorithm off, gets its delta only)"""
    d = item_delta(costs, key, value)
    if d is None:
        return None
    cfg = dict(defaults(), **{key: value})
    parent = R.ITEMS[key].parent
    if parent and not R.ITEMS[parent].is_choice:
        cfg[parent] = 1
    if validate(cfg)[0]:
        return d
    t = model_terms(cfg)
    return {r: d.get(r, 0) + t[r] for r in REGIONS}


@functools.lru_cache(maxsize=None)
def _pair_conds(name):
    return tuple((k, int(v)) for k, v in (c.split("=") for c in name.split(",")))


def pair_conds(name):
    """a costs.json "pairs" key ("MOTION=1,SECTIONS=4") -> [(key, value)]"""
    return list(_pair_conds(name))


def built_parent(cfg, key):
    """the item's parent (if any) is in the build"""
    parent = R.ITEMS[key].parent
    return not parent or built(cfg, parent)


def pair_delta(cfg, costs):
    """what items set together cost beyond their own deltas (costs.json "pairs": measured with all of them set,
    less each one's delta; and the groups, group_delta), per region"""
    out = group_delta(cfg, costs)
    for name, d in (costs or {}).get("pairs", {}).items():
        conds = _pair_conds(name)
        if all(k in cfg and cfg[k] == v and built_parent(cfg, k) for k, v in conds):   # (an option of an item that is off is not in the build)
            for r in REGIONS:
                out[r] += d.get(r, 0)
    return out


def group_delta(cfg, costs):
    """costs.json "groups": items (off by default) that share code and state built with any of them (the insert
    effects' slots, core.h INS_FX): each one's delta pays the shared part, so with m of them on the sum counts it
    m - 1 times too often. Per region"""
    out = {r: 0 for r in REGIONS}
    groups = (costs or {}).get("groups", {})
    for name, g in groups.items():
        if "when" in g:
            continue
        for v in groups.values():                       # (a variant measured where its "when" holds: NAME|UI=1)
            if v.get("when") and v["members"] == g["members"] and all(cfg.get(k) == x for k, x in v["when"].items()):
                g = v
        m = sum(1 for k in g["members"] if k in cfg and built(cfg, k) and cfg[k] != R.ITEMS[k].default)
        for r in REGIONS:
            out[r] -= max(0, m - 1) * g["shared"].get(r, 0)
    return out


REV_ROOM_LEN = (1559 + 1931 + 2389 + 2791 + 12 + 2, 779 + 967 + 1193 + 1399 + 6 + 2)   # fx.c REV_ROOM_LEN (full, half rate)


def rev_lines(cfg):
    """-> {region: bytes} of the reverb's shared line buffer (fx.c and reverb_alt.c rev_line): the size of the
    largest algorithm built (ROOM 8684 samples, 4346 at half rate; PLATE / FDN8 / VTINY RV_N: 8192, 4096 at half
    rate, FDN8 twice that in the pool; SPRING 4096), in the pool with REV_POOL, else in main RAM. A maximum over
    several items and a region that REV_POOL picks: no per-item delta adds up to it, so the estimate computes it
    (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    f, _ = flags(cfg)

    def on(key):                                        # (flags: an option of an item left out has its default)
        return int(f.get(R.ITEMS[key].flag, 0) or 0)
    half, pool = on("REV_HALF"), on("REV_POOL")
    own = max(REV_ROOM_LEN[half] if on("REV_ROOM") else 0, 4096 * on("SPRING"))
    n = own
    if on("REV_PLATE") or on("REV_FDN8") or on("REV_AIRWIN"):
        n = max((4096 if half else 8192) << (1 if on("REV_FDN8") and pool else 0), own)
    if not on("FX_REVERB") or not n:
        n = 1                                           # (the C array's 1 when the reverb is not built)
    return {"pool" if pool else "ram": 2 * n}


AUTO_ITEMS = ("MOTION", "CHANCE", "SL24_XSTEP", "MICRO", "FILLS", "PLOCK", "COND")   # (backports24seq.h FELUCCA_AUTO)
AUTO_MAX, AUTO_NTRK = 128, 4                                                 # (seq/auto.h)
AUTO_LIST = 1 + 3 * AUTO_MAX                                                 # auto_list_t: n, AUTO_MAX x 3 bytes
AUTO_STORE = 4 + 4 + AUTO_NTRK * AUTO_LIST                                   # auto_store_t (seq/auto.c): psum, on + rsv, 4 lists
AUTO_AUX = 4                                                                 # storage/auto_proj.c AUTO_AUX
AUTO_ENC_MAX = 3 + AUTO_NTRK * (1 + 3 * AUTO_MAX)                            # auto.h AUTO_ENC_MAX (> the stepx form's)
SL24_XS = 4 * 176          # storage/project.c sl24_xs: NTRK stepx_t (seq/stepx.h, 176 B) in the pool, with SL24_IMPORT or SL24_EXPORT


# items only another item's code includes (the build is the same without it): counted only with that one. Found
# 2026-10-14: SEQTOOLS (ui/optimist/op_tools.c) put 5.5 KB of flash into user-default's estimate, which has SLOOP's UI
NEEDS = {"SEQTOOLS": {"UI": 1},
         "ED_SCRN": {"UI": 1}, "ED_SCRS": {"UI": 1}, "ED_SCOPE": {"UI": 1}, "ED_TOOLS": {"UI": 1, "SEQTOOLS": 1},   # (the web editor's commands that read the Optimist UI: nothing built with SLOOP's UI)
         "ED_FAVS": {"FAVORITES": 1}}


def counted(cfg, key):
    """the item's measured delta is in this configuration's build (NEEDS_AUTO, NEEDS)"""
    if key in NEEDS_AUTO and not auto_built(cfg):
        return False
    return all(cfg.get(k) == v for k, v in NEEDS.get(key, {}).items())


def auto_built(cfg):
    """the automation store is in the build (backports24seq.h FELUCCA_AUTO)"""
    f, _ = flags(cfg)
    return any(f.get(R.ITEMS[k].flag, 0) for k in AUTO_ITEMS)


def auto_store(cfg):
    """-> {region: bytes} of the automation store's storage (seq/auto.h, auto.c, storage/auto_proj.c), built with ANY
    of MOTION, CHANCE, SL24_XSTEP (and so MICRO, FILLS, PLOCK: FELUCCA_AUTO): the working store (auto_w) and the
    undo's copy of a list in main RAM; the project buffers' stores (auto_aux x AUTO_AUX, auto_slot x 4 with
    SECTIONS 4) and the extras record's read buffer (sx_rbuf) in the pool; with SL24_IMPORT or SL24_EXPORT, 2.4's step
    extras on their way in or out (project.c sl24_xs, pool: found 2026-10-12, 704 B under the estimate). Shared by those
    items: the estimate counts it once (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    f, _ = flags(cfg)
    if not any(f.get(R.ITEMS[k].flag, 0) for k in AUTO_ITEMS):
        return {"ram": 0, "pool": 0}
    slots = 0 if f.get("FELUCCA_SECTIONS", 16) > 4 else 4           # (SEC_LOGGED: the sections live in the log)
    rbuf = (4 + AUTO_ENC_MAX + 3) // 4 * 4 if not slots else (AUTO_ENC_MAX + 3) // 4 * 4   # (stepx_log.c / snapshots.c)
    export = SL24_XS if f.get(R.ITEMS["SL24_EXPORT"].flag, 0) or f.get(R.ITEMS["SL24_IMPORT"].flag, 0) else 0
    return {"ram": AUTO_STORE + AUTO_LIST, "pool": (AUTO_AUX + slots) * AUTO_STORE + rbuf + export}


# engines.c part_mem: NPART engine states in the pool, a union of the engines built that keep one there; bytes of
# the array with each member alone (sizes.json "symbols", measure_costs.py holds these against every build it makes)
PART_MEM = {"ENG_FM6": 11712, "ENG_PHYS": 38664, "ENG_GRAIN": 21060, "ENG_ACID": 1440}   # (2026-10-14)
PART_MEM_NONE = 0                                   # (none built: not in the image)


def part_mem(cfg):
    """-> {region: bytes} of the synth parts' engine states (engines.c part_mem, the pool): the largest member
    built. A maximum: no per-item delta adds up to it (GRAIN off frees 9.3 KB, but not with PHYS on), so the estimate
    computes it (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    sizes = [n for k, n in PART_MEM.items() if built(cfg, k)]
    return {"pool": max(sizes) if sizes else PART_MEM_NONE}


def model_terms(cfg):
    """-> {region: bytes} the computed part of the estimate, against the default build: what is not a sum of
    per-item deltas (the reverb's line buffer, rev_lines; the automation store, auto_store; the engines' part
    states, part_mem)"""
    d = defaults()
    terms = [(rev_lines(cfg), rev_lines(d)), (auto_store(cfg), auto_store(d)), (part_mem(cfg), part_mem(d))]
    return {r: sum(a.get(r, 0) - b.get(r, 0) for a, b in terms) for r in REGIONS}


def cfg_diff(cfg):
    """-> {key: value} the image items a configuration sets away from the defaults (costs.json "checks" and
    build/exact-sizes.json keep this: the configuration a real build was made as, an anchor of the estimate)"""
    d, e = effective(defaults()), effective(cfg)
    return {k: v for k, v in e.items() if v != d[k]}


def anchor_record(cfg):
    """-> {"cfg": cfg_diff, "built_hash"}: what costs.json and build/exact-sizes.json keep of a real build's
    configuration; built_hash checks it is read back as it was built (a registry default changed since: not)"""
    diff = cfg_diff(cfg)
    return {"cfg": diff, "built_hash": f"{cfg_hash(dict(defaults(), **diff)):08x}"}


def anchor_cfg(rec):
    """-> the configuration of an anchor_record, None when it no longer reads back as built"""
    if not isinstance(rec, dict) or not isinstance(rec.get("cfg"), dict) or any(k not in R.ITEMS for k in rec["cfg"]):
        return None
    cfg = dict(defaults(), **rec["cfg"])
    return cfg if f"{cfg_hash(cfg):08x}" == rec.get("built_hash") else None


def effective(cfg):
    """-> {key: value} what the image is built with: every image item, an option of an item left out at its default
    (the build ignores it), so two configurations that build the same image compare equal"""
    full = dict(defaults(), **cfg)
    return {k: full[k] if not it.parent or built(full, it.parent) else it.default
            for k, it in R.ITEMS.items() if not it.no_image}


def distance(a, b):
    """the number of image items two configurations build differently"""
    ea, eb = effective(a), effective(b)
    return sum(1 for k in ea if ea[k] != eb[k])


_ANCHOR_MEMO = {}          # (proposals() fills it for one costs while it runs: the anchors do not change within it)


def anchors(costs, exclude=(), extra=()):
    """-> [(name, cfg, real sizes)] the configurations whose real build is known: the defaults (costs.json "base"),
    the check configurations measured with the deltas (costs.json "checks", each with the configuration it was built
    as) and extra, more of them (configure.py: the real builds made from this very source, build/exact-sizes.json);
    exclude: names left out (tests/builder_test.py: a check configuration is estimated without its own build)"""
    memo = _ANCHOR_MEMO.get(id(costs)) if not exclude and not extra else None
    if memo is not None:
        return list(memo)
    out = [("defaults", defaults(), costs["base"])] if "defaults" not in exclude else []
    for n, e in sorted(costs.get("checks", {}).items()):
        acfg = anchor_cfg(e)
        if n not in exclude and e.get("sizes") and acfg:
            out.append((n, acfg, e["sizes"]))
    if not exclude and not extra and id(costs) in _ANCHOR_MEMO:
        _ANCHOR_MEMO[id(costs)] = out
    return out + list(extra)


# the estimate's margin per item between the configuration and the anchor it is estimated from (budget): the
# error of a change grows with the number of items changed (LTO shares and inlines differently with what else is
# built). Flash only: 150 single changes from a user's configuration (2026-10-14) were off by 0.2 KB of flash at the
# median and 1.5 KB at the 90th percentile, either way, while RAM, pool and RAM code were 0 at the median (a RAM
# margin of 32 B an item put the BLE test builds, 18 items from user-default, 576 B over). Errs over: a
# configuration predicted to fit must fit
ANCHOR_MARGIN = {"flash": 256, "ram": 0, "pool": 0, "ramtext": 0}


def additive(cfg, costs):
    """-> (total, items, unmeasured) the sum model: the default build, each item's measured delta, the pairs, the
    computed terms. Its error grows with the distance from the defaults (budget anchors it nearer)"""
    total = dict(costs["base"])
    items, missing = {}, []
    for k, it in R.ITEMS.items():
        if it.parent and not built(cfg, it.parent):
            continue                                    # (the parent's own delta covers its options)
        if cfg[k] == it.default:
            continue
        d = item_delta(costs, k, cfg[k])
        if d is None:
            missing.append(k)
            continue
        if not counted(cfg, k):
            continue                                    # (the firmware builds it as off: measured where it is on)
        items[k] = d
        for r in REGIONS:
            total[r] += d.get(r, 0)
    pairs, terms = pair_delta(cfg, costs), model_terms(cfg)
    for r in REGIONS:
        total[r] += pairs[r] + terms[r]
    return total, items, missing


def nearest_anchor(cfg, costs, exclude=(), extra=()):
    """-> (name, cfg, real sizes, distance) the anchor (anchors) the fewest items away; at the same distance the
    later one (a real build from this source over costs.json's)"""
    best = None
    for n, acfg, real in anchors(costs, exclude, extra):
        d = distance(cfg, acfg)
        if best is None or d <= best[3]:
            best = (n, acfg, real, d)
    return best


def budget(cfg, costs, anchor=True, exclude=(), exact=None, extra_anchors=()):
    """-> {"total": {region: bytes}, "items": {key: {region: delta}}, "unmeasured": [keys], "exact": bool,
    "anchor": name, "distance": items} (None without costs): the total is exact, the sizes of this configuration's
    last real build from this source, when given (configure.budget looks it up), else the estimate: the real build
    of the nearest configuration whose build is known (nearest_anchor: the defaults, a check configuration,
    extra_anchors) plus what the sum model (additive) says the items that differ change, plus ANCHOR_MARGIN per such
    item. Anchored, the items both share cost what they really cost together (LTO shares code between features, so a
    configuration many items from the defaults is several KB off the plain sum: 17.7 KB of flash for a user's,
    2026-10-14); anchor=False: the plain sum (measure_costs.py measures the pairs against it). The per-item deltas
    stay the sum model's either way"""
    if not costs:
        return None
    total, items, missing = additive(cfg, costs)
    ref = nearest_anchor(cfg, costs, exclude, extra_anchors) if anchor and not exact else None
    if exact:
        total = dict(exact)
    elif ref:
        name, acfg, real, dist = ref
        at, _, _ = additive(acfg, costs)
        total = {r: real[r] + total[r] - at[r] + ANCHOR_MARGIN[r] * dist for r in REGIONS}
    return {"total": total, "items": items, "unmeasured": missing, "exact": bool(exact),
            "anchor": ref[0] if ref else None, "distance": ref[3] if ref else None}


# ---- the reserve (R "Reserve" items: kept headroom, in KB in the .config; tools/build.py refuses a build below it)
def reserve_flash(cfg):
    """bytes of app flash the configuration keeps free"""
    return (cfg or {}).get("RESERVE_FLASH_KB", 0) * 1024


def reserve_undo(cfg):
    """bytes of undo history the configuration keeps (0: no minimum)"""
    return (cfg or {}).get("RESERVE_UNDO_KB", 0) * 1024


def undo_ring(total):
    """the undo history's ring a build leaves (app.ld _undo_*): the free main RAM plus the pool beyond its 8 KiB spare
    (docs/MEMORY-MAP.md 2.1); never below 0"""
    return max(0, LIMITS["ram"] - total["ram"]) + max(0, LIMITS["pool"] - SPARE["pool"] - total["pool"])


def undo_short(cfg, total):
    """bytes the ring falls short of the configuration's undo reserve (0: it keeps it, or there is no minimum)"""
    return max(0, reserve_undo(cfg) - undo_ring(total))


def reserve_errors(cfg, flash_free, ring):
    """-> [message] what a build leaves below the reserve (tools/build.py refuses it): flash_free is the app slot's
    free bytes, ring the undo history's bytes (None: this build has no history to keep)"""
    out = []
    if flash_free < reserve_flash(cfg):
        out.append(f"app flash free {flash_free} B < the reserve of {reserve_flash(cfg)} B (RESERVE_FLASH_KB)")
    if ring is not None and ring < reserve_undo(cfg):
        out.append(f"undo history ring {ring} B < the reserve of {reserve_undo(cfg)} B (RESERVE_UNDO_KB)")
    return out


def fits(total, cfg=None):
    """-> {region: (used, capacity, over)}: over > 0 overflows (the pool keeps its 8 KiB spare; the flash keeps the
    configuration's reserve)"""
    cap = {r: LIMITS[r] - SPARE[r] for r in REGIONS}
    cap["flash"] -= reserve_flash(cfg)
    return {r: (total[r], cap[r], total[r] - cap[r]) for r in REGIONS}


def kit_keys():
    """the sampled kits (they share the PERC samples)"""
    return [k for k in R.ITEMS if k.startswith("KIT_")]


def kit_shared(costs):
    """-> {region: bytes} the sampled kits share (the PERC samples): what all of them together add to a build
    that has none (their own deltas plus costs.json's "pairs" entry for all of them off); None: not measured"""
    kits = kit_keys()
    for name, d in (costs or {}).get("pairs", {}).items():
        conds = pair_conds(name)
        if kits and sorted(k for k, _ in conds) == sorted(kits) and all(v == 0 for _, v in conds):
            own = [item_delta(costs, k, 0) or {} for k in kits]
            return {r: -(d.get(r, 0) + sum(o.get(r, 0) for o in own)) for r in REGIONS}
    return None


def is_last_kit(cfg, key):
    """the item is the only sampled kit still in the build: switching it off drops the shared samples"""
    return key in kit_keys() and built(cfg, key) and not any(built(cfg, k) for k in kit_keys() if k != key)


def savings_of(cfg, costs):
    """what switching each built item off (or its smallest choice) would save, per region"""
    out = {}
    for k, it in R.ITEMS.items():
        if not built(cfg, k) or (it.parent and not built(cfg, it.parent)):
            continue
        alt = 0 if not it.is_choice else min((c[0] for c in it.choices), key=lambda v: v if v else 1 << 30)
        if alt == cfg[k]:
            continue
        here = item_delta(costs, k, cfg[k]) if cfg[k] != it.default else {r: 0 for r in REGIONS}
        there = item_delta(costs, k, alt) if alt != it.default else {r: 0 for r in REGIONS}
        if here is None or there is None:
            continue
        p_here, p_there = pair_delta(cfg, costs), pair_delta(dict(cfg, **{k: alt}), costs)
        t_here, t_there = model_terms(cfg), model_terms(dict(cfg, **{k: alt}))
        out[k] = {r: here.get(r, 0) - there.get(r, 0) + p_here[r] - p_there[r] + t_here[r] - t_there[r]
                  for r in REGIONS}
    return out


# ---- fitting: drop items in a fixed order of least loss until the estimate fits
FIT_ORDER = [  # (key, value): least loss first (docs/MEMORY-BUDGET.md section 3); samples last, PERC never
    ("ENG_SLICE", 0), ("ICONS", 0), ("DLY_LEN", 32768), ("OVERVIEW", 0), ("PUNCH_N", 16384),
    ("SL_LEN", 2048), ("ENG_LOFI", 0), ("ENG_FORMANT", 0), ("ENG_PHASE", 0), ("ENG_DRAWBAR", 0),
    ("FM6_OPL", 0), ("SET_SCRCH", 0), ("SET_STRGS", 0), ("SET_HORNS", 0),
    ("LCD_DIRTY", 0),   # (after the samples: for main RAM, 656 B, the graph strip redrawn whole; the defaults were 52 B over 2026-10-13)
]
FIT_MARGIN = {"flash": 1024, "ram": 512, "pool": 0, "ramtext": 256}   # the estimate's error (~0.5 %)


def over_any(cfg, costs, margin=FIT_MARGIN, budget_fn=budget):
    """-> {region: bytes over, the margin included}; budget_fn: configure.budget on the runner (real builds' sizes)"""
    b = budget_fn(cfg, costs)
    return {r: o + margin[r] for r, (_, _, o) in fits(b["total"], cfg).items() if o + margin[r] > 0}


def _fit_run(cfg, costs, keep, order, budget_fn):
    """-> (cfg after walking the order, [changes], fits): the fixed order of least loss as far as it gets"""
    cfg, changes = dict(cfg), []
    for k, v in order:
        over = over_any(cfg, costs, budget_fn=budget_fn)
        if not over:
            break
        if k in keep or cfg[k] == v or (R.ITEMS[k].parent and not built(cfg, R.ITEMS[k].parent)):
            continue
        trial = dict(cfg)
        trial[k] = v
        b0, b1 = budget_fn(cfg, costs)["total"], budget_fn(trial, costs)["total"]
        if not any(b1[r] < b0[r] for r in over):        # (it would not help a region that overflows)
            continue
        cfg = trial
        changes.append(f"{k}={v}")
    return cfg, changes, not over_any(cfg, costs, budget_fn=budget_fn)


def fit(cfg, costs, keep=(), order=FIT_ORDER, budget_fn=budget):
    """-> (cfg that fits by the estimate, [changes]); keep: keys never touched. None if nothing more to drop"""
    out, changes, ok = _fit_run(cfg, costs, keep, order, budget_fn)
    return (out, changes) if ok else (None, changes)


# ---- proposals: several ways to fit, each with what it changes, what is lost and the result (the web builder's panel;
# the terminal builder can offer the same). Deterministic; every one is made by the same rules as fit()
PROPOSAL_MAX = 4


def _off_value(cfg, key):
    """what switching the item off means (a choice: its smallest value that is not 0, else 0), None: nothing to drop"""
    it = R.ITEMS[key]
    alt = 0 if not it.is_choice else min((c[0] for c in it.choices), key=lambda v: v if v else 1 << 30)
    return None if alt == cfg[key] else alt


def _new_errors(before, after):
    old = validate(before)[0]
    return [e for e in validate(after)[0] if e not in old]


def _drop(cfg, key, value):
    """-> cfg with the item at `value` and what its dependants need (a validate() error the drop causes names items
    that were on: they go too, at most 3, never a synth engine), or None when that is not possible"""
    trial = dict(cfg, **{key: value})
    for _ in range(3):
        errs = _new_errors(cfg, trial)
        if not errs:
            return trial
        more = [k for e in errs for k in e.keys if k != key and trial.get(k) and R.ITEMS[k].group != "Synth engines"
                and not R.ITEMS[k].no_image]
        if not more:
            return None
        for k in sorted(set(more)):
            trial[k] = 0
    return None if _new_errors(cfg, trial) else trial


def _changes(before, after):
    """-> [{key, label, from, to, desc}] in registry order: the items that differ (children of a dropped parent
    are not listed: they follow it)"""
    out = []
    for k, it in R.ITEMS.items():
        if before[k] != after[k]:
            if it.parent and not built(after, it.parent) and before[it.parent] != after[it.parent]:
                continue
            out.append({"key": k, "label": it.label, "from": before[k], "to": after[k],
                        "desc": it.off_warning or it.desc, "reserve": it.no_image})
    return out


def _shown_over(cfg, costs, budget_fn):
    """a region is over its limit as the gauges show it (the estimate, without the fit margin)"""
    return any(o > 0 for _, _, o in fits(budget_fn(cfg, costs)["total"], cfg).values())


def _outcome(cfg, costs, budget_fn):
    b = budget_fn(cfg, costs)["total"]
    return {"after": {r: {"used": u, "capacity": c, "over": o} for r, (u, c, o) in fits(b, cfg).items()},
            "fits": not _shown_over(cfg, costs, budget_fn)}


def _greedy(cfg, costs, keep, budget_fn):
    """drop built items of the others, the ones that save most in the overflowing regions first, until it fits
    (the savings are those of the configuration it starts from: an estimate, the fit is checked each time)"""
    cfg = dict(cfg)
    sav = {k: v for k, v in savings_of(cfg, costs).items() if built(cfg, k)}
    for _ in range(40):
        over = over_any(cfg, costs, budget_fn=budget_fn)
        if not over:
            break
        best = None
        for k in sorted(sav):
            it = R.ITEMS[k]
            if k in keep or it.group == "Reserve" or it.no_image or not built(cfg, k):
                continue
            gain = sum(min(sav[k][r], over[r]) for r in over)
            if gain > 0 and (best is None or gain > best[0]):
                best = (gain, k)
        if best is None:
            break
        t = _drop(cfg, best[1], _off_value(cfg, best[1]))
        if t is None:
            keep = set(keep) | {best[1]}
            continue
        cfg = t
    return cfg


def proposals(cfg, costs, budget_fn=budget, limit=PROPOSAL_MAX):
    """-> [{id, title, text, values, changes, after, fits}]: up to `limit` ways to make the configuration fit by the
    estimate, best first; [] when it already fits. changes: what is switched off or lowered ({key, label, from, to,
    desc}, desc: what the user loses); after: {region: {used, capacity, over}}; fits: no gauge is over its limit.
    When nothing fits, the ones that come closest are still returned (fits False).
    The ways: "least" (fit(): the fixed order of least loss), "single" (the one item whose removal alone fits, the
    cheapest two), "reserves" (the reserves lowered to 0 first, then the least loss), "engines" / "fx" (keep every
    synth engine / every FX, drop from the others)"""
    _ANCHOR_MEMO[id(costs)] = None
    try:
        return _proposals(cfg, costs, _memo_budget(budget_fn), limit)
    finally:
        _ANCHOR_MEMO.pop(id(costs), None)


def _memo_budget(budget_fn):
    seen = {}

    def memo(cfg, costs, **kw):
        key = tuple(sorted(cfg.items()))
        if key not in seen:
            seen[key] = budget_fn(cfg, costs, **kw)
        return seen[key]
    return memo


def _proposals(cfg, costs, budget_fn, limit):
    if not _shown_over(cfg, costs, budget_fn):
        return []
    over0 = over_any(cfg, costs, budget_fn=budget_fn)
    made = []

    def add(pid, title, text, new):
        if new is None or new == cfg or any(new == m["values"] for m in made):
            return
        made.append({"id": pid, "title": title, "text": text, "values": new, "changes": _changes(cfg, new),
                     **_outcome(new, costs, budget_fn)})

    walked, _, _ = _fit_run(cfg, costs, (), FIT_ORDER, budget_fn)
    add("least", "Least loss (the builder's order)", "The fixed order of least loss: what costs the least to give up goes first.", walked)
    sav = savings_of(cfg, costs)
    singles = []
    for k in sorted(sav):                                   # (only items that save about what is over: then check it)
        it = R.ITEMS[k]
        if it.group == "Reserve" or it.no_image or any(sav[k][r] < over0[r] * 0.5 for r in over0):
            continue
        t = _drop(cfg, k, _off_value(cfg, k))
        if t is not None and not over_any(t, costs, budget_fn=budget_fn):
            singles.append((sum(sav[k].values()), k, t))
    for i, (_, k, t) in enumerate(sorted(singles)[:2]):
        add(f"single-{k}", f"Drop only {R.ITEMS[k].label}", "One item alone makes it fit; nothing else changes.", t)
    res = dict(cfg)
    for k in ("RESERVE_FLASH_KB", "RESERVE_UNDO_KB"):
        if k in res and res[k]:
            res[k] = 0
    if res != cfg:
        add("reserves", "Free the reserves first", "Lower the kept headroom to 0, then the order of least loss.",
            _fit_run(res, costs, (), FIT_ORDER, budget_fn)[0])
    engines = {k for k, it in R.ITEMS.items() if it.group == "Synth engines"}
    fx = {k for k, it in R.ITEMS.items() if it.group.startswith("FX")}
    for pid, title, keep in (("engines", "Keep my synth engines, drop the extras", engines), ("fx", "Keep my effects, drop the rest", fx)):
        add(pid, title, "Everything in that group stays; the others give way, the biggest savings first.",
            _greedy(_fit_run(cfg, costs, keep, FIT_ORDER, budget_fn)[0], costs, keep, budget_fn))
    fitting = [m for m in made if m["fits"]]
    if fitting:
        made = fitting + [m for m in made if not m["fits"]]
    else:
        made.sort(key=lambda m: sum(max(0, d["over"]) for d in m["after"].values()))
    return made[:limit]


# ---- the canonical form and the cache key (cloud builds)
def canonical(cfg):
    """-> the sorted KEY=value lines of what the image is built with (effective: the defaults filled in, an option
    of an item left out at its default, the reserve items dropped) plus the retired items at the value every image
    is still built as (R.HASH_AS, as cfg_hash counts them): two configurations that build the same image read alike"""
    full = dict(R.HASH_AS, **effective(cfg))
    return "".join(f"{k}={full[k]}\n" for k in sorted(full))


def cloud_key(cfg, commit):
    """-> sha256 hex of KEY_TAG, the full commit SHA and the canonical form: the name of a cached build"""
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", commit):
        raise ConfigError(f"commit: a full lower-case hex SHA, got {commit!r}")
    return hashlib.sha256(f"{KEY_TAG}\n{commit}\n{canonical(cfg)}".encode()).hexdigest()


# ---- the JSON API (dict in, dict out): the menu, the CLI, the workflow and a web page alike
def describe():
    """-> the registry and the constants a front end needs, JSON-serialisable"""
    return {"key_tag": KEY_TAG, "registry": R.to_json(), "regions": list(REGIONS), "limits": dict(LIMITS),
            "spare": dict(SPARE), "needs_auto": list(NEEDS_AUTO), "fit_order": [list(x) for x in FIT_ORDER]}


def _request_text(req):
    """a request's configuration as .config text: "text" as it is, or "config" ({KEY: int}) as KEY=value lines"""
    if "text" in req:
        if not isinstance(req["text"], str):
            raise ConfigError("text: a string")
        return req["text"]
    items = req.get("config", {})
    if not isinstance(items, dict):
        raise ConfigError("config: an object of KEY: integer")
    bad = [k for k, v in items.items() if not isinstance(k, str) or isinstance(v, bool) or not isinstance(v, int)]
    if bad:
        raise ConfigError(f"config: integer values only ({', '.join(map(str, bad[:5]))})")
    return "".join(f"{k}={v}\n" for k, v in items.items())


def _issues(lst):
    return [{"text": str(i), "keys": list(getattr(i, "keys", ()))} for i in lst]


def evaluate(req):
    """{"text": .config text | "config": {KEY: int}, "strict": reject unknown keys, "commit": full SHA (for the key),
    "costs": costs.json as a dict (for the budget), "exact": a real build's sizes} -> {"ok", "error", "name",
    "parse_warnings", "errors", "warnings", "notices", "config", "normalized", "canonical", "cfg_hash", "key",
    "budget", "fits", "fit"}; ok: it parses, validates without errors and (with costs) fits by the estimate"""
    if not isinstance(req, dict):
        return {"ok": False, "error": "the request: an object"}
    said = []
    try:
        cfg, name = parse(_request_text(req), warn=said.append, reject_unknown=bool(req.get("strict")))
        key = cloud_key(cfg, req["commit"]) if req.get("commit") is not None else None
    except ConfigError as e:
        return {"ok": False, "error": str(e), "parse_warnings": said}
    err, warn, note = validate(cfg)
    b = budget(cfg, req.get("costs"), exact=req.get("exact"))
    fit_rows = ({r: {"used": u, "capacity": c, "over": o} for r, (u, c, o) in fits(b["total"], cfg).items()}
                if b else None)
    fits_ok = all(x["over"] <= 0 for x in fit_rows.values()) if fit_rows else None
    return {"ok": not err and fits_ok is not False, "error": None, "name": name, "parse_warnings": said,
            "errors": _issues(err), "warnings": _issues(warn), "notices": _issues(note), "config": cfg,
            "normalized": effective(cfg), "canonical": canonical(cfg), "cfg_hash": f"{cfg_hash(cfg):08x}", "key": key,
            "budget": b, "fits": fit_rows, "fit": fits_ok}
